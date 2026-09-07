#pragma once

#include "Rng.h"
#include "Tokens.h"

#include <array>
#include <cstdint>

namespace Elite
{

  struct Universe; // Universe.h -- forward, because a control code that leaves is run against it
  struct Ports;    // Ports.h, likewise

  /*
   * 6502: DTW1 to DTW8 -- the state the extended printer carries between bytes.
   *
   * These are eight separate bytes in the original rather than a packed set of flags, and they
   * are kept separate here for the same reason: each is written independently by a different
   * control code, and folding them together would invent invariants the game does not have.
   *
   * DTW7 is not a variable at all in the game. It is the operand byte of the `LDA #'A'` that
   * opens MT16, so the routine that changes it is rewriting an instruction. The port gives it a
   * name because what the trick achieves is a value.
   */
  struct ExtendedTextState
  {
    std::uint8_t lowerCaseBits = 0; ///< 6502: DTW1 -- bits set into a letter to lower its case
    std::uint8_t sentenceStart = 0; ///< 6502: DTW2 -- set when the last character ended a word
    std::uint8_t toLineBuffer = 0;  ///< 6502: DTW3 -- send characters through the recursive printer
    std::uint8_t justify = 0;       ///< 6502: DTW4 -- bit 7 buffers the line, bit 6 never flushes it
    std::uint8_t bufferLength = 0;  ///< 6502: DTW5 -- how much of the line buffer is in use
    std::uint8_t alwaysLower = 0;   ///< 6502: DTW6 -- lower case regardless of the sentence state
    std::uint8_t literal = 'A';     ///< 6502: DTW7 -- the character control code 16 prints
    std::uint8_t caseMask = 0xFF;   ///< 6502: DTW8 -- bits cleared from the next letter, once
  };

  /*
   * 6502: MT15 -- `LDA #0 / STA DTW4 / ASL A / STA DTW5`. Stop justifying and throw the buffered
   * line away.
   *
   * It is a function as well as a control code because `MESS` calls it as a SUBROUTINE, in the
   * middle of using the justifier as a measuring device: justify with bit 6 set so nothing flushes,
   * print the token to find its width, then call this to turn the buffer off before printing it for
   * real. `MT14` shares its tail through an `EQUB &2C` -- that one in the usual place, unlike the
   * one in `MESS` (§6.66).
   */
  void StopJustifying(ExtendedTextState& _state) noexcept;

  /*
   * 6502: DASC, which the game also knows as TT26.
   *
   * Every printed character in Elite passes through here, from both text systems at once: the
   * recursive printer arrives by `JMP DASC` and the extended printer by DTS. What it decides is
   * where the character goes. Normally it goes straight to the screen. With DTW4 set it goes
   * into a ninety-byte line buffer instead, and a form feed then empties that buffer to the
   * screen thirty columns at a time -- widening the gaps between words until each line ends
   * exactly on a space. That is Elite's justified text, and it is why a system description reads
   * as a neat block rather than a ragged one.
   *
   * It is a TextSink because that is precisely what it is. The recursive printer is handed one of
   * these rather than the screen, exactly as the original hands it DASC, and that is what lets a
   * system name printed by TT27 take part in the justification of the sentence around it.
   */
  class CharacterPrinter : public TextSink
  {
  public:
    /*
     * 6502: BUF.
     *
     * The game gives this ninety bytes before the next variable begins, and its own text is
     * longer than that: a system description runs to a hundred characters or so, and the buffer
     * holds all of it until the form feed at the end. So the original spills into the ship
     * position tables that follow, which is harmless -- justification only ever runs while docked
     * and those tables are the flight model's. Sizing this at ninety would truncate every
     * description in the game.
     *
     * A hundred and eighty-four is where the spill would stop being harmless: that is the
     * distance from BUF to QQ18, the recursive token table, which the text system reads while it
     * is filling this. Nothing in the game comes close.
     */
    static constexpr std::size_t BUFFER_SIZE = 184;

    /// The column a justified line breaks at. Thirty characters, then a form feed.
    static constexpr std::uint8_t LINE_WIDTH = 30;

