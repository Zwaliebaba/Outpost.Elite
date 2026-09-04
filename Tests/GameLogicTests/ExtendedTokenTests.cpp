#include "pch.h"

#include "OracleImage.h"

#include "ExtendedTokens.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::ExtendedTokenPrinter;
using Elite::Rng;
using Elite::TextSink;
using Elite::TokenPrinter;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The extended token printer against the shipped one (slices 1c-b and 1c-c-b).
 *
 * The trap is on CHPR rather than on DASC, and the difference is the whole of slice 1c-c-b.
 * DASC is no longer a boundary: it is the routine that decides whether a character goes to the
 * screen at all or into the line buffer to be justified, and it keeps the case state as it
 * goes. Trapping it would compare what the two sides intended to print rather than what they
 * printed, which is exactly the distinction the justification erases.
 *
 * Trapping CHPR instead means the shipped DASC runs for real, so the comparison covers the
 * buffer, the word wrap and the padding as well as the characters. It also means the trap has
 * to clear the carry the way CHPR does -- see Cpu6502::TrapExit, and the SBC four instructions
 * after the call that borrows because of it.
 *
 * Two things make this harder than the recursive suite. Some tokens print one of five
 * randomised alternatives, drawn from the game's own generator, so both sides start from the
 * same four bytes of generator state and a mismatch in how many times either draws shows up
 * immediately as diverging text. And three control codes -- 9, 11 and 21 -- reach the canvas,
 * so tokens containing one are counted and set aside rather than compared against a screen the
 * port does not have.
 */
namespace GameLogicTests
{

  namespace
  {

    class CapturingSink : public TextSink
    {
    public:
      void Put(std::uint8_t _character) override
      {
        characters.push_back(_character);
      }
      std::vector<std::uint8_t> characters;
    };

    class DeferredControls : public Elite::ControlCodes
    {
    public:
      void Run(std::uint8_t _code) override
      {
        reached = true;
        lastCode = _code;
      }
      bool reached = false;
      std::uint8_t lastCode = 0;
    };

    /// Value tokens reach commander state, which is phase 2's.
    class DeferredValues : public Elite::ValueTokens
    {
    public:
      void Print(std::uint8_t, TextSink&) override
      {
        reached = true;
      }
      bool reached = false;
    };

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

    std::wstring Describe(const std::vector<std::uint8_t>& _bytes)
    {
      std::wstring text = L"[";
      for (std::size_t index = 0; index < _bytes.size() && index < 48; ++index)
      {
        if (index != 0)
        {
          text += L' ';
        }
        text += std::to_wstring(_bytes[index]);
      }
      if (_bytes.size() > 48)
      {
        text += L" ...";
      }
      return text + L"]";
    }

    using GeneratorState = std::array<std::uint8_t, 4>;
    constexpr GeneratorState SEED = {0x5A, 0x4A, 0x02, 0x48};

    struct TextStateBytes
    {
      std::uint8_t lowerCaseBits = 0;
      std::uint8_t sentenceStart = 0;
      std::uint8_t toLineBuffer = 0;
      std::uint8_t justify = 0;
      std::uint8_t alwaysLower = 0;
      std::uint8_t caseMask = 0xFF;
    };

    /// Sets every one of the game's text-state bytes. These live inside a loaded code block rather
    /// than in zero page, so leaving one alone means comparing against whatever the binary happens
    /// to hold there instead of against the port's starting state.
    void SeedTextState(Cpu6502& _cpu, const OracleImage& _oracle, const TextStateBytes& _state)
    {
      _cpu.memory[_oracle.Label("DTW1")] = _state.lowerCaseBits;
      _cpu.memory[_oracle.Label("DTW2")] = _state.sentenceStart;
      _cpu.memory[_oracle.Label("DTW3")] = _state.toLineBuffer;
      _cpu.memory[_oracle.Label("DTW4")] = _state.justify;
      _cpu.memory[_oracle.Label("DTW5")] = 0;
      _cpu.memory[_oracle.Label("DTW6")] = _state.alwaysLower;
      _cpu.memory[_oracle.Label("DTW8")] = _state.caseMask;
      _cpu.memory[_oracle.Label("QQ17")] = 0;

      for (std::uint16_t index = 0; index < Elite::CharacterPrinter::BUFFER_SIZE; ++index)
      {
        _cpu.memory[static_cast<std::uint16_t>(_oracle.Label("BUF") + index)] = 0;
      }
    }

