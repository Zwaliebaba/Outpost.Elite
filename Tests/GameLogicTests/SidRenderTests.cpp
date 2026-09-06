#include "pch.h"

#include "Music.h"
#include "SoundEffects.h"

#include "SidSynth.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The synthesiser's OUTPUT, hashed (slice 4f's tail, plan §6.156).
 *
 * THIS IS THE ONE THING IN THE PORT WITH NO ORACLE AND NO PROSPECT OF ONE. Everything upstream of
 * it is compared against the shipped game: `NOISE` and its friends write a buffer that is compared
 * byte for byte, and `SOINT` writes SID registers whose whole sequence is compared write for write
 * over 4,160 frames (§6.129). What happens after those registers reach a 6581 is not something the
 * assembled game can be asked about, because the game never rendered audio -- it wrote to a chip.
 *
 * So `SidSynth` is fidelity by construction and R6 closed on the owner listening to it (2026-09-06).
 * What that sign-off could not leave behind is a way for CI to notice the synthesiser changing, and
 * this is that: sixteen effects rendered to samples and hashed, with the hashes committed.
 *
 * WHAT A HASH HERE IS AND IS NOT. It is not evidence that the sound is RIGHT -- nothing in this
 * repository can be that, and the row says so. It is evidence that the sound is the SAME as the
 * sound a person listened to and accepted, which is the only claim a golden ever makes (Risk R10).
 * If one of these fails, the question is not "which is correct" but "was this change meant to alter
 * what the player hears"; if it was, listen to it and re-record, exactly as a golden image is
 * re-recorded with a visual diff attached.
 *
 * The clock and the sample rate are `SoundOutput`'s, so what is hashed is what the executable
 * plays and not a convenient approximation of it.
 */
namespace GameLogicTests
{

  namespace
  {
    /// 6502: the effect player runs once per interrupt, and the interrupt is once per frame.
    constexpr std::uint32_t FRAMES_PER_SECOND = 50;

    /// What `SoundOutput` runs the chip at: NTSC's clock and CD's sample rate.
    constexpr std::uint32_t CLOCK_HZ = 1'022'727;
    constexpr std::uint32_t SAMPLE_RATE = 44'100;

    /// The longest effect is the E.C.M. at 255 frames; 260 runs every one of them past its end.
    constexpr std::uint32_t FRAMES = 260;

    constexpr std::size_t FRAME_SAMPLES = SAMPLE_RATE / FRAMES_PER_SECOND;

    /*
     * Render one effect from silence to its end, and hash every sample.
     *
     * FNV-1a over the little-endian sample bytes, which is the hash `Canvas::Hash` uses -- one
     * arithmetic to check rather than two, and its collision behaviour is irrelevant here because
     * what it is asked is "did this change", not "is this unique".
     */
    std::uint64_t RenderEffect(std::uint8_t _effect, bool _play = true)
    {
      Elite::SoundBuffer buffer;
      Elite::MusicPlayer music;
      Outpost::SidSynth synth(CLOCK_HZ, SAMPLE_RATE);

      /*
       * 6502: LDA #%00001111 / STA SID+&18 -- and it has to be done HERE because the effect player
       * never does it.
       *
       * `SOINT` programs the three voices and never writes register &18, so every sound in the game
       * plays at whatever volume something else left in it. Two routines leave 15: `COLD` at boot,
       * and `BDENTRY` when the theme starts. Rendering an effect into a chip at volume zero is
       * rendering silence, which is what the first run of this test produced -- sixteen identical
       * hashes -- and chasing that is what found `SoundOutput` relying on the title music to make
       * the game audible at all (§6.156).
       */
      synth.Write(0x18u, 0x0Fu);

      if (_play)
      {
        (void)Elite::PlaySoundEffect(buffer, _effect, false);
      }

      std::uint64_t hash = 14695981039346656037ull;
      std::vector<std::int16_t> samples(FRAME_SAMPLES);

      for (std::uint32_t frame = 0; frame < FRAMES; ++frame)
      {
        Elite::SidWriteLog log;
        Elite::RunSoundInterrupt(buffer, music, log);
        for (std::size_t index = 0; index < log.count; ++index)
        {
          synth.Write(log.writes[index].reg, log.writes[index].value);
        }

        synth.Render(samples.data(), samples.size());
        for (const std::int16_t sample : samples)
        {
          const std::uint16_t bits = static_cast<std::uint16_t>(sample);
          hash = (hash ^ static_cast<std::uint8_t>(bits & 0xFFu)) * 1099511628211ull;
          hash = (hash ^ static_cast<std::uint8_t>(bits >> 8)) * 1099511628211ull;
        }
      }
      return hash;
    }
  } // namespace