    /// 6502: character 12, which is a newline everywhere except inside the line buffer, where it
    /// is the instruction to empty it.
    static constexpr std::uint8_t FORM_FEED = 12;

    /// The bytes are `Universe::sentences` since M5-e-2b; the printer binds to them the way
    /// `TextPrinter` binds to `TextState`, so that the universe copies and the printer does not.
    CharacterPrinter(TextSink& _screen, ExtendedTextState& _state) noexcept
      : m_screen(_screen),
        m_state(_state)
    {
    }

    /// 6502: DTW1 to DTW8 -- the universe's bytes, which this printer works on (M5-e-2b).
    [[nodiscard]] ExtendedTextState& State() noexcept
    {
      return m_state;
    }
    [[nodiscard]] const ExtendedTextState& State() const noexcept
    {
      return m_state;
    }

    /// 6502: DASC -- route one character, and justify the buffered line when one is asked for.
    void Put(std::uint8_t _character) noexcept override;

    /// 6502: BUF -- the line being justified. Public because MT17 reaches into it.
    std::array<std::uint8_t, BUFFER_SIZE> buffer{};

  private:
    /// 6502: DA1 to DAL4 -- empty the buffer to the screen, thirty columns at a time.
    void Justify() noexcept;

    /// 6502: DAS1 -- print the first _count characters of the buffer.
    void Emit(std::uint8_t _count) noexcept;

    /*
     * 6502: DA11 through DAL3 -- widen one gap in the line, and say whether the line is ready.
     *
     * The rotating bit that chooses which gap lives in SC+1, the screen pointer's high byte,
     * borrowed for the purpose. It is passed by reference here for the same reason it is a
     * variable there: it carries from one gap to the next within a line.
     */
    /*
   * 6502: SC+1 -- the rotating bit that chooses which gap, and the answer beside it (M5-a-3).
   *
   * It was a `std::uint8_t&` out-parameter until then, which is P10's pattern: a bare byte
   * reference the caller has to remember to keep. It is one value carried out and back in, so the
   * routine returns it with the flag, exactly as M2-b did for the arithmetic kernel.
   */
  struct PadResult
  {
    bool broke;         ///< false is the port's give-up on a line with no gap at all
    std::uint8_t rotor; ///< 6502: SC+1, carried to the next gap on the same line
  };

  [[nodiscard]] PadResult PadToWidth(std::uint8_t _rotor) noexcept;

    TextSink& m_screen; ///< 6502: CHPR
    ExtendedTextState& m_state; ///< `Universe::sentences`, bound the way `TextPrinter` binds its `TextState`
  };

  /*
   * The control codes that leave the text system, and `ControlCodes` WAS THE SEAM UNTIL M3-b-4b.
   *
   * JMTB has THIRTY-ONE reachable entries, not the twenty-one the low ones suggest, and every one
   * of them is used by a token the game prints. Codes 22 to 31 are the mission briefings and the
   * disk menu: they wait for keys, spin the title ship, read a typed line, or print a token under
   * a game-state index. Nine and eleven reach the canvas.
   *
   * Codes 8, 21, 23 and 29 are SPLIT rather than passed on whole: the flags they set belong to the
   * text system and are set here, and only the cursor move or the screen clear leaves. A handler
   * for those four must not set those flags again, or it will set them twice.
   *
   * `Elite::RunControlCode` is the handler (`Missions.h`), and this printer reaches it through the
   * game rather than through an interface -- so a printer built without one ignores the codes that
   * leave, exactly as a printer built without the seam did.
   */

  /*
   * 6502: DETOK, DETOK2, DETOK3.
   *
   * The second and larger of Elite's two text systems. Where the recursive tokens are a
   * compression scheme, these are closer to a small interpreter: a byte can be a character, a
   * letter pair, another extended token, one of several randomised alternatives, or a control
   * code. That is how the game fits its mission briefings and system descriptions into a few
   * kilobytes and still has them read differently each time.
   */
  class ExtendedTokenPrinter
  {
  public:
    ExtendedTokenPrinter(CharacterPrinter& _characters, TokenPrinter& _recursive, Rng& _rng) noexcept
      : m_characters(_characters),
        m_recursive(_recursive),
        m_rng(_rng)
    {
    }