    /// The port's side of the same, wired the way the game wires it: the recursive printer's
    /// characters go to DASC, not to the screen, which is what lets a system name printed by TT27
    /// take part in the justification around it.
    struct PortPrinter
    {
      explicit PortPrinter(const TextStateBytes& _state, const GeneratorState& _seed = SEED)
        : characters(screen),
          recursive(characters, &values),
          printer(characters, recursive, rng, &controls)
      {
        rng.SetState(_seed);
        characters.state.lowerCaseBits = _state.lowerCaseBits;
        characters.state.sentenceStart = _state.sentenceStart;
        characters.state.toLineBuffer = _state.toLineBuffer;
        characters.state.justify = _state.justify;
        characters.state.alwaysLower = _state.alwaysLower;
        characters.state.caseMask = _state.caseMask;
      }

      CapturingSink screen;
      Rng rng;
      DeferredValues values;
      DeferredControls controls;
      Elite::CharacterPrinter characters;
      TokenPrinter recursive;
      ExtendedTokenPrinter printer;
    };

    /// Runs one extended token through the shipped printer and returns what it emitted.
    std::vector<std::uint8_t> RunShipped(std::uint8_t _token, const TextStateBytes& _state, const GeneratorState& _seed,
                                         bool& _outCompleted)
    {
      const OracleImage& oracle = OracleImage::Instance();

      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(oracle.Label("CHPR"), Cpu6502::TrapExit::ClearCarry);

      for (std::size_t index = 0; index < _seed.size(); ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("RAND") + index)] = _seed[index];
      }

      SeedTextState(cpu, oracle, _state);

      cpu.a = _token;
      cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      cpu.c = false;

      const auto run = cpu.CallSubroutine(oracle.Label("DETOK"), 500'000);
      _outCompleted = run.completed && !run.illegalOpcode;

      std::vector<std::uint8_t> characters;
      for (const auto& hit : cpu.trapHits)
      {
        characters.push_back(hit.a);
      }
      return characters;
    }

    /// Compares one token. Returns false when it reached a seam this slice does not cross.
    bool CompareToken(std::uint8_t _token, const TextStateBytes& _state, const GeneratorState& _seed = SEED)
    {
      PortPrinter port(_state, _seed);
      port.printer.Print(_token);

      if (port.controls.reached || port.values.reached)
      {
        return false;
      }

      bool completed = false;
      const std::vector<std::uint8_t> expected = RunShipped(_token, _state, _seed, completed);

      const std::wstring where = L" for extended token " + std::to_wstring(_token);
      Assert::IsTrue(completed, (L"the shipped printer should return" + where).c_str());

      if (port.screen.characters != expected)
      {
        Assert::Fail(
          (L"characters differ" + where + L"\n  game: " + Describe(expected) + L"\n  port: " + Describe(port.screen.characters)).c_str());
      }

      return true;
    }

    void CompareEveryToken(const TextStateBytes& _state, const char* _label)
    {
      std::uint32_t compared = 0;
      std::uint32_t deferred = 0;

      for (std::uint32_t token = 1; token < 256; ++token)
      {
        if (CompareToken(static_cast<std::uint8_t>(token), _state))
        {
          ++compared;
        }
        else
        {
          ++deferred;
        }
      }

      Logger::WriteMessage(
        (std::string(_label) + ": compared " + std::to_string(compared) + ", deferred " + std::to_string(deferred)).c_str());

      Assert::IsTrue(compared > 200, L"a substantial share of tokens should be pure text");
    }

    // ======================================================================================
    // DASC on its own
    // ======================================================================================

