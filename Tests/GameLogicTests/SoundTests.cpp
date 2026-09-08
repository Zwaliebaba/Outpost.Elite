#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "LookupTables.h"
#include "Music.h"
#include "SoundEffects.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The sound (phase 5).
 *
 * Compared on TWO things, because the sound has two halves. The game half -- `NOISE`, `NOISE2`,
 * `NOISEOFF`, `SOFLUSH` -- writes a buffer, and every byte of that buffer is compared after every
 * call, with the carry the routine returns. The interrupt half -- `SOINT` and the music player --
 * writes the chip, and what is compared there is the SEQUENCE of register writes the handler makes
 * in a frame, in order, because the order is what a SID hears: seven zeros and then a control byte
 * is a gate going down and up, and a comparison of the registers' final state could not see it.
 *
 * The interrupt half is run as the interrupt it is. `COMIRQ1` is entered with a return address and
 * a status byte on the stack, exactly as the 6510 leaves them, and runs to its `RTI`; the port's
 * `RunSoundInterrupt` runs beside it, and the two logs are compared write for write.
 */
namespace GameLogicTests
{

  namespace
  {
    bool OracleMissing()
    {
      const OracleImage& oracle = OracleImage::Instance();
      if (oracle.Available())
      {
        return false;
      }
      Logger::WriteMessage(("SKIPPED -- oracle absent: " + oracle.Reason()).c_str());
      return true;
    }

    std::wstring Widen(const std::string& _text)
    {
      return std::wstring(_text.begin(), _text.end());
    }

    /// 6502: SID -- the chip's registers, which the interpreter treats as memory.
    constexpr std::uint16_t SID_BASE = 0xD400;

    /// The labels the sound buffer lives at, resolved once.
    struct SoundLabels
    {
      std::uint16_t soflg, socnt, sopr, pulsew, sofrch, sofrq, socr, soatk, sosus, sovch, dnoiz;
      std::uint16_t noise, noise2, noiseoff, soflush, beep, comirq1, rastct, bomb;
      std::uint16_t mupla, dockingMusicOff, mufor, mudock, musilly, mulie;
      std::uint16_t bdbuff, counter, vibrato2Count, vibrato3Count, bddataptr1, bddataptr3;
      std::uint16_t commandSixTally, voice1Control, voice2Control, voice3Control, restLength, value5;
      std::uint16_t voice2NoteHigh, voice2NoteLow, voice2RaisedHigh, voice2RaisedLow, voice3NoteHigh, voice3NoteLow, voice3RaisedHigh, voice3RaisedLow;
      std::uint16_t bdbeqmod1, bdbeqmod2, bdlab24, bdlab23;
      std::uint16_t startbd, startat, stopbd, stopat, musicstart;

      explicit SoundLabels(const OracleImage& _oracle)
        : soflg(_oracle.Label("SOFLG")),
          socnt(_oracle.Label("SOCNT")),
          sopr(_oracle.Label("SOPR")),
          pulsew(_oracle.Label("PULSEW")),
          sofrch(_oracle.Label("SOFRCH")),
          sofrq(_oracle.Label("SOFRQ")),
          socr(_oracle.Label("SOCR")),
          soatk(_oracle.Label("SOATK")),
          sosus(_oracle.Label("SOSUS")),
          sovch(_oracle.Label("SOVCH")),
          dnoiz(_oracle.Label("DNOIZ")),
          noise(_oracle.Label("NOISE")),
          noise2(_oracle.Label("NOISE2")),
          noiseoff(_oracle.Label("NOISEOFF")),
          soflush(_oracle.Label("SOFLUSH")),
          beep(_oracle.Label("BEEP")),
          comirq1(_oracle.Label("COMIRQ1")),
          rastct(_oracle.Label("RASTCT")),
          bomb(_oracle.Label("BOMB")),
          mupla(_oracle.Label("MUPLA")),
          dockingMusicOff(_oracle.Label("MUTOK")),
          mufor(_oracle.Label("MUFOR")),
          mudock(_oracle.Label("MUDOCK")),
          musilly(_oracle.Label("MUSILLY")),
          mulie(_oracle.Label("MULIE")),
          bdbuff(_oracle.Label("BDBUFF")),
          counter(_oracle.Label("counter")),
          vibrato2Count(_oracle.Label("vibrato2")),
          vibrato3Count(_oracle.Label("vibrato3")),
          bddataptr1(_oracle.Label("BDdataptr1")),
          bddataptr3(_oracle.Label("BDdataptr3")),
          commandSixTally(_oracle.Label("value0")),
          voice1Control(_oracle.Label("value1")),
          voice2Control(_oracle.Label("value2")),
          voice3Control(_oracle.Label("value3")),
          restLength(_oracle.Label("value4")),
          value5(_oracle.Label("value5")),
          voice2NoteHigh(_oracle.Label("voice2lo1")),
          voice2NoteLow(_oracle.Label("voice2hi1")),
          voice2RaisedHigh(_oracle.Label("voice2lo2")),
          voice2RaisedLow(_oracle.Label("voice2hi2")),
          voice3NoteHigh(_oracle.Label("voice3lo1")),
          voice3NoteLow(_oracle.Label("voice3hi1")),
          voice3RaisedHigh(_oracle.Label("voice3lo2")),
          voice3RaisedLow(_oracle.Label("voice3hi2")),
          bdbeqmod1(_oracle.Label("BDbeqmod1")),
          bdbeqmod2(_oracle.Label("BDbeqmod2")),
          bdlab24(_oracle.Label("BDlab24")),
          bdlab23(_oracle.Label("BDlab23")),
          startbd(_oracle.Label("startbd")),
          startat(_oracle.Label("startat")),
          stopbd(_oracle.Label("stopbd")),
          stopat(_oracle.Label("stopat")),
          musicstart(_oracle.Label("musicstart"))
      {
      }
    };

