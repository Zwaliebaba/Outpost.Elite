#pragma once

#include "Canvas.h"
#include "ExtendedTokens.h"
#include "Tokens.h"

#include <cstdint>

namespace Elite
{

/*
 * The text cursor and the character printer (slice 1d-b).
 *
 * 6502: XC, YC, QQ17, COL2, K3 -- the zero-page bytes CHPR reads and leaves behind. XC and YC
 * are character cells, not pixels, and the printer advances them itself, so a caller that prints
 * two characters in a row does not touch them.
 */
/// 6502: the `&10` RES2 stores in COL2 -- colour 1 (white) for bitmap code %01 and colour 0
/// (black) for %10. The default text colour of every screen in the game.
inline constexpr std::uint8_t TEXT_COLOUR_WHITE = 0x10;

/// 6502: MAG2 -- purple for %01, black for %10. GNUM and MT26 switch to it while the player is
/// typing and back to `TEXT_COLOUR_WHITE` when the line is done.
inline constexpr std::uint8_t TEXT_COLOUR_PURPLE = 0x40;

struct TextState
{
  std::uint8_t column = 0; ///< 6502: XC
  std::uint8_t row = 0;    ///< 6502: YC

  /// 6502: QQ17 -- the capitalisation state the token printer owns. CHPR only reads it, and only
  /// to notice the value 255, which means "print nothing at all".
  std::uint8_t caseFlags = 0;

  /*
   * 6502: COL2 -- the palette byte written alongside every glyph.
   *
   * ZERO IS BLACK ON BLACK, and that is the shipped value: `COL2` is uninitialised memory, and
   * what puts `TEXT_COLOUR_WHITE` in it before anything prints is RES2, which the start sequence
   * reaches twice before the first title screen. So a caller that prints without running RES2
   * prints invisibly, exactly as the original would -- the default is left at zero rather than
   * "fixed" here so that a screen compared against the oracle starts from the same byte it does.
   */
  std::uint8_t cellColour = 0;
};

/*
 * The two things CHPR does that this slice cannot finish.
 *
 * Character 7 rings the bell, which is a sound event and belongs to phase 5; and a character
 * printed below the last row clears the screen and starts again, which needs TT66 and lands in
 * 1d-c. Both are seams rather than stubs -- the printer around them is complete -- and the tests
 * count how often they are reached rather than passing over them quietly.
 */
class TextEffects
{
public:
  virtual ~TextEffects() = default;

  /// 6502: R5 -- JSR BEEP.
  virtual void Beep() = 0;