    /*
     * The token sweep above reaches DASC through the tokens the game happens to contain, which
     * leaves two of its branches untouched: the DTW4 bit 6 that suppresses the flush, and control
     * code 16's DTW7. Bit 6 is set by exactly one routine in the game -- the in-flight message
     * printer, which buffers a whole message so it can measure it and centre it by hand -- and that
     * routine is phase 5's. So the branch is ported now and cannot be exercised through anything
     * that exists yet, which is the case for a test that drives the routine directly.
     *
     * These feed one character at a time into both, keeping the shipped DASC's memory between calls
     * so its buffer accumulates the way the port's does, and compare the screen output AND the four
     * state bytes after every character. Comparing the state as well as the output is what makes
     * this stronger than the sweep: a buffer that filled differently but happened to print the same
     * would fail here.
     */
    struct DascComparison
    {
      std::vector<std::uint8_t> screen;
      std::uint8_t sentenceStart = 0;
      std::uint8_t bufferLength = 0;
      std::uint8_t caseMask = 0;
      std::vector<std::uint8_t> buffer;

      [[nodiscard]] bool operator==(const DascComparison&) const = default;
    };

    std::wstring DescribeState(const DascComparison& _state)
    {
      return L"DTW2=" + std::to_wstring(_state.sentenceStart) + L" DTW5=" + std::to_wstring(_state.bufferLength) + L" DTW8=" +
             std::to_wstring(_state.caseMask) + L" out=" + Describe(_state.screen) + L" buf=" + Describe(_state.buffer);
    }

    /// How much of BUF is worth comparing. The whole thing would drag the recursive token table in
    /// with it, and neither side ever writes that far.
    constexpr std::uint16_t BUFFER_COMPARED = 128;

    void CompareThroughDasc(const std::vector<std::uint8_t>& _text, const TextStateBytes& _state, const wchar_t* _label)
    {
      const OracleImage& oracle = OracleImage::Instance();

      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(oracle.Label("CHPR"), Cpu6502::TrapExit::ClearCarry);
      SeedTextState(cpu, oracle, _state);

      CapturingSink screen;
      Elite::CharacterPrinter characters(screen);
      characters.state.justify = _state.justify;
      characters.state.caseMask = _state.caseMask;
      characters.state.sentenceStart = _state.sentenceStart;

      for (std::size_t index = 0; index < _text.size(); ++index)
      {
        const std::uint8_t character = _text[index];

        cpu.ClearTrapHits();
        cpu.a = character;
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        const auto run = cpu.CallSubroutine(oracle.Label("DASC"), 500'000);

        const std::wstring where =
          std::wstring(_label) + L", character " + std::to_wstring(index) + L" (" + std::to_wstring(character) + L")";
        Assert::IsTrue(run.completed && !run.illegalOpcode, (L"the shipped DASC should return at " + where).c_str());

        DascComparison game;
        for (const auto& hit : cpu.trapHits)
        {
          game.screen.push_back(hit.a);
        }
        game.sentenceStart = cpu.memory[oracle.Label("DTW2")];
        game.bufferLength = cpu.memory[oracle.Label("DTW5")];
        game.caseMask = cpu.memory[oracle.Label("DTW8")];
        for (std::uint16_t offset = 0; offset < BUFFER_COMPARED; ++offset)
        {
          game.buffer.push_back(cpu.memory[static_cast<std::uint16_t>(oracle.Label("BUF") + offset)]);
        }

        screen.characters.clear();
        characters.Put(character);

        DascComparison port;
        port.screen = screen.characters;
        port.sentenceStart = characters.state.sentenceStart;
        port.bufferLength = characters.state.bufferLength;
        port.caseMask = characters.state.caseMask;
        for (std::uint16_t offset = 0; offset < BUFFER_COMPARED; ++offset)
        {
          port.buffer.push_back(characters.buffer[offset]);
        }

        if (game != port)
        {
          Assert::Fail((L"DASC differs at " + where + L"\n  game: " + DescribeState(game) + L"\n  port: " + DescribeState(port)).c_str());
        }
      }
    }

    std::vector<std::uint8_t> Text(const char* _text)
    {
      std::vector<std::uint8_t> bytes;
      for (const char* cursor = _text; *cursor != '\0'; ++cursor)
      {
        bytes.push_back(static_cast<std::uint8_t>(*cursor));
      }
      return bytes;
    }

  } // namespace