    /// Write the port's buffer into the image, so both start from the same state.
    void LoadBuffer(Cpu6502& _cpu, const SoundLabels& _at, const Elite::SoundBuffer& _buffer)
    {
      for (std::size_t voice = 0; voice < 3u; ++voice)
      {
        const std::uint16_t offset = static_cast<std::uint16_t>(voice);
        _cpu.memory[static_cast<std::uint16_t>(_at.soflg + offset)] = _buffer.flag[voice];
        _cpu.memory[static_cast<std::uint16_t>(_at.socnt + offset)] = _buffer.counter[voice];
        _cpu.memory[static_cast<std::uint16_t>(_at.sopr + offset)] = _buffer.priority[voice];
        _cpu.memory[static_cast<std::uint16_t>(_at.sofrch + offset)] = _buffer.frequencyChange[voice];
        _cpu.memory[static_cast<std::uint16_t>(_at.sofrq + offset)] = _buffer.frequency[voice];
        _cpu.memory[static_cast<std::uint16_t>(_at.socr + offset)] = _buffer.control[voice];
        _cpu.memory[static_cast<std::uint16_t>(_at.soatk + offset)] = _buffer.attack[voice];
        _cpu.memory[static_cast<std::uint16_t>(_at.sosus + offset)] = _buffer.sustain[voice];
        _cpu.memory[static_cast<std::uint16_t>(_at.sovch + offset)] = _buffer.volumeRate[voice];
      }
      _cpu.memory[_at.pulsew] = _buffer.pulseWidth;
      _cpu.memory[_at.dnoiz] = _buffer.soundOff;
    }

    /// Every byte of the buffer, compared by name.
    void CompareBuffer(const Cpu6502& _cpu, const SoundLabels& _at, const Elite::SoundBuffer& _buffer, const std::wstring& _where)
    {
      struct Row
      {
        const wchar_t* name;
        std::uint16_t label;
        const std::array<std::uint8_t, 3>* ours;
      };
      const Row rows[] = {
        {L"SOFLG", _at.soflg, &_buffer.flag},       {L"SOCNT", _at.socnt, &_buffer.counter},
        {L"SOPR", _at.sopr, &_buffer.priority},     {L"SOFRCH", _at.sofrch, &_buffer.frequencyChange},
        {L"SOFRQ", _at.sofrq, &_buffer.frequency},  {L"SOCR", _at.socr, &_buffer.control},
        {L"SOATK", _at.soatk, &_buffer.attack},     {L"SOSUS", _at.sosus, &_buffer.sustain},
        {L"SOVCH", _at.sovch, &_buffer.volumeRate},
      };
      for (const Row& row : rows)
      {
        for (std::size_t voice = 0; voice < 3u; ++voice)
        {
          Assert::AreEqual<int>(_cpu.memory[static_cast<std::uint16_t>(row.label + voice)], (*row.ours)[voice],
                                (_where + L": " + row.name + L"+" + std::to_wstring(voice)).c_str());
        }
      }
      Assert::AreEqual<int>(_cpu.memory[_at.pulsew], _buffer.pulseWidth, (_where + L": PULSEW").c_str());
    }