  TEST_CLASS(TheSynthesiserRender)
  {
  public:
    /*
     * Sixteen effects, sixteen hashes, and the hashes are in the file.
     *
     * Re-record only after listening: this is the audible half of slice 5a's acceptance, and the
     * whole reason it exists is that the acceptance was taken by ear and left nothing behind.
     */
    TEST_METHOD(EveryEffectRendersToItsRecordedHash)
    {
      // Recorded 2026-09-06, from the tree the owner listened to (Risk R6). Two per line so a
      // failing index is countable by eye.
      static constexpr std::uint64_t EXPECTED[16] = {
        0xD4FA18DE283B649Eull, 0xA1592B4EFF4E99E7ull,
        0x02EAF2C0E4831BFBull, 0xB64A7AC2DDE68C37ull,
        0x05B8BDA6F163FE73ull, 0x2205666B2A33E465ull,
        0x2E13A1DB1059D480ull, 0x4AEE62B6E8391FDDull,
        0xBA373868F54993BDull, 0x42549FE57631B8F2ull,
        0xF419753ED82B1825ull, 0x431F8AA9BCC6F7C3ull,
        0xEAC19B19108BC9F5ull, 0xF6CF613C52646A6Full,
        0x4037F04C39BA7477ull, 0x85C079CC4EAFBF7Full,
      };

      /*
       * EVERY hash is computed and LOGGED before any of them is asserted, which is not tidiness.
       * A re-record needs all sixteen numbers and a run that stopped at the first difference would
       * hand over one -- and then the second run would hand over the second, which is how a golden
       * gets re-recorded wrongly a line at a time (Risk R10).
       */
      std::array<std::uint64_t, 16> got{};
      std::string recorded;
      for (std::uint8_t effect = 0; effect < 16u; ++effect)
      {
        got[effect] = RenderEffect(effect);
        char text[32] = {};
        std::snprintf(text, sizeof(text), "0x%016llXull, ", static_cast<unsigned long long>(got[effect]));
        recorded += text;
      }
      Logger::WriteMessage(("SidSynth: " + recorded).c_str());

      /*
       * The silence to measure the sixteen against: the same chip, the same volume, the same two
       * hundred and sixty frames, and no effect played.
       *
       * This was `RenderEffect(255u)` on the belief that `NOISE` refuses 255. It does not: `SOUX6`
       * masks it to 127 and reads `SFXCNT,Y` and the rest of the tables at 127, which on the 6502
       * is whatever sits after them and in the port is a subscript past a sixteen-entry table --
       * an assertion in Debug, garbage in Release, and a "silence" hash made of that garbage
       * (plan §6.158).
       */
      const std::uint64_t silence = RenderEffect(0u, false);
      std::size_t silent = 0;

      for (std::uint8_t effect = 0; effect < 16u; ++effect)
      {
        silent += (got[effect] == silence) ? 1u : 0u;
        Assert::AreEqual<std::uint64_t>(EXPECTED[effect], got[effect],
                                        (L"effect " + std::to_wstring(effect)
                                         + L": the synthesiser's output changed. If that was intended, LISTEN to it and re-record; "
                                           L"the recorded hashes are what the owner accepted by ear (Risk R6).")
                                          .c_str());
      }

      Assert::AreEqual<std::size_t>(0u, silent, L"and not one of the sixteen renders to silence");
    }
  };

} // namespace GameLogicTests