  TEST_CLASS(ExtendedTokenPrinterAgainstTheShippedGame)
  {
  public:
    TEST_METHOD(EveryTokenMatchesWithNoCaseFolding)
    {
      if (OracleMissing())
      {
        return;
      }
      CompareEveryToken(TextStateBytes{}, "plain");
    }

    /// The state the game is normally in while writing a description: lower case forced, with the
    /// bits that do it set in the mask.
    TEST_METHOD(EveryTokenMatchesInLowerCaseMode)
    {
      if (OracleMissing())
      {
        return;
      }
      TextStateBytes state;
      state.lowerCaseBits = 0x20;
      state.alwaysLower = 0x80;
      state.caseMask = 0xFF;
      CompareEveryToken(state, "lower case");
    }

    /// Sentence case: the first letter of a word stays capital.
    TEST_METHOD(EveryTokenMatchesInSentenceCase)
    {
      if (OracleMissing())
      {
        return;
      }
      TextStateBytes state;
      state.lowerCaseBits = 0x20;
      state.sentenceStart = 0x80;
      CompareEveryToken(state, "sentence case");
    }

    /*
     * 6502: DT3 and the JMTB jump table -- every control code, one at a time.
     *
     * The token sweeps only reach the codes the shipped tokens actually contain, and two of the
     * twenty-one are not among them. Code 16 prints DTW7, which is the operand byte of its own
     * LDA and is only ever changed by the disk catalogue; code 20 is a line movement nothing
     * uses. Both were ported from the binary and neither could be wrong in a way the sweeps would
     * notice, which is what this is for.
     *
     * It also pins the dispatch itself. The game's table is indexed from one and read as pairs of
     * bytes out of the two addresses BEFORE it, so an off-by-one there sends every code to its
     * neighbour's routine -- and most of the neighbours are plausible enough that only a
     * comparison would show it.
     */
    TEST_METHOD(EveryControlCodeMatchesTheShippedDispatch)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();

      /*
       * The codes the port cannot compare against the shipped routine.
       *
       * 9, 11 and 21 reach the canvas. 22, 24 and 26 wait for a key or read a typed line, so
       * running them in the oracle would spin until the instruction budget ran out; 25 prints a
       * token and then delays for a hundred frames. 27, 28, 30 and 31 print a token chosen by
       * GCNT or DISK, which is game state the printer does not hold.
       *
       * 21 is in this list and still has a ported half -- see REACHES_SEAM below. It is here
       * because CLYNS clears screen memory the port has no canvas for, not because its flags are
       * unported.
       */
      constexpr std::array<std::uint8_t, 11> DEFERRED = {9, 11, 21, 22, 24, 25, 26, 27, 28, 30, 31};

      /// 8, 21, 23 and 29 are split: the flags they set are text state and stay here, and only the
      /// cursor move or the screen clear is passed on. So they reach the seam AND are comparable
      /// -- except 21, whose screen half the port has no canvas for.
      constexpr std::array<std::uint8_t, 14> REACHES_SEAM = {8, 9, 11, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};

      /*
       * Two starting states, because one is not enough to see everything.
       *
       * Code 16 prints its character through DASC rather than DTS, which is invisible from a
       * state where DTS would not have changed it -- so the second state is the one the game is
       * in while it writes a description, with lower case forced.
       */
      TextStateBytes plain;
      plain.justify = 0x80;

      TextStateBytes lowered;
      lowered.justify = 0x80;
      lowered.lowerCaseBits = 0x20;
      lowered.alwaysLower = 0x80;

      std::uint32_t compared = 0;

      for (const TextStateBytes& start : {plain, lowered})
      {
        for (std::uint8_t code = 1; code <= 31; ++code)
        {
          const bool deferred = std::find(DEFERRED.begin(), DEFERRED.end(), code) != DEFERRED.end();
          const bool seam = std::find(REACHES_SEAM.begin(), REACHES_SEAM.end(), code) != REACHES_SEAM.end();

          PortPrinter port(start);
          port.printer.PrintByte(code);

          Assert::AreEqual(seam, port.controls.reached,
                           (L"control code " + std::to_wstring(code) + L" should" + (seam ? L"" : L" not") + L" reach the seam").c_str());
          if (deferred)
          {
            continue;
          }

          Cpu6502 cpu = oracle.Fresh();
          cpu.AddTrap(oracle.Label("CHPR"), Cpu6502::TrapExit::ClearCarry);
          for (std::size_t index = 0; index < SEED.size(); ++index)
          {
            cpu.memory[static_cast<std::uint16_t>(oracle.Label("RAND") + index)] = SEED[index];
          }
          SeedTextState(cpu, oracle, start);

          // 6502: DETOK2 pushes V and V+1 before dispatching and puts them back afterwards, so
          // the codes that expand a token of their own set them for themselves.
          cpu.a = code;
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          Assert::IsTrue(cpu.CallSubroutine(oracle.Label("DETOK2"), 500'000).completed,
                         (L"DETOK2 should return for control code " + std::to_wstring(code)).c_str());

          /*
           * The state is compared BEFORE the buffer is flushed. A form feed sets DTW2 and DTW8
           * on its way through DASC, so flushing first would hide every code that sets one of
           * them -- MT8 and MT19 among them.
           */
          const std::wstring where = L" for control code " + std::to_wstring(code) + L" from DTW1=" + std::to_wstring(start.lowerCaseBits);

          Assert::AreEqual<std::uint32_t>(cpu.memory[oracle.Label("DTW1")], port.characters.state.lowerCaseBits,
                                          (L"DTW1 differs" + where).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[oracle.Label("DTW2")], port.characters.state.sentenceStart,
                                          (L"DTW2 differs" + where).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[oracle.Label("DTW3")], port.characters.state.toLineBuffer,
                                          (L"DTW3 differs" + where).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[oracle.Label("DTW4")], port.characters.state.justify,
                                          (L"DTW4 differs" + where).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[oracle.Label("DTW5")], port.characters.state.bufferLength,
                                          (L"DTW5 differs" + where).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[oracle.Label("DTW6")], port.characters.state.alwaysLower,
                                          (L"DTW6 differs" + where).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[oracle.Label("DTW8")], port.characters.state.caseMask,
                                          (L"DTW8 differs" + where).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[oracle.Label("QQ17")], port.recursive.CaseFlags(), (L"QQ17 differs" + where).c_str());

          for (std::uint16_t offset = 0; offset < BUFFER_COMPARED; ++offset)
          {
            Assert::AreEqual<std::uint32_t>(cpu.memory[static_cast<std::uint16_t>(oracle.Label("BUF") + offset)],
                                            port.characters.buffer[offset],
                                            (L"BUF[" + std::to_wstring(offset) + L"] differs" + where).c_str());
          }

          // Now empty the buffer on both sides, so what the code produced is compared as text as
          // well as in the state it left behind.
          port.characters.Put(Elite::CharacterPrinter::FORM_FEED);

          cpu.a = Elite::CharacterPrinter::FORM_FEED;
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          Assert::IsTrue(cpu.CallSubroutine(oracle.Label("DASC"), 500'000).completed,
                         (L"the flush should return for control code " + std::to_wstring(code)).c_str());

          std::vector<std::uint8_t> expected;
          for (const auto& hit : cpu.trapHits)
          {
            expected.push_back(hit.a);
          }

          if (port.screen.characters != expected)
          {
            Assert::Fail(
              (L"output differs" + where + L"\n  game: " + Describe(expected) + L"\n  port: " + Describe(port.screen.characters)).c_str());
          }

          ++compared;
        }
      }

      Logger::WriteMessage(("JMTB: " + std::to_string(compared) + " control code expansions compared, " + std::to_string(DEFERRED.size()) +
                            " of 31 codes deferred")
                             .c_str());

      // Twenty of the thirty-one reachable codes are compared, in two case states each. The table
      // has a thirty-second entry and DETOK2 can never dispatch it: the test is CMP #32 / BCC, so
      // a byte of 32 is a space rather than a code.
      Assert::AreEqual<std::uint32_t>(40u, compared, L"twenty of the thirty-one codes stay inside the text system");
    }

    /*
     * 6502: MT18 -- a random pronounceable word, and the carry chain that decides which one.
     *
     * The tokens that reach this control code come out differently for every generator state, and
     * the second of its two draws is made with the carry the FIRST one left rather than with a
     * clear one. The sweeps above all run from a single fixed state, and would pass with that
     * coupling removed; this runs the tokens that reach MT18 across enough states to find the ones
     * where the carry is set.
     *
     * MT17, the other control code with arithmetic of its own, is not here. It prints the system
     * name and then reaches back into the justification buffer to see whether that name ended on
     * a vowel, so it cannot be exercised without a universe to name -- it is covered instead by
     * the description sweep in the universe suite, over all 2,048 systems.
     */
    TEST_METHOD(RandomWordsMatchAcrossGeneratorStates)
    {
      if (OracleMissing())
      {
        return;
      }

      // The tokens whose text contains control code 18, read out of the shipped table. Two more
      // do -- 73 and 191 -- and are left out because they reach MT17 as well.
      constexpr std::array<std::uint8_t, 6> TOKENS = {50, 71, 75, 119, 189, 190};

      std::uint32_t compared = 0;
      GeneratorState seed = SEED;

      /*
       * A hundred and twenty-eight states, which is more than it looks like it needs.
       *
       * The coupling this is here to catch is narrow: the carry only reaches bit 0 of the first
       * rotate, and that bit only reaches the drawn NUMBER when it happens to change a carry two
       * additions later. So most states produce the same word either way, and a handful do not.
       * Two dozen rounds found none of them.
       */
      for (std::uint32_t round = 0; round < 128; ++round)
      {
        // Any spread of states will do; this one walks somewhere different every round without
        // needing the generator itself, which is the thing under test.
        for (std::size_t index = 0; index < seed.size(); ++index)
        {
          seed[index] = static_cast<std::uint8_t>(seed[index] * 7u + round * 31u + index);
        }

        for (const std::uint8_t token : TOKENS)
        {
          if (CompareToken(token, TextStateBytes{}, seed))
          {
            ++compared;
          }
        }
      }

      Logger::WriteMessage(("MT18: " + std::to_string(compared) + " token expansions compared").c_str());
      Assert::AreEqual<std::uint32_t>(128u * static_cast<std::uint32_t>(TOKENS.size()), compared,
                                      L"every token that reaches MT18 should compare rather than defer");
    }

    /*
     * Every byte, with justification off. The interesting half is not the routing -- everything
     * goes to the screen -- but the two state bytes: DTW8 is put back to 255 by every character
     * including the ones that never see it, and DTW2 records the five that end a word.
     */
    TEST_METHOD(EveryByteThroughDascMatchesWithNoJustification)
    {
      if (OracleMissing())
      {
        return;
      }

      std::vector<std::uint8_t> everything;
      for (std::uint32_t byte = 0; byte < 256; ++byte)
      {
        everything.push_back(static_cast<std::uint8_t>(byte));
      }

      // The mask starts where MT19 leaves it rather than at 255, because the thing worth checking
      // is that DASC puts it back -- and a comparison that started at 255 would pass without it.
      TextStateBytes state;
      state.caseMask = 0xDF;
      state.sentenceStart = 0x80;
      CompareThroughDasc(everything, state, L"no justification");
    }

    /*
     * The justification itself, over lines chosen for the cases the padding has to get right.
     *
     * The last two are the ones that catch a plausible port. A line that already breaks on a
     * space at column 30 takes a different exit from one that has to be padded into shape, and a
     * line whose gaps are already double-width is where the rotating bit that chooses which gap
     * to widen actually shows -- pad the first space you find every time and the text still comes
     * out thirty columns wide, just wrong.
     */
    TEST_METHOD(JustifiedLinesMatchTheShippedRoutine)
    {
      if (OracleMissing())
      {
        return;
      }

      const std::vector<std::pair<const char*, const wchar_t*>> lines = {
        {"SHORT\x0c", L"shorter than a line"},
        {"\x0c", L"nothing at all"},
        {"THIS PLANET IS MOST NOTABLE FOR ITS EXOTIC GOAT SOUP\x0c", L"two lines"},
        {"A BB CCC DDDD EEEEE FFFFFF GGGGGGG HHHHHHHH IIIIIIIII JJJJJJJJJJ\x0c", L"widening gaps"},
        {"123456789 123456789 12345678 THEN SOME MORE WORDS AFTER THAT\x0c", L"a space at column 30"},
        {"ONE  TWO  THREE  FOUR  FIVE  SIX  SEVEN  EIGHT  NINE  TEN  ELEVEN\x0c", L"gaps already doubled"},
        {"ALPHA   BETA   GAMMA   DELTA   EPSILON   ZETA   ETA   THETA   IOTA\x0c", L"gaps already tripled"},
        {"AA    BB    CC    DDDDDDDDDDDDDDDD EEEE FFFF GGGG HHHH\x0c", L"a run of four spaces"},
        {"A. B: C. D: E. F: G. H: I. J: K. L: M. N: O. P: Q. R: S. T: U. V.\x0c", L"nothing but short words"},
        {"THIS PLANET IS MOST NOTABLE FOR ITS PARKING METERS BUT RAVAGED BY UNPREDICTABLE SOLAR "
         "ACTIVITY.\x0c",
         L"a whole description"},
      };

      for (const auto& line : lines)
      {
        TextStateBytes state;
        state.justify = 0x80;
        CompareThroughDasc(Text(line.first), state, line.second);
      }
    }

    /*
     * 6502: DTW4 bit 6, set only by the in-flight message printer.
     *
     * With it set a form feed is a character to be kept rather than an instruction to flush, so
     * the buffer simply fills and nothing reaches the screen at all. The routine that uses it is
     * phase 5's; this is what stops the branch being written once and never looked at again.
     */
    TEST_METHOD(TheMessageBufferNeverFlushesOnItsOwn)
    {
      if (OracleMissing())
      {
        return;
      }

      TextStateBytes state;
      state.justify = 0xC0;
      CompareThroughDasc(Text("DOCKED\x0cSTILL DOCKED, AND STILL NOT FLUSHED EVEN PAST THIRTY COLUMNS\x0c"), state, L"message buffer");
    }

    /// The per-system override table is walked by the same machinery over different bytes.
    TEST_METHOD(SystemOverridesMatch)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();

      std::uint32_t compared = 0;
      std::uint32_t comparedWithText = 0;

      // The override table holds 27 entries, and the last of them is not followed by a
      // terminator inside the table. That matters because the game's walker is not a bounded
      // lookup: it counts terminators with nothing to stop it, so reaching for the final entry
      // sends it off the end of the table and into whatever follows. The port bounds-checks and
      // returns instead, so past that point the two are not comparable -- one has stopped and the
      // other is still running.
      //
      // The 26 entries below it are bounded on both sides and are what this compares. In the game
      // the last entry is reached through a lookup that never asks for it in isolation.
      constexpr std::uint32_t OVERRIDE_ENTRY_COUNT = 26;

      for (std::uint32_t token = 1; token <= OVERRIDE_ENTRY_COUNT; ++token)
      {
        PortPrinter port{TextStateBytes{}};
        port.printer.PrintSystemOverride(static_cast<std::uint8_t>(token));

        if (port.controls.reached || port.values.reached)
        {
          continue;
        }

        Cpu6502 cpu = oracle.Fresh();
        cpu.AddTrap(oracle.Label("CHPR"), Cpu6502::TrapExit::ClearCarry);
        for (std::size_t index = 0; index < SEED.size(); ++index)
        {
          cpu.memory[static_cast<std::uint16_t>(oracle.Label("RAND") + index)] = SEED[index];
        }

        SeedTextState(cpu, oracle, TextStateBytes{});
        cpu.a = static_cast<std::uint8_t>(token);
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        Assert::IsTrue(cpu.CallSubroutine(oracle.Label("DETOK3"), 500'000).completed,
                       (L"the shipped walker should return for override " + std::to_wstring(token)).c_str());

        std::vector<std::uint8_t> expected;
        for (const auto& hit : cpu.trapHits)
        {
          expected.push_back(hit.a);
        }

        if (port.screen.characters != expected)
        {
          Assert::Fail((L"system override " + std::to_wstring(token) + L" differs\n  game: " + Describe(expected) + L"\n  port: " +
                        Describe(port.screen.characters))
                         .c_str());
        }
        ++compared;
        if (!port.screen.characters.empty())
        {
          ++comparedWithText;
        }
      }

      // Most override entries open with a nested token whose own text begins with a control
      // code, so the number that stay inside this slice is small. Counting how many produced
      // actual characters is what makes this assertion mean something: a port that returned
      // nothing everywhere would match an oracle that also emitted nothing, and pass.
      Logger::WriteMessage(
        ("system overrides compared: " + std::to_string(compared) + ", of which produced text: " + std::to_string(comparedWithText))
          .c_str());
      Assert::IsTrue(comparedWithText >= 1, L"at least one override must be compared with real text on both sides");
    }
  };

} // namespace GameLogicTests
