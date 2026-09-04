#pragma once

#include "Canvas.h"
#include "ExtendedTokens.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Universe.h"

#include <cstdint>
#include <span>

namespace Elite
{

/*
 * The two galactic charts (slice 2b).
 *
 * Elite draws its universe twice. The long-range chart is the whole galaxy: 256 dots at half
 * vertical scale, with a circle showing how far your fuel reaches. The short-range chart is the
 * neighbourhood at four times the magnification, with names beside the systems and each one
 * drawn as a disc whose size is its own seed bit.
 *
 * Neither chart stores anything. Both walk the same seed twist the universe generator uses, so
 * the chart IS the galaxy rather than a picture of it -- which is why every routine here takes
 * the galaxy's seeds and steps them 256 times.
 *
 * Two of the drawing calls are not here. The fuel circle is CIRCLE2 and each system's disc is
 * SUN, both of which keep a line heap so they can erase themselves, and that heap lands with
 * the flight model in slice 3c. They are seams below, and the tests compare the ARGUMENTS this
 * code hands them against the shipped routines -- so when 3c arrives the charts are complete
 * without anything here changing.
 */

/*
 * 6502: QQ9, QQ10, QQ0, QQ1, QQ11, QQ14 -- what a chart reads.
 *
 * The crosshairs and where you are are separate: the crosshairs are what you have selected and
 * the home position is where the ship is, and the difference between them is the jump you are
 * contemplating. Both are galactic coordinates, the same bytes the seeds carry.
 */
struct ChartView
{
  std::uint8_t cursorX = 0; ///< 6502: QQ9 -- the crosshairs' galactic x
  std::uint8_t cursorY = 0; ///< 6502: QQ10 -- and y
  std::uint8_t homeX = 0;   ///< 6502: QQ0 -- the current system's x
  std::uint8_t homeY = 0;   ///< 6502: QQ1 -- and y

  /// 6502: QQ11 -- which view is showing. Bit 7 set is the short-range chart, and nearly every
  /// routine here branches on it rather than taking a chart as an argument.
  std::uint8_t view = 0;

  /// 6502: QQ14 -- fuel, in light years times ten. It is the fuel circle's radius.
  std::uint8_t fuel = 0;
};

/// 6502: QQ19 -- a crosshair's centre and half-width.
struct Crosshairs
{
  std::uint8_t x = 0;    ///< 6502: QQ19
  std::uint8_t y = 0;    ///< 6502: QQ19+1
  std::uint8_t size = 0; ///< 6502: QQ19+2
};

/// 6502: what TT128 leaves for CIRCLE2 -- K3, K4, K and STP.
struct RangeCircle
{
  std::uint8_t x = 0;      ///< 6502: K3
  std::uint8_t y = 0;      ///< 6502: K4
  std::uint8_t radius = 0; ///< 6502: K
  std::uint8_t step = 0;   ///< 6502: STP -- how far round the circle each segment goes
};

/*
 * 6502: CIRCLE2 -- the fuel range circle, and SUN -- a system's disc on the short-range chart.
 *
 * Both draw by walking a line heap so that the next frame can erase exactly what the last one
 * drew, and that heap is the flight model's (slice 3c). Until it exists the charts hand their
 * arguments here instead, which is enough to compare them against the game.
 */
class ChartShapes
{
public:
  virtual ~ChartShapes() = default;

  /// 6502: TT128's tail -- JMP CIRCLE2.
  virtual void DrawRangeCircle(const RangeCircle& _circle) = 0;