  /// 6502: clss -- JSR TT66simp, then print the character again on the fresh screen.
  virtual void ClearScreen() = 0;
};

/*
 * 6502: BPRNT -- print a number, right-aligned in a fixed width, with an optional decimal point.
 *
 * The number is a 32-bit value in K, and the printer works by repeated subtraction of ten to the
 * eleventh from a 40-bit accumulator that it multiplies by ten between digits. Eleven digits
 * always, of which `digits` come after the point; leading zeros print as spaces until the first
 * significant digit, and everything narrower than the width is padded on the left.
 *
 * Characters go to a TextSink rather than to the canvas, for the reason slice 1c-a's printer
 * does: the original reaches DASC, which is the whole sentence-case machinery, and the number
 * printer's own behaviour is the sequence of characters it hands over.
 */
struct NumberWorkspace
{
  std::uint8_t k[4] = { 0, 0, 0, 0 }; ///< 6502: K -- the value, most significant byte first
  std::uint8_t u = 0;                 ///< 6502: U -- how many digits fall after the decimal point
};

/// 6502: BPRNT. `_withPoint` is the carry the entry points set, and it decides whether a decimal
/// point is printed at all -- pr6 clears it, pr5 leaves it as the caller had it.
void PrintNumber(TextSink& _sink, NumberWorkspace& _work, bool _withPoint) noexcept;

/// 6502: TT11 -- the same, for a sixteen-bit value, which is how nearly every caller reaches it.
void PrintValue(TextSink& _sink, std::uint16_t _value, std::uint8_t _digits, bool _withPoint) noexcept;

/// 6502: pr2 -- three digits, no decimal point, for a byte.
void PrintByteValue(TextSink& _sink, std::uint8_t _value, bool _withPoint) noexcept;

/*
 * 6502: TT66simp -- clear the text area and put the cursor back at (1, 1).
 *
 * It walks ylookup in steps of eight and zeroes 256 bytes from each character row's start, which
 * is exactly the 32 cells the space view and the text screens occupy -- the four-cell margins
 * either side are left alone. Rows 1 to 23 only; row 0 and the dashboard are not its business.
 */
void ClearTextArea(Canvas& _canvas, TextState& _state) noexcept;

/*
 * 6502: TTX66K's BOL3 / BOL4 -- the palette byte behind every character cell.
 *
 * THIS IS WHAT MAKES THE PICTURE VISIBLE, and it is separate from `ClearTextArea` for the same
 * reason it is separate on the hardware: TT66simp zeroes the BITMAP, and the bitmap holds two-bit
 * codes rather than colours. Code %01 takes the high nibble of the cell's byte in screen RAM and
 * %10 takes the low nibble, so a screen whose cells are still zero draws black on black no matter
 * what is in the bitmap -- every glyph, every line, the whole frame.
 *
 * `&10` is white for %01 and black for %10, which is the pair TTX66K fills 24 rows of 32 cells
 * with before it clears anything. CHPR overwrites the cells it prints into with `COL2`, so this
 * is what colours everything the text printer does NOT touch: the lines, the box, the chart.
 */
void ResetCellColours(Canvas& _canvas) noexcept;

/*
 * 6502: TT66, which falls into TTX66 -- the text state a screen change leaves behind.
 *
 * ONLY the text state. The rest of TTX66 is the ball line heap, the laser, the message delay and
 * `TTX66K` -- the dashboard, the sprites, the border box and the colour bands -- all of which is
 * flight state and phase 3's. So this is the half of the routine that GameLogic owns, and the
 * seam a screen is entered through (`TradeScreenEffects::ClearToView`) is this plus that.
 *
 * QQ17 IS WRITTEN TWICE AND THE SECOND ONE WINS. Near the top the routine does `LDA #128 / STA
 * QQ17 / STA DTW2`, and its LAST five bytes are `LDX #1 / STX XC / STX YC / DEX / STX QQ17` -- so
 * the state a caller sees is QQ17 = 0, ALL CAPS, while DTW2 keeps the 128. Reading the first
 * store and stopping there is an easy mistake to make and this port nearly made it: the upstream
 * source packs the routine across three numbered lines and the tail is on the third.
 * `TheScreenSeamsMatchTheShippedRoutines` runs the shipped TT66 and compares every byte of text
 * state against this, which is the only reason the question is settled rather than argued.
 */
void SetUpTextScreen(TokenPrinter& _printer, TextState& _text, ExtendedTextState& _extended) noexcept;

/*
 * 6502: CLYNS, which falls into CLYNS2 -- clear the bottom three text rows.
 *
 * The three rows are 21, 22 and 23, and the routine reaches them by address rather than through
 * `ylookup`: `SCBASE + &1A60` is character row 21 at the four-cell left margin, and `&140` is one
 * character row. So it clears the same 32 cells `ClearTextArea` does, three rows of them, and
 * leaves the cursor at column 1 of row 21 ready for the message that follows.
 *
 * DTW2 goes to 255 here and to 128 in TTX66 above, which is not a typo in either: 255 tells the
 * extended printer that no sentence is in progress, and the message CLYNS is clearing for starts
 * one. DLY and `de` are the message-delay counters and are flight state, so they are not here.
 */
void ClearMessageRows(Canvas& _canvas, TokenPrinter& _printer, TextState& _text,
                      ExtendedTextState& _extended) noexcept;

/// 6502: LDA #21 / STA YC -- the row CLYNS leaves the cursor on, which is the top of the three it
/// cleared and where every in-flight message and every "PRESS SPACE" prompt begins.
inline constexpr std::uint8_t MESSAGE_ROW = 21;

/*
 * 6502: TT26 / CHPR -- print one character at the cursor and advance it.
 *
 * Two entry points in the original share this body, and the control codes below 32 are handled
 * here rather than by the caller, so this is where a newline actually moves the cursor.
 */
class TextPrinter : public TextSink
{
public:
  TextPrinter(Canvas& _canvas, TextState& _state, TextEffects* _effects = nullptr) noexcept
    : m_canvas(_canvas)
    , m_state(_state)
    , m_effects(_effects)
  {
  }