    /*
     * The game a control code that leaves the text system is run against (M3-b-4b).
     *
     * IT IS A SETTER AND HAS TO BE. This object is a member of `Ports`, and `RunControlCode` takes
     * a `Ports&` -- so the pair cannot be a constructor argument on either side without one of them
     * existing first. `TokenPrinter::SetValueTokens` unties the same knot for the same reason, and
     * `Main.cpp`'s composition already lends the struct back to two of the objects in it.
     *
     * A printer with no game ignores the codes that leave, which is what a null `ControlCodes*`
     * meant before it and is what the token suites are built on.
     */
    void SetGame(Universe& _universe, Ports& _ports) noexcept
    {
      m_universe = &_universe;
      m_ports = &_ports;
    }

    /*
     * How many codes have LEFT the text system, whether or not a game was there to run them.
     *
     * It is a counter and not a seam, and the difference is the point: `ControlCodes` was a virtual
     * whose only production implementation forwarded straight back into `GameLogic`, and this is a
     * `std::uint32_t` on an object the suites already own. What it preserves is the one thing the
     * seam gave the token suites that nothing else can -- `CompareToken` must SKIP a token that
     * reaches a code the port defers, because such a token cannot be compared against a game that
     * runs it, and no state comparison can tell "deferred" from "ran and did nothing".
     */
    [[nodiscard]] std::uint32_t CodesThatLeft() const noexcept
    {
      return m_codesThatLeft;
    }

    /// 6502: DETOK -- print extended token N from the main table.
    void Print(std::uint8_t _token) noexcept;

    /// 6502: DETOK3 -- the same, from the per-system override table.
    void PrintSystemOverride(std::uint8_t _token) noexcept;

    /// 6502: DETOK2 -- act on one byte of a token's text. Public because the walkers are not the
    /// only callers in the game.
    void PrintByte(std::uint8_t _byte) noexcept;

    /// 6502: DASC -- the routine this printer sends its characters to, and the one every other
    /// part of the game prints through as well. Callers that print a NUMBER rather than a token
    /// need it, because BPRNT ends there too.
    [[nodiscard]] CharacterPrinter& Characters() noexcept
    {
      return m_characters;
    }

    /// 6502: DTW1 to DTW8, and BUF. They live with DASC because that is the routine that reads
    /// and writes most of them.
    [[nodiscard]] ExtendedTextState& State() noexcept
    {
      return m_characters.State();
    }

  private:
    void Walk(const std::uint8_t* _table, std::size_t _size, std::uint8_t _token) noexcept;

    /// 6502: DTS -- one character, under the case state, then on to DASC.
    void PrintCharacter(std::uint8_t _character) noexcept;

    /// 6502: DT6 -- pick one of up to five alternatives and print that instead.
    void PrintRandomVariant(std::uint8_t _byte) noexcept;

    /// 6502: DT3 and the JMTB jump table -- one control code.
    /*
     * 6502: DT3 -- the text system's half of the dispatch.
     *
     * It is `RunTextCode` and not `RunControlCode` because `Elite::RunControlCode` is the game's
     * half and lives in `Missions.h`: this one sets the flags that belong to the printer and hands
     * on what does not, which is the split codes 8, 21, 23 and 29 make explicit.
     */
    void RunTextCode(std::uint8_t _code) noexcept;

    /// 6502: MT17 -- the current system's name, turned into an adjective.
    void PrintSystemAdjective() noexcept;

    /// 6502: MT18 -- a random pronounceable word, one to four letter pairs long.
    void PrintRandomWord() noexcept;

    CharacterPrinter& m_characters;
    TokenPrinter& m_recursive;
    Rng& m_rng;
    Universe* m_universe = nullptr; ///< set by `SetGame`; null is a printer that defers the codes
    Ports* m_ports = nullptr;
    std::uint32_t m_codesThatLeft = 0;
  };

} // namespace Elite