    /*
     * Run an interrupt handler to its RTI.
     *
     * `CallSubroutine` pushes a return address and waits for an RTS to land on it; an interrupt ends
     * with RTI, which pops a status byte as well and does not add one to the address. So this pushes
     * what the 6510 pushes -- the return address itself and the flags -- and steps until the program
     * counter comes back to it.
     */
    void RunInterrupt(Cpu6502& _cpu, std::uint16_t _handler, const std::wstring& _where)
    {
      constexpr std::uint16_t STOP = 0xFFF9;
      _cpu.Push(static_cast<std::uint8_t>(STOP >> 8));
      _cpu.Push(static_cast<std::uint8_t>(STOP & 0xFFu));
      _cpu.Push(0x24u); // the flags the main loop would have: interrupts enabled, nothing else set
      _cpu.pc = _handler;

      for (std::uint32_t steps = 0; steps < 200'000u; ++steps)
      {
        if (_cpu.pc == STOP)
        {
          return;
        }
        Assert::IsTrue(_cpu.Step(), (_where + L": the handler hit an opcode the interpreter does not have").c_str());
      }
      Assert::Fail((_where + L": the handler did not return").c_str());
    }

    /// The interrupt's second pass: RASTCT = 1 is the dashboard raster, which is the one that
    /// reaches the sound.
    void RunSoundPass(Cpu6502& _cpu, const SoundLabels& _at, const std::wstring& _where)
    {
      _cpu.memory[_at.rastct] = 1u;
      _cpu.memory[_at.bomb] = 0u;
      _cpu.LogStores(SID_BASE, static_cast<std::uint16_t>(SID_BASE + 0x18));
      RunInterrupt(_cpu, _at.comirq1, _where);
    }

    /// The oracle's SID writes against the port's, write for write.
    void CompareWrites(const Cpu6502& _cpu, const Elite::SidWriteLog& _log, const std::wstring& _where)
    {
      Assert::AreEqual<std::size_t>(0, _log.dropped, (_where + L": the port's log overflowed").c_str());
      Assert::AreEqual<std::size_t>(_cpu.stores.size(), _log.count, (_where + L": how many SID writes").c_str());
      for (std::size_t index = 0; index < _log.count; ++index)
      {
        const std::wstring at = _where + L" write " + std::to_wstring(index);
        Assert::AreEqual<int>(_cpu.stores[index].address - SID_BASE, _log.writes[index].reg, (at + L": register").c_str());
        Assert::AreEqual<int>(_cpu.stores[index].value, _log.writes[index].value, (at + L": value").c_str());
      }
    }

    /// Something in every slot, so that a routine that reads the wrong byte reads a different one.
    Elite::SoundBuffer BusyBuffer(std::uint32_t _seed)
    {
      Elite::SoundBuffer buffer;
      std::uint32_t state = 0x2545F491u ^ (_seed * 0x9E3779B9u);
      auto next = [&state]()
      {
        state = state * 1103515245u + 12345u;
        return static_cast<std::uint8_t>(state >> 16);
      };
      for (std::size_t voice = 0; voice < 3u; ++voice)
      {
        // A flag with a plausible effect number, sometimes zero, sometimes with the "new" bit.
        const std::uint8_t roll = next();
        buffer.flag[voice] = (roll < 64u) ? std::uint8_t{0} : static_cast<std::uint8_t>(((roll % 17u) + 1u) | ((roll & 1u) ? 0x80u : 0u));
        buffer.counter[voice] = static_cast<std::uint8_t>(next() % 40u);
        buffer.priority[voice] = next();
        buffer.frequencyChange[voice] = next();
        buffer.frequency[voice] = next();
        buffer.control[voice] = next();
        buffer.attack[voice] = next();
        buffer.sustain[voice] = next();
        buffer.volumeRate[voice] = static_cast<std::uint8_t>((next() & 1u) ? 3u : 255u);
      }
      buffer.pulseWidth = (next() & 1u) ? 2u : 6u;
      return buffer;
    }

    /// Every byte of the music player, compared by name.
    void CompareMusic(const Cpu6502& _cpu, const SoundLabels& _at, const Elite::MusicPlayer& _music, const std::wstring& _where)
    {
      Assert::AreEqual<int>(_cpu.memory[_at.mupla], _music.playing, (_where + L": MUPLA").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.bdbuff], _music.buffer, (_where + L": BDBUFF").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.counter], _music.counter, (_where + L": counter").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.vibrato2Count], _music.vibrato2Count, (_where + L": vibrato2").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.vibrato3Count], _music.vibrato3Count, (_where + L": vibrato3").c_str());
      Assert::AreEqual<int>(_cpu.ReadWord(_at.bddataptr1), static_cast<int>(_music.pointer + _at.musicstart),
                            (_where + L": BDdataptr1").c_str());
      Assert::AreEqual<int>(_cpu.ReadWord(_at.bddataptr3), static_cast<int>(_music.restart + _at.musicstart),
                            (_where + L": BDdataptr3").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.commandSixTally], _music.commandSixTally, (_where + L": value0").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.voice1Control], _music.voice1Control, (_where + L": value1").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.voice2Control], _music.voice2Control, (_where + L": value2").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.voice3Control], _music.voice3Control, (_where + L": value3").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.restLength], _music.restLength, (_where + L": value4").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.voice2NoteHigh], _music.voice2NoteHigh, (_where + L": voice2lo1").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.voice2NoteLow], _music.voice2NoteLow, (_where + L": voice2hi1").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.voice2RaisedHigh], _music.voice2RaisedHigh, (_where + L": voice2lo2").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.voice2RaisedLow], _music.voice2RaisedLow, (_where + L": voice2hi2").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.voice3NoteHigh], _music.voice3NoteHigh, (_where + L": voice3lo1").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.voice3NoteLow], _music.voice3NoteLow, (_where + L": voice3hi1").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.voice3RaisedHigh], _music.voice3RaisedHigh, (_where + L": voice3lo2").c_str());
      Assert::AreEqual<int>(_cpu.memory[_at.voice3RaisedLow], _music.voice3RaisedLow, (_where + L": voice3hi2").c_str());

      /*
       * The two self-modified branch operands, read as the bit they stand for. The assembled operand
       * reaches the labelled half; the port's flag is false in that state, so the check is "does the
       * BEQ point at the label" against "is the flag clear".
       */
      const std::uint16_t target2 =
        static_cast<std::uint16_t>(_at.bdbeqmod1 + 2 + static_cast<std::int8_t>(_cpu.memory[_at.bdbeqmod1 + 1u]));
      const std::uint16_t target3 =
        static_cast<std::uint16_t>(_at.bdbeqmod2 + 2 + static_cast<std::int8_t>(_cpu.memory[_at.bdbeqmod2 + 1u]));
      Assert::AreEqual(target2 != _at.bdlab24, _music.vibrato2Raised, (_where + L": BDbeqmod1's operand").c_str());
      Assert::AreEqual(target3 != _at.bdlab23, _music.vibrato3Raised, (_where + L": BDbeqmod2's operand").c_str());
    }

    void LoadMusicOptions(Cpu6502& _cpu, const SoundLabels& _at, const Elite::MusicPlayer& _music, std::uint8_t _titleReset)
    {
      _cpu.memory[_at.mupla] = _music.playing;
      _cpu.memory[_at.dockingMusicOff] = _music.options.dockingMusicOff;
      _cpu.memory[_at.mufor] = _music.options.dockingMusicForced;
      _cpu.memory[_at.mudock] = _music.options.dockingPlaysTheme;
      _cpu.memory[_at.musilly] = _music.options.effectsDuringMusic;
      _cpu.memory[_at.mulie] = _titleReset;
    }
  } // namespace

  TEST_CLASS(TheSoundBufferMatchesTheShippedGame)
  {
  public:
    /*
     * 6502: NOISE, over every effect, both flag states, both carries, and 24 buffer states.
     *
     * The buffer states are what decide the routine: which voice is already playing the effect,
     * which has the lowest priority, whether the new effect's priority beats it. Random-but-seeded
     * buffers cover the comparisons both ways; the two extra states -- everything zero, everything
     * at 255 -- are the ones a fresh machine and a jammed one would be in.
     */
    TEST_METHOD(NoiseMatchesNOISE)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const SoundLabels at(oracle);

      std::uint32_t took = 0;
      std::uint32_t refused = 0;
      std::uint32_t passedThrough = 0;

      for (std::uint32_t state = 0; state < 26u; ++state)
      {
        for (std::uint32_t effect = 0; effect < 16u; ++effect)
        {
          for (const std::uint8_t flagBit : {0u, 128u})
          {
            for (const bool carryIn : {false, true})
            {
              Elite::SoundBuffer ours = BusyBuffer(state);
              if (state == 24u)
              {
                ours = Elite::SoundBuffer{};
              }
              else if (state == 25u)
              {
                for (std::size_t voice = 0; voice < 3u; ++voice)
                {
                  ours.priority[voice] = 255u;
                  ours.flag[voice] = static_cast<std::uint8_t>(voice + 1u);
                }
              }
              ours.soundOff = (state % 7u == 3u) ? 0xFFu : 0u;

              Cpu6502 cpu = oracle.Fresh();
              LoadBuffer(cpu, at, ours);
              cpu.y = static_cast<std::uint8_t>(effect | flagBit);
              cpu.c = carryIn;
              cpu.v = true; // NOISE opens with CLV; a port that read V before that would show here
              const Elite::Testing::RunResult run = cpu.CallSubroutine(at.noise, 20'000);
              Assert::IsTrue(run.completed, L"NOISE returned");

              const Elite::NoiseResult answer = Elite::PlaySoundEffect(ours, static_cast<Elite::SoundEffect>(effect | flagBit), carryIn);
              const bool carry = answer.carry;

              const std::wstring where = Widen("NOISE state " + std::to_string(state) + " effect " + std::to_string(effect) +
                                               (flagBit ? "+128" : "") + " carry " + std::to_string(carryIn ? 1 : 0));
              Assert::AreEqual(cpu.c, carry, (where + L": the carry").c_str());
              CompareBuffer(cpu, at, ours, where);

              if (ours.soundOff != 0u)
              {
                ++passedThrough;
              }
              else if (carry)
              {
                ++took;
              }
              else
              {
                ++refused;
              }
            }
          }
        }
      }

      Assert::IsTrue(took > 0 && refused > 0 && passedThrough > 0, L"all three answers were reached");
      Logger::WriteMessage(("NOISE: " + std::to_string(took) + " took a voice, " + std::to_string(refused) + " refused, " +
                            std::to_string(passedThrough) + " passed the carry through\n")
                             .c_str());
    }

    /// 6502: NOISE2 -- the same, with the sustain and frequency supplied. The explosions are the
    /// callers, and they pass 208 and 81 for the pitch; the sweep goes wider than that.
    TEST_METHOD(PitchedNoiseMatchesNOISE2)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const SoundLabels at(oracle);

      for (std::uint32_t state = 0; state < 12u; ++state)
      {
        for (const std::uint8_t effect : {2u, 3u, 7u, 14u})
        {
          for (const std::uint8_t frequency : {0u, 15u, 81u, 208u, 240u, 255u})
          {
            const std::uint8_t sustain = static_cast<std::uint8_t>(0x80u + state * 0x0Bu);

            Elite::SoundBuffer ours = BusyBuffer(state + 100u);
            Cpu6502 cpu = oracle.Fresh();
            LoadBuffer(cpu, at, ours);
            cpu.y = effect;
            cpu.a = sustain;
            cpu.x = frequency;
            cpu.c = (state & 1u) != 0u;
            const Elite::Testing::RunResult run = cpu.CallSubroutine(at.noise2, 20'000);
            Assert::IsTrue(run.completed, L"NOISE2 returned");

            const bool carry =
              Elite::PlaySoundEffectPitched(ours, static_cast<Elite::SoundEffect>(effect), sustain, frequency, (state & 1u) != 0u).carry;

            const std::wstring where = Widen("NOISE2 state " + std::to_string(state) + " effect " + std::to_string(effect) + " frequency " +
                                             std::to_string(frequency));
            Assert::AreEqual(cpu.c, carry, (where + L": the carry").c_str());
            CompareBuffer(cpu, at, ours, where);
          }
        }
      }
    }

    /// 6502: NOISEOFF, SOFLUSH and BEEP over the same buffers.
    TEST_METHOD(StoppingMatchesNOISEOFFAndSOFLUSH)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const SoundLabels at(oracle);

      for (std::uint32_t state = 0; state < 24u; ++state)
      {
        for (std::uint32_t effect = 0; effect < 16u; ++effect)
        {
          Elite::SoundBuffer ours = BusyBuffer(state + 200u);
          Cpu6502 cpu = oracle.Fresh();
          LoadBuffer(cpu, at, ours);
          cpu.y = static_cast<std::uint8_t>(effect);
          Assert::IsTrue(cpu.CallSubroutine(at.noiseoff, 20'000).completed, L"NOISEOFF returned");
          Elite::StopSoundEffect(ours, static_cast<Elite::SoundEffect>(effect));
          CompareBuffer(cpu, at, ours, Widen("NOISEOFF state " + std::to_string(state) + " effect " + std::to_string(effect)));
        }

        {
          Elite::SoundBuffer ours = BusyBuffer(state + 300u);
          Cpu6502 cpu = oracle.Fresh();
          LoadBuffer(cpu, at, ours);
          Assert::IsTrue(cpu.CallSubroutine(at.soflush, 20'000).completed, L"SOFLUSH returned");
          Elite::FlushSoundEffects(ours);
          CompareBuffer(cpu, at, ours, Widen("SOFLUSH state " + std::to_string(state)));
        }

        for (const bool carryIn : {false, true})
        {
          Elite::SoundBuffer ours = BusyBuffer(state + 400u);
          Cpu6502 cpu = oracle.Fresh();
          LoadBuffer(cpu, at, ours);
          cpu.c = carryIn;
          Assert::IsTrue(cpu.CallSubroutine(at.beep, 20'000).completed, L"BEEP returned");
          const bool carry = Elite::Beep(ours, carryIn).carry;
          const std::wstring where = Widen("BEEP state " + std::to_string(state));
          Assert::AreEqual(cpu.c, carry, (where + L": the carry").c_str());
          CompareBuffer(cpu, at, ours, where);
        }
      }
    }
  };

  TEST_CLASS(TheSoundInterruptMatchesTheShippedGame)
  {
  public:
    /*
     * 6502: SOINT through COMIRQ1 -- every effect started from a fresh buffer and run to its end,
     * comparing the register writes and the buffer on every frame.
     *
     * Run through the interrupt handler rather than by calling SOINT, because SOINT has no RTS: it
     * falls into `coffee`, which pops the registers the handler pushed and returns from the interrupt.
     * The handler's screen half writes VIC registers on the way through, which the interpreter treats
     * as memory, and its `BIT BOMB` reads a byte this test keeps at zero.
     */
    TEST_METHOD(EveryEffectPlaysOutAsSOINTDoes)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const SoundLabels at(oracle);

      std::uint32_t frames = 0;
      std::uint32_t writes = 0;

      for (std::uint32_t effect = 0; effect < 16u; ++effect)
      {
        Elite::SoundBuffer ours;
        Elite::MusicPlayer music;
        Cpu6502 cpu = oracle.Fresh();
        LoadBuffer(cpu, at, ours);
        LoadMusicOptions(cpu, at, music, 0u);

        cpu.y = static_cast<std::uint8_t>(effect);
        Assert::IsTrue(cpu.CallSubroutine(at.noise, 20'000).completed, L"NOISE returned");
        Assert::IsTrue(Elite::PlaySoundEffect(ours, static_cast<Elite::SoundEffect>(effect), false).carry,
                       L"a fresh buffer takes any effect");

        // The longest effect is the E.C.M. at 255 frames; run past that so every one ends.
        for (std::uint32_t frame = 0; frame < 260u; ++frame)
        {
          const std::wstring where = Widen("effect " + std::to_string(effect) + " frame " + std::to_string(frame));
          RunSoundPass(cpu, at, where);

          Elite::SidWriteLog log;
          Elite::RunSoundInterrupt(ours, music, log);

          CompareWrites(cpu, log, where);
          CompareBuffer(cpu, at, ours, where);
          ++frames;
          writes += static_cast<std::uint32_t>(log.count);
        }

        Assert::AreEqual<int>(0, ours.flag[0] | ours.flag[1] | ours.flag[2], Widen("effect " + std::to_string(effect) + " ended").c_str());
      }

      Logger::WriteMessage(
        ("SOINT: " + std::to_string(frames) + " frames, " + std::to_string(writes) + " register writes compared\n").c_str());
    }

    /*
     * Three voices at once, with effects arriving while others play -- the priority and the
     * "already playing" paths under the interrupt, and the pulse width's one-exit flip.
     */
    TEST_METHOD(OverlappingEffectsMatchSOINT)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const SoundLabels at(oracle);

      // Effect to start on each frame, or 255 for none. Chosen so that voices fill, refuse, replace
      // and empty in turn, and so that voice 1 is silent on some frames and not on others.
      const std::array<std::uint8_t, 64> script = {
        0,   255, 255, 5,   255, 6,   255, 255, 0,   255, 255, 255, 2,   255, 9,   255, 255, 255, 255, 255, 1, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 3,   255, 255, 4, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 13,  255, 255, 255, 255, 7,
      };

      for (const std::uint8_t musilly : {0u, 128u})
      {
        Elite::SoundBuffer ours;
        Elite::MusicPlayer music;
        music.options.effectsDuringMusic = musilly;
        Cpu6502 cpu = oracle.Fresh();
        LoadBuffer(cpu, at, ours);
        LoadMusicOptions(cpu, at, music, 0u);

        for (std::uint32_t frame = 0; frame < 300u; ++frame)
        {
          const std::uint8_t effect = (frame < script.size()) ? script[frame] : std::uint8_t{255};
          if (effect != 255u)
          {
            cpu.y = effect;
            cpu.c = false;
            Assert::IsTrue(cpu.CallSubroutine(at.noise, 20'000).completed, L"NOISE returned");
            const bool carry = Elite::PlaySoundEffect(ours, static_cast<Elite::SoundEffect>(effect), false).carry;
            Assert::AreEqual(cpu.c, carry, Widen("frame " + std::to_string(frame) + ": NOISE's carry").c_str());
          }

          const std::wstring where = Widen("overlap musilly " + std::to_string(musilly) + " frame " + std::to_string(frame));
          RunSoundPass(cpu, at, where);

          Elite::SidWriteLog log;
          Elite::RunSoundInterrupt(ours, music, log);

          CompareWrites(cpu, log, where);
          CompareBuffer(cpu, at, ours, where);
        }
      }
    }
  };

  TEST_CLASS(TheMusicMatchesTheShippedGame)
  {
  public:
    /*
     * 6502: startbd and startat, over the option flags that gate them, then 2,000 interrupts of
     * each tune with the register writes and the player's every byte compared on each.
     *
     * The plan's acceptance is the first 2,000 ticks; the docking music repeats well inside that
     * and the theme most of the way through, so command 9's rewind is exercised for one and the
     * longest chains for both.
     */
    TEST_METHOD(BothTunesPlayAsBDirqhereDoes)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const SoundLabels at(oracle);

      for (const bool theme : {false, true})
      {
        Elite::SoundBuffer ours;
        Elite::MusicPlayer music;
        Cpu6502 cpu = oracle.Fresh();
        LoadBuffer(cpu, at, ours);
        LoadMusicOptions(cpu, at, music, 0u);
        cpu.LogStores(SID_BASE, static_cast<std::uint16_t>(SID_BASE + 0x18));

        Assert::IsTrue(cpu.CallSubroutine(theme ? at.startat : at.startbd, 200'000).completed, L"the start returned");
        Elite::SidWriteLog started;
        Elite::MemoryMap map;
        map.port = cpu.memory[0x0001u]; // 6502: l1 -- `april16` brackets `BDENTRY` with `SETL1`
        if (theme)
        {
          Elite::StartTheme(music, map, started);
        }
        else
        {
          Elite::StartDockingMusic(music, map, started);
        }

        const std::wstring tune = theme ? L"theme" : L"docking";
        CompareWrites(cpu, started, tune + L" start");
        CompareMusic(cpu, at, music, tune + L" start");
        Assert::AreEqual<int>(0xFF, music.playing, (tune + L": playing").c_str());

        /*
         * The plan's acceptance is 2,000 interrupts. Neither tune rewinds inside that -- the Blue Danube
         * runs for minutes -- so the docking tune is run on until it has, plus a few hundred more, so
         * that command 9 and everything after a rewind is compared too. The theme is left at the
         * acceptance figure; it is the same player on the same commands.
         */
        const std::uint32_t atLeast = 2'000u;
        const std::uint32_t atMost = theme ? 2'000u : 24'000u;
        std::uint32_t writes = 0;
        std::uint32_t rewinds = 0;
        std::uint32_t ticks = 0;
        std::uint32_t firstRewind = 0;
        for (std::uint32_t tick = 0; tick < atMost; ++tick)
        {
          const std::wstring where = tune + L" tick " + std::to_wstring(tick);
          const std::uint16_t before = music.pointer;
          RunSoundPass(cpu, at, where);

          Elite::SidWriteLog log;
          Elite::RunSoundInterrupt(ours, music, log);

          CompareWrites(cpu, log, where);
          CompareMusic(cpu, at, music, where);
          CompareBuffer(cpu, at, ours, where);
          writes += static_cast<std::uint32_t>(log.count);
          ticks = tick + 1u;
          if (music.pointer < before)
          {
            if (rewinds == 0)
            {
              firstRewind = tick;
            }
            ++rewinds;
          }
          if (tick >= atLeast && rewinds > 0 && tick > firstRewind + 300u)
          {
            break;
          }
        }
        if (!theme)
        {
          Assert::IsTrue(rewinds > 0, L"the docking tune rewound inside the run, so command 9 was compared");
        }
        Logger::WriteMessage((std::string(theme ? "theme" : "docking") + ": " + std::to_string(ticks) + " interrupts, " +
                              std::to_string(writes) + " register writes, " + std::to_string(rewinds) + " rewinds" +
                              (rewinds > 0 ? ", first at " + std::to_string(firstRewind) : std::string()) + "\n")
                               .c_str());
      }
    }


    /*
     * 6502: BDRO6 and BDRO11 -- the two commands neither shipped tune contains (M6-a-1).
     *
     * The coverage review named them: `bdro6` increments `value0`, which nothing reads, and
     * `bdro11` is a `JMP BDRO9` that rewinds the tune, and the Blue Danube and the title theme
     * between them play neither. So both were ported against the source alone and compared
     * against the original nowhere, and after M6-b nothing could ever ask it.
     *
     * WHAT IS PLAYED IS STILL THE GAME'S OWN DATA. The player is started as `startat` starts it
     * and then both pointers are moved to a byte inside `comudat` chosen because the nibbles from
     * there reach those two commands -- the same bytes, the same player, a different entry. A
     * synthetic tune would need the port to read its notes from somewhere other than `MUSIC_DATA`,
     * which is a change to the port to suit a test.
     *
     * `value4` IS SEEDED, and it has to be. Command 11's rewind is unconditional, so a run of
     * commands with no rest in it loops for ever -- on the original literally, and in the port
     * until `COMMANDS_PER_PASS` cuts it off, which is a guard the game has no equivalent of. A
     * rest length of three is what a tune's own command 12 would have left, and it makes every
     * pass end where the game's would.
     */
    TEST_METHOD(TheCommandsNoTunePlaysMatchBDirqhere)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const SoundLabels at(oracle);

      struct Place
      {
        std::uint16_t offset; ///< into MUSIC_DATA, which is `musicstart` on the oracle's side
        const char* what;
      };

      /*
       * Measured over the extracted data rather than chosen, and the measurement had to be made ON
       * THE ORACLE. Command 11's rewind is unconditional, so a run of commands with no rest and no
       * terminator in it repeats for ever -- and the port's `COMMANDS_PER_PASS` cuts that off after
       * four thousand commands while the interpreter spins until the fixture's budget runs out. The
       * port is the wrong instrument for finding an entry that TERMINATES, because it is the one
       * with the guard; these three complete sixty interrupts on the original and reach commands 6,
       * 9, 10, 11, 13 and 14 between them, with the pointer never leaving the extracted region.
       */
      const Place PLACES[] = {
        {918u, "6, 9, 10 and 11"},
        {1511u, "6, 9, 11, 13 and 14"},
        {2237u, "the same six from the title theme's half of the data"},
      };

      constexpr std::uint8_t REST = 3;
      constexpr std::uint32_t TICKS = 60;

      for (const Place& place : PLACES)
      {
        Elite::SoundBuffer ours;
        Elite::MusicPlayer music;
        Cpu6502 cpu = oracle.Fresh();
        LoadBuffer(cpu, at, ours);
        LoadMusicOptions(cpu, at, music, 0u);
        cpu.LogStores(SID_BASE, static_cast<std::uint16_t>(SID_BASE + 0x18));

        const std::wstring tune = L"comudat+" + std::to_wstring(place.offset);

        // 6502: startat -- the theme, with `april16`'s `SETL1` bracket around `BDENTRY`. Both
        // machines are put in the same state by the routine the game uses, and then moved.
        Assert::IsTrue(cpu.CallSubroutine(at.startat, 200'000).completed, (tune + L": the start returned").c_str());
        Elite::SidWriteLog started;
        Elite::MemoryMap map;
        map.port = cpu.memory[0x0001u];
        Elite::StartTheme(music, map, started);
        CompareWrites(cpu, started, tune + L" start");
        CompareMusic(cpu, at, music, tune + L" start");

        // 6502: BDdataptr1, BDdataptr3, value5 and value4 -- the four bytes that say where the
        // player is and how long a rest lasts, moved on both sides together.
        music.tuneStart = place.offset;
        music.pointer = place.offset;
        music.restart = place.offset;
        music.restLength = REST;
        const std::uint16_t address = static_cast<std::uint16_t>(place.offset + at.musicstart);
        cpu.memory[at.bddataptr1] = static_cast<std::uint8_t>(address & 0xFFu);
        cpu.memory[static_cast<std::uint16_t>(at.bddataptr1 + 1u)] = static_cast<std::uint8_t>(address >> 8);
        cpu.memory[at.bddataptr3] = static_cast<std::uint8_t>(address & 0xFFu);
        cpu.memory[static_cast<std::uint16_t>(at.bddataptr3 + 1u)] = static_cast<std::uint8_t>(address >> 8);
        cpu.memory[at.value5] = static_cast<std::uint8_t>(address & 0xFFu);
        cpu.memory[static_cast<std::uint16_t>(at.value5 + 1u)] = static_cast<std::uint8_t>(address >> 8);
        cpu.memory[at.restLength] = REST;

        std::uint32_t writes = 0;
        for (std::uint32_t tick = 0; tick < TICKS; ++tick)
        {
          const std::wstring where = tune + L" tick " + std::to_wstring(tick);
          RunSoundPass(cpu, at, where);

          Elite::SidWriteLog log;
          Elite::RunSoundInterrupt(ours, music, log);

          CompareWrites(cpu, log, where);
          CompareMusic(cpu, at, music, where);
          CompareBuffer(cpu, at, ours, where);
          writes += static_cast<std::uint32_t>(log.count);
        }

        /*
         * `value0` is command 6's only effect and nothing in the game reads it, so it is also the
         * only evidence the command ran at all -- which makes this assertion the one that says the
         * gap is closed rather than merely aimed at.
         */
        Assert::IsTrue(music.commandSixTally > 0u, (tune + L": command 6 ran, which is what value0 counts").c_str());
        Assert::AreEqual<int>(static_cast<int>(place.offset), static_cast<int>(music.restart),
                              (tune + L": the rewind point never moved").c_str());

        Logger::WriteMessage((std::string("commands at comudat+") + std::to_string(place.offset) + " (" + place.what + "): " +
                              std::to_string(TICKS) + " interrupts, " + std::to_string(writes) + " register writes, value0 " +
                              std::to_string(music.commandSixTally) + "\n")
                               .c_str());
      }
    }

    /// 6502: startbd, stopbd and stopat under every combination of the five flags that gate them.
    TEST_METHOD(StartingAndStoppingObeyTheOptions)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const SoundLabels at(oracle);

      for (std::uint32_t flags = 0; flags < 32u; ++flags)
      {
        for (const bool stopFirst : {false, true})
        {
          Elite::SoundBuffer ours = BusyBuffer(flags);
          Elite::MusicPlayer music;
          music.options.dockingMusicOff = (flags & 1u) ? 0x80u : 0u;
          music.options.dockingMusicForced = (flags & 2u) ? 0x80u : 0u;
          music.options.dockingPlaysTheme = (flags & 4u) ? 0x80u : 0u;
          const std::uint8_t titleReset = (flags & 8u) ? 0x80u : 0u;
          music.playing = (flags & 16u) ? 0xFFu : 0u;
          if (music.playing != 0u)
          {
            // A player mid-tune, so that a stop has something to stop and a start something to refuse.
            music.tuneStart = Elite::MUSIC_THEME_OFFSET;
            music.pointer = static_cast<std::uint16_t>(Elite::MUSIC_THEME_OFFSET + 40u);
            music.restart = Elite::MUSIC_THEME_OFFSET;
          }

          Cpu6502 cpu = oracle.Fresh();
          LoadBuffer(cpu, at, ours);
          LoadMusicOptions(cpu, at, music, titleReset);
          cpu.memory[at.bddataptr1] = static_cast<std::uint8_t>((music.pointer + at.musicstart) & 0xFFu);
          cpu.memory[static_cast<std::uint16_t>(at.bddataptr1 + 1u)] = static_cast<std::uint8_t>((music.pointer + at.musicstart) >> 8);
          cpu.memory[at.bddataptr3] = static_cast<std::uint8_t>((music.restart + at.musicstart) & 0xFFu);
          cpu.memory[static_cast<std::uint16_t>(at.bddataptr3 + 1u)] = static_cast<std::uint8_t>((music.restart + at.musicstart) >> 8);
          cpu.memory[at.value5] = static_cast<std::uint8_t>((music.tuneStart + at.musicstart) & 0xFFu);
          cpu.memory[static_cast<std::uint16_t>(at.value5 + 1u)] = static_cast<std::uint8_t>((music.tuneStart + at.musicstart) >> 8);
          cpu.LogStores(SID_BASE, static_cast<std::uint16_t>(SID_BASE + 0x18));

          const std::wstring where = Widen("flags " + std::to_string(flags) + (stopFirst ? " stopbd" : " startbd"));
          Elite::SidWriteLog log;
          Elite::MemoryMap map;
          map.port = cpu.memory[0x0001u]; // 6502: l1, as the image has it before the routine runs
          if (stopFirst)
          {
            Assert::IsTrue(cpu.CallSubroutine(at.stopbd, 200'000).completed, L"stopbd returned");
            Elite::StopDockingMusic(music, titleReset, ours, map, log);
          }
          else
          {
            Assert::IsTrue(cpu.CallSubroutine(at.startbd, 200'000).completed, L"startbd returned");
            Elite::StartDockingMusic(music, map, log);
          }

          CompareWrites(cpu, log, where);

          // 6502: l1 -- `april16` and `stopat` bracket their SID writes with `SETL1`, which the
          // port dropped until M3-b-3a: the routines said so in their comments and did not do it.
          Assert::AreEqual(cpu.memory[0x0001u], map.port, (where + L": l1 after the bracket").c_str());
          CompareMusic(cpu, at, music, where);
          CompareBuffer(cpu, at, ours, where);
        }
      }
    }
  };

} // namespace GameLogicTests