  /// 6502: CHPR. Returns the character, as the routine does in A.
  std::uint8_t Print(std::uint8_t _character) noexcept;

  /*
   * The screen is where DASC sends a character it is not buffering, so this is a TextSink for
   * the same reason DASC is: `JMP CHPR` is the last instruction on that path. Handing one of
   * these to a CharacterPrinter wires the two text systems to the canvas exactly as the game
   * wires them.
   */
  void Put(std::uint8_t _character) noexcept override { (void)Print(_character); }

private:
  /// 6502: RR1 onwards -- the printable path, which is the glyph and its cell colour.
  void PrintGlyph(std::uint8_t _character) noexcept;

  Canvas& m_canvas;
  TextState& m_state;
  TextEffects* m_effects = nullptr;
};

/*
 * Where a blocking key read comes from.
 *
 * 6502: TT217 -- "scan the keyboard until a key is pressed". The game BLOCKS here, inside a
 * screen's own loop, and that is a genuine architectural problem for this port rather than a
 * detail: ADR-004 section 1 says GameLogic's input is an `InputFrame`, which is a poll and not a
 * wait. The two cannot both be true of the same code.
 *
 * Slice 2c did not resolve it, and deliberately: the routines that read the keyboard are ported
 * against this seam, exactly as the charts were ported against the seams for the drawing they
 * could not yet do. It lives here rather than in Market.h, where it started, because a second
 * slice needed it -- the commander's name entry -- and a seam two slices apart both reach for
 * belongs with the text layer rather than with the market. Whoever builds 2e decides how the seam is driven -- a pumped thread, a
 * coroutine, or rewriting the docked screens as state machines fed by `InputFrame`. The last of
 * those stops being a line-by-line port, which is the cost worth knowing before choosing it.
 */
class KeySource
{
public:
  virtual ~KeySource() = default;

  /// 6502: TT217 -- block until a key is pressed, and return its character.
  virtual std::uint8_t NextKey() = 0;
};

/*
 * The three token wrappers that also move the cursor.
 *
 * They are here rather than beside the others in Tokens.h because they need a TextState as well
 * as a TokenPrinter, and Tokens.h is the header TextPrint.h includes rather than the other way
 * round.
 */

/// 6502: INCYC -- INC YC. The whole routine.
void MoveCursorDown(TextState& _text) noexcept;

/*
 * 6502: TT60 -- and it is a chain of four routines, each falling into the next.
 *
 * TT60 (`JSR TT27`) falls into TTX69 (`JSR INCYC`), which falls into TT69 (set sentence case),
 * which falls into TT67 (print a newline). The assembled addresses are 27268, 27271, 27274 and
 * 27278 -- three bytes, three bytes, four bytes, with no RTS anywhere in them. So `JSR TT60`
 * prints a token, moves the cursor down a row, switches to sentence case AND prints a newline,
 * which is two vertical movements rather than one and is what puts the blank line under the
 * inventory screen's title.
 */
void PrintTitleLine(TokenPrinter& _printer, TextState& _text, std::uint8_t _token) noexcept;

/// 6502: TTX69 -- the same chain one link down, entered without a token. Moves the cursor to the
/// next row, sets sentence case and prints a newline, so it is TWO vertical movements.
void MoveDownAndNewline(TokenPrinter& _printer, TextState& _text) noexcept;

/// 6502: plf2 -- JSR plf / LDA #6 / JMP DOXC. A token, a newline, then indent to column six.
void PrintThenIndent(TokenPrinter& _printer, TextState& _text, std::uint8_t _token) noexcept;

} // namespace Elite
