#pragma once

#include "Canvas.h"
#include "TextPrint2x.h"
#include "ExtendedTokens.h"
#include "SoundEffects.h"
#include "Tokens.h"

#include "Colours.h"

#include <array>
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
  inline constexpr CellPalette TEXT_COLOUR_WHITE{Colour::White, Colour::Black};

  /// 6502: MAG2 -- purple for %01, black for %10. GNUM and MT26 switch to it while the player is
  /// typing and back to `TEXT_COLOUR_WHITE` when the line is done.
  inline constexpr CellPalette TEXT_COLOUR_PURPLE{Colour::Purple, Colour::Black};

  struct TextState
  {
    std::uint8_t column = 0; ///< 6502: XC
    std::uint8_t row = 0;    ///< 6502: YC

    /// 6502: QQ17 -- the capitalisation state. The token printer works on it and CHPR reads it for
    /// the value 255, which means "print nothing at all". ONE byte since M5-e-2c: the printer kept
    /// a copy until then and every store of QQ17 was two stores (§8).
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
    CellPalette palette{};
  };

  /*
   * 6502: DLY, de, MCH and messXC -- what an in-flight message needs between frames.
   *
   * `DLY` counts it down, `MCH` is the token on screen so it can be printed AGAIN to erase it, `de`
   * is one bit saying whether " DESTROYED" is appended, and `messXC` is the column the last message
   * started at. The message logic is `Messages.h`'s; the STRUCT is here because `CLYNS` clears the
   * first two, and a text routine that clears message state has to be able to name it (§6.67).
   */
  struct MessageState
  {
    std::uint8_t delay = 0;  ///< 6502: DLY
    std::uint8_t append = 0; ///< 6502: de
    std::uint8_t token = 0;  ///< 6502: MCH
    std::uint8_t column = 0; ///< 6502: messXC
  };

  /*
   * `TextEffects` WAS HERE AND IS NOT ANY MORE (M3-b-4a).
   *
   * It carried one method, `ClearScreen`, and the header said it was "the one thing CHPR does that
   * the library still cannot do for itself" because `TT66` reaches the dashboard, the sprites, the
   * border and the colour bands. THAT WAS THE WRONG ROUTINE. `clss` is `JSR TT66simp`, and
   * `TT66simp` is a bitmap wipe of rows 1 to 23 and a cursor home -- `ClearTextArea` above, ported
   * and compared against the shipped routine since slice 2a. It is §6.73 for the eleventh time and
   * the defect it was hiding is in §8.
   *
   * `Beep` WAS THE OTHER AND WENT IN M3-b-2b. Character 7 is `R5`, which is `JSR BEEP`, and `BEEP`
   * has been `Elite::Beep` over a `SoundBuffer` since slice 5a -- so the printer takes the buffer
   * and rings the bell itself. Every caller on this side drops the carry it answers with, which is
   * why the call discards it.
   */

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
  /// 6502: K -- the value, most significant byte first. A value since M2-c; `NumberWorkspace` held
  /// it and `U` until then.
  using NumberBytes = std::array<std::uint8_t, 4>;

  /*
   * 6502: BPRNT. `_withPoint` is the carry the entry points set, and it decides whether a decimal
   * point is printed at all -- pr6 clears it, pr5 leaves it as the caller had it. `_digits` is `U`,
   * how many digits fall after the decimal point.
   *
   * RETURNS WHAT IT LEAVES IN `U`. The routine rewrites the byte as it works (`LDA #11 / SEC / SBC U
   * / STA U / INC U`), and `SV1` prints the competition number with no `U` of its own -- so the
   * width that print gets is whatever the last `BPRNT` left, and `SaveScreen` carries the byte for
   * that one reader. `TT11` sets `U` before every other print, and the port's copies of those are
   * their own locals, which is a gap this comment names and M2-c did not close (§8, M2-c).
   */
  [[nodiscard]] std::uint8_t PrintNumber(TextSink& _sink, NumberBytes _value, std::uint8_t _digits, bool _withPoint) noexcept;

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
  void ResetCellColours(Canvas& _canvas, Picture* _picture = nullptr) noexcept;

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
   * one.
   *
   * THIS USED TO BE `CLYNS2` UNDER `CLYNS`'S NAME. `CLYNS` opens `LDA #0 / STA DLY / STA de` and
   * then falls into `CLYNS2`; the port implemented the second and called it the first, because when
   * it was written the message counters had nowhere to live. Nothing in the library calls `CLYNS2`
   * -- it is a label with no callers -- so every real caller wanted the two stores, and slice 3d-c
   * put them back (§6.67).
   *
   * `_layout` is here for the twin alone: which wide rows the message occupies is the screen's
   * layout, because a message over the space view sits at the height the original put it and one on
   * a text screen is packed with the rest (Resolution.md section 6.2). The faithful routine does not
   * read it, and the default is the space view's -- the screen `CLYNS` is called on most.
   */
  void ClearMessageRows(Canvas& _canvas, TokenPrinter& _printer, TextState& _text, ExtendedTextState& _extended,
                        MessageState& _message,
                        Picture* _picture = nullptr, TextLayout _layout = SPACE_VIEW_LAYOUT) noexcept;

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
    /*
     * The buffer is a POINTER and not a `Universe&`, because the printer is built over a canvas and
     * a `TextState` and nothing else, and half the tests that build one have no universe to hand.
     * A null buffer is a printer whose bell is not connected.
     *
     * THE SCREEN CLEAR IS NOT NULLABLE ANY MORE (M3-b-4a). It was, and a printer built without a
     * `TextEffects` dropped the character that overran row 23 instead of clearing and printing it
     * -- which was every test that built one. `clss` is `ClearTextArea` now and needs nothing this
     * object does not already hold.
     */
    TextPrinter(Canvas& _canvas, TextState& _state, SoundBuffer* _sound = nullptr) noexcept
      : m_canvas(_canvas),
        m_state(_state),
        m_sound(_sound)
    {
    }

    /*
     * The 640x400 surface this ALSO prints on, and the view whose layout says where
     * (Design/Resolution.md section 6, slice RS-1).
     *
     * POINTERS, as the two members are, and for the reason the sound buffer above is one: a printer
     * that has not been attached to a picture is a printer that draws the canvas alone, and that is
     * a state rather than a missing argument.
     *
     * ATTACHED RATHER THAN CONSTRUCTED, and nullable, for the reason the sound buffer above is: a
     * printer is built over a canvas and a `TextState`, and most of the fixtures that build one
     * have no universe to hand and are comparing the canvas anyway. A printer with nothing attached
     * draws the canvas alone, which is exactly what those fixtures assert.
     *
     * THE LAYOUT AND NOT THE VIEW SINCE RS-5-a, and the reason is that `QQ11` does not name a
     * screen: `STATUS` and `TT213` both call `TRADEMODE` with #8, so the status screen and the
     * inventory screen are one view and a layout read off the byte cannot tell them apart. The
     * layout lives in the universe beside the view and is written by `SetUpScreen` in the same
     * breath as the view, so this is still ONE fact read per glyph with nothing to keep in step.
     */
    void AttachPicture(Picture* _picture, const TextLayout* _layout) noexcept
    {
      m_picture = _picture;
      m_layout = _layout;
    }

    /// 6502: CHPR. Returns the character, as the routine does in A.
    std::uint8_t Print(std::uint8_t _character) noexcept;

    /*
     * The screen is where DASC sends a character it is not buffering, so this is a TextSink for
     * the same reason DASC is: `JMP CHPR` is the last instruction on that path. Handing one of
     * these to a CharacterPrinter wires the two text systems to the canvas exactly as the game
     * wires them.
     */
    void Put(std::uint8_t _character) noexcept override
    {
      (void)Print(_character);
    }

  private:
    /// 6502: RR1 onwards -- the printable path, which is the glyph and its cell colour.
    void PrintGlyph(std::uint8_t _character) noexcept;

    /// The layout for whatever screen is up, or the centred default when nothing is attached.
    [[nodiscard]] TextLayout Layout() const noexcept
    {
      return (m_layout != nullptr) ? *m_layout : CENTRED_LAYOUT;
    }

    Canvas& m_canvas;
    TextState& m_state;
    SoundBuffer* m_sound = nullptr; ///< 6502: what `R5`'s JSR BEEP fills

    Picture* m_picture = nullptr;        ///< the second surface, or none -- see `AttachPicture`
    const TextLayout* m_layout = nullptr; ///< the screen's layout, read and never written
  };

  /*
   * `KeySource` WAS HERE AND IS `Keyboard::NextKey` SINCE M3-b-3d.
   *
   * 6502: TT217 -- "scan the keyboard until a key is pressed". The game BLOCKS here, inside a
   * screen's own loop, and ADR-004 §1's problem is unchanged by the move: `GameLogic`'s input is
   * meant to be an `InputFrame`, which is a poll and not a wait, and the two cannot both be true of
   * the same code. Whoever resolves it decides how the seam is driven -- a pumped thread, a
   * coroutine, or rewriting the docked screens as state machines. The last of those stops being a
   * line-by-line port, which is the cost worth knowing before choosing it.
   */

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