  /// 6502: TT23's ee1 -- FLFLLS, SUN, FLFLLS. The radius is two or three, from a seed bit.
  virtual void DrawSystemDisc(std::uint8_t _x, std::uint8_t _y, std::uint8_t _radius) = 0;
};

/*
 * 6502: TT123 -- move one coordinate of the crosshairs by a signed step.
 *
 * A move that would run off either end of the galaxy is REFUSED rather than clamped: the
 * original tests the step's sign against the carry the addition produced, and puts the old
 * value back when they disagree. So holding a cursor key at the edge of the chart does nothing,
 * rather than sliding along it.
 */
[[nodiscard]] std::uint8_t StepCoordinate(std::uint8_t _value, std::uint8_t _step) noexcept;

/*
 * 6502: TT15 -- draw a crosshair as two lines through a point.
 *
 * Every edge saturates rather than wrapping, and the vertical stroke's bottom is clamped to 151
 * on the long-range chart only, because that chart has the fuel circle's legend under it. The
 * lines are drawn by LOIN, which plots by EOR, so drawing the same crosshair twice erases it --
 * that is how the cursor moves.
 */
void DrawCrosshairs(Canvas& _canvas, DrawWorkspace& _work, const Crosshairs& _at, std::uint8_t _view) noexcept;

/*
 * 6502: TT103 -- draw the crosshair at the selected system, on whichever chart is showing.
 *
 * On the long-range chart it is always drawn. On the short-range chart (TT105) it is drawn only
 * if the selection is close enough to be on screen, and neither range test is symmetric. Both
 * accept a difference below 38 going one way; going the other, x accepts down to -26 and y down
 * to -36. So the visible window is off-centre, by different amounts on each axis, and the two
 * constants are four instructions apart in the original.
 */
void DrawTargetCrosshairs(Canvas& _canvas, DrawWorkspace& _work, const ChartView& _view) noexcept;

/*
 * 6502: TT16 -- move the crosshairs and redraw them.
 *
 * Erase, move, draw. The move arrives as two signed steps and the original negates the vertical
 * one on the way in, because the keyboard's "down" is the screen's "up".
 */
void MoveCrosshairs(Canvas& _canvas, DrawWorkspace& _work, ChartView& _view, std::uint8_t _stepX,
                    std::uint8_t _stepY) noexcept;

/*
 * 6502: TT14 -- the circle showing how far the fuel reaches, and the crosshair at its centre.
 *
 * Two quite different circles share the routine. On the long-range chart it is centred on where
 * you are, at half vertical scale, with a radius of fuel/4. On the short-range chart it is
 * centred on the middle of the screen at four times the scale, with a radius of the fuel itself.
 */
void DrawFuelRange(Canvas& _canvas, DrawWorkspace& _work, const ChartView& _view, ChartShapes* _shapes) noexcept;

/*
 * 6502: NLIN2 and NLIN4 -- a rule right across the screen at a given row.
 *
 * Both charts draw one under their title. X2 is 255 rather than the screen width, so the line
 * runs into the right margin; that is the original's, not a rounding here.
 */
void DrawSeparator(Canvas& _canvas, DrawWorkspace& _work, std::uint8_t _y) noexcept;

/*
 * 6502: TT22 -- the long-range chart.
 *
 * The screen clear at the top of the routine is TT66, which resets the whole view and belongs
 * with the screen work; a caller does that first, and leaves the cursor where TT66 leaves it.
 * Everything after it is here: the title, the two rules, the fuel circle, the 256 dots and the
 * crosshairs.
 *
 * A system's brightness is its own seed byte: `ORA #%01010000` on QQ15+4 makes a value PIXEL
 * reads as a distance, so the chart's dots vary in size for no reason except what the galaxy
 * happens to contain.
 */
void DrawLongRangeChart(Canvas& _canvas, DrawWorkspace& _work, TokenPrinter& _printer, TextState& _text,
                        const ChartView& _view, const SystemSeeds& _galaxy, ChartShapes* _shapes) noexcept;

/*
 * 6502: TT23 -- the short-range chart.
 *
 * Every system within twenty of you across and thirty-eight up or down is drawn, with its name
 * beside it if there is a free character row within one of its own. The row bookkeeping is 25
 * bytes the original keeps in the SHIP workspace, XX1, purely as scratch -- it looks like a
 * dependency on the flight model and is not.
 *
 * A system whose row comes out below three is skipped entirely, name and disc together, because
 * the test that rejects it branches past both.
 */
void DrawShortRangeChart(Canvas& _canvas, DrawWorkspace& _work, TokenPrinter& _printer, TextState& _text,
                         const ChartView& _view, const SystemSeeds& _galaxy, ChartShapes* _shapes) noexcept;

/*
 * 6502: CLYNS -- clear the bottom two rows of the screen and put the cursor there.
 *
 * The chart routines reach it through `hm`, and it clears screen memory the way TT66 does, so it
 * is the same seam control code 21 already declares. The two flags it sets are text state and are
 * set by the caller rather than here.
 */
class ChartEffects
{
public:
  virtual ~ChartEffects() = default;
  virtual void ClearBottomRows() = 0;
};

/*
 * 6502: QQ12, QQ22, QQ8 and safehouse -- what choosing a hyperspace target reads and writes
 * besides the chart.
 *
 * None of this is the charts' own state: it is the commander's, and slice 2d owns where it lives.
 * It arrives here as a value for the same reason the market's does -- the arithmetic that reads
 * it is this slice's even though the bytes are not.
 */
struct JumpState
{
  std::uint8_t docked = 0;    ///< 6502: QQ12 -- non-zero while docked, and you cannot jump docked
  std::uint8_t countdown = 0; ///< 6502: QQ22+1 -- non-zero while a jump is already counting down
  std::uint16_t distance = 0; ///< 6502: QQ8 -- how far the selected system is, in tenths
  bool controlHeld = false;   ///< 6502: JSR CTRL / BMI Ghy -- the galactic hyperdrive's key

  /// 6502: safehouse -- the seeds of the system being jumped to, saved because the countdown
  /// runs while the player keeps moving the crosshairs.
  SystemSeeds target;
};

/// What `hyp` decided. The original says it by which routine it jumps to; this says it by name.
enum class JumpOutcome
{
  Docked,       ///< 6502: dockEd -- and the message has been printed
  Busy,         ///< a countdown is already running, or the view cannot select a system
  Galactic,     ///< 6502: Ghy -- CTRL was held, and that needs equipment state (slice 2d)
  AlreadyThere, ///< the crosshairs are on the system you are in, so there is nothing to jump to
  OutOfRange,   ///< 6502: TT147 -- too far or not enough fuel, and "RANGE?" has been printed
  CountingDown, ///< the name has been printed and the countdown has started
};

/*
 * 6502: TT147 -- "RANGE?", which is a token and a question mark.
 *
 * Reached both from `hyp` and from the equipment screen, which is why it is a routine of its own
 * for two instructions.
 */
void PrintRangeError(TokenPrinter& _printer) noexcept;

/*
 * 6502: ee3 -- print the hyperspace countdown at the top left.
 *
 * Three digits and no decimal point, at (1, 1). The cursor move is two calls that share their
 * argument: DOXC and DOYC are both handed the same 1, which is why the number sits in the corner
 * rather than anywhere the caller chose.
 */
void PrintCountdown(TextSink& _sink, TextState& _text, std::uint8_t _count) noexcept;

/*
 * 6502: hm -- put the crosshairs on the system nearest to where they are, and clear the message
 * rows underneath.
 *
 * Erase, search, redraw: the first TT103 rubs out the crosshair that is there, because LOIN
 * draws by EOR, and the second draws it at wherever TT111 settled.
 */
NearestSystem SelectNearestSystem(Canvas& _canvas, DrawWorkspace& _work, ChartView& _view,
                                  const SystemSeeds& _galaxy, ChartEffects* _effects) noexcept;

/*
 * 6502: hyp -- the hyperspace key, up to the point where the countdown starts.
 *
 * Everything after the countdown is arrival: `hyp1` copies the saved seeds over the commander's
 * system and rolls a new market, and `TT18` deducts the fuel and flies the tunnel. Those are the
 * commander's and the flight model's, so this stops where the countdown begins.
 *
 * The two range tests are not one test. A distance of 256 tenths or more fails on its HIGH byte
 * alone, before the fuel is looked at; only then is the low byte compared against the fuel. So a
 * system 25.6 light years away is out of range even with a full tank, and the message is the same
 * one you get for having no fuel.
 */
JumpOutcome RequestHyperspace(Canvas& _canvas, DrawWorkspace& _work, TokenPrinter& _printer,
                              ExtendedTokenPrinter& _extended, TextState& _text, ChartView& _view,
                              JumpState& _jump, const SystemSeeds& _galaxy, ChartEffects* _effects) noexcept;

/*
 * 6502: HME2's HME3 loop -- find a system by the name that was typed.
 *
 * The search is the justification buffer used as a scratch pad. Control code 14 turns buffering
 * on, `cpl` prints the system's name into BUF instead of onto the screen, and the typed name is
 * compared against it backwards. That is why this slice waited for the line buffer.
 *
 * It compares the typed characters with bit 5 FORCED ON and the buffer's as they are, so the
 * match depends on what case `cpl` printed in -- which is to say on QQ17, which this routine
 * does not set. See the tests: the game's own prompt leaves QQ17 in a state that makes the
 * first system of every galaxy unfindable, and the port reproduces that.
 *
 * Returns true when a system matched, and leaves the crosshairs on it.
 */
[[nodiscard]] bool FindSystemByName(TokenPrinter& _printer, CharacterPrinter& _characters, ChartView& _view,
                                    const SystemSeeds& _galaxy, std::span<const std::uint8_t> _typed) noexcept;

} // namespace Elite
