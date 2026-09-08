#pragma once

#include "ScreenPresenter.h"
#include "Window.h"

#include "Canvas.h"
#include "Charts.h"
#include "ExtendedTokens.h"
#include "MarketScreen.h"
#include "Missions.h"
#include "NameEntry.h"
#include "PlanetDraw.h"
#include "StartUp.h"
#include "TextPrint.h"
#include "Tokens.h"

#include <chrono>
#include <cstdint>

namespace Elite
{
  class Game; // Game.h -- the shell drains its sound log
  struct SoundBuffer;
  struct MusicPlayer;
} // namespace Elite

namespace Outpost
{

  class FlightSession;
  class SoundOutput;

  /*
   * Everything the game reaches for outside `GameLogic`, answered once (slice 2e).
   *
   * SEVEN interfaces on one object, which is what ADR-004 says the executable is: each screen
   * declares what it needs separately and the shell answers all of it. `DockedSessionTests.cpp`
   * builds the same shape out of a null presenter and asserts that the declarations are mutually
   * consistent, so the arrangement here is verified before this file compiles.
   *
   * `WaitFrames` used to appear on two interfaces at once -- the line editor's and the start
   * sequence's -- and is `Presenter`'s alone since M3-b-3b. One definition
   * overrides both in each case, which is the language's own rule and is deliberate rather than
   * lucky: two independent statements of what a routine needs, satisfied by one thing.
   * `ResetMissileIndicators` was a third until M3-b-1e, which took `msblob` off both.
   *
   * WHAT IS HONESTLY MISSING, and it is said here rather than left to be discovered while playing.
   * Phase 4 owns the docking tunnel and the rotating title ship; phase 5 owns sound. Every method
   * whose body is a comment saying so is a routine that exists in the game and does not exist here
   * yet. Three that WERE such comments no longer are: `RESET`, `RES2` and `msblob` are ported, and
   * the shell forwards them to `FlightSession` rather than approximating them (§6.73).
   */
  class GameShell final : public Elite::Presenter, public Elite::Keyboard
  {
  public:
    GameShell(Window& _window, ScreenPresenter& _presenter) noexcept
      : m_window(_window),
        m_presenter(_presenter)
    {
    }

    /// The canvas this presents and the view byte it reads -- `Game`'s, attached once `Game` exists
    /// (M5-e-2), because `Game` needs this object at construction and owns the universe now.
    void AttachUniverse(Elite::Universe& _universe) noexcept
    {
      m_canvas = &_universe.canvas;
      m_picture = &_universe.picture;
      m_view = &_universe.view;
    }

    /*
     * `Attach` AND `AttachGalaxy` WERE HERE AND ARE NOT ANY MORE (M3-b-4b).
     *
     * The printer, the cursor, the sentence flags, the message counters and `GCNT` were five
     * pointers this object held for one method: `Run`, the control-code seam. `Elite::RunControlCode`
     * reaches all five through `(Universe&, Ports&)`, so the shell stopped needing any of them.
     */

    /*
     * One turn of the outer loop: dispatch what the window has, then draw and wait for the vertical
     * blank. Returns false once the window has closed.
     *
     * This is the whole of the shell's timing. There is no sleep and no timer anywhere in the
     * program: `Present` blocks on vsync, so a turn of this is a frame, and `WaitFrames` below is
     * literally the C64's `DELAY` -- a count of vertical syncs.
     */
    [[nodiscard]] bool Turn();

    /*
     * One FLIGHT-LOOP frame has been drawn: show it for as long as the shipped loop took to
     * compute the next one.
     *
     * NOT THE SAME THING AS `Present`, and the difference is five times over. `Present` is
     * `DELAY` with a count of one -- a single vertical sync, which is what the launch and
     * hyperspace tunnels ask for because the original spells that delay out inside them.
     * `DEATH`'s own loop -- run the flight loop, count down, go round -- asks for nothing of the
     * kind: it runs flat out and the VIC-II showed each frame for however long the next took
     * (§6.17 -- the C64 loop has no frame cap). At the measured 81,000-odd cycles a frame that is
     * about 12.7 a second, so paced by vsync the sixty-four frames of the death sequence took a
     * second instead of five and looked like a glitch rather than a death.
     *
     * The accumulator is the title screen's, for the same reason and with the same backlog rule:
     * a stall costs a frame rather than being repaid by running faster to catch up.
     */
    void HoldFlightFrame(std::uint8_t _ships) override;

    // ---- Elite::Keyboard -------------------------------------------------------------------------

    /// The matrix walk's read of one row, which is all of `RDKEY` that is the platform's
    /// since M3-b-3d -- `Elite::ScanKeyboard` is the rest.
    [[nodiscard]] bool Held(std::size_t _key) override;

    /*
     * Block until a key is pressed.
     *
     * The nested pump `Window.h` argues for. The window stays alive while the game waits, and the
     * canvas cannot be uploaded mid-mutation because the code that mutates it is the code that is
     * not running while this is.
     *
     * ON CLOSE IT DOES NOT RETURN. That is the price of a blocking seam and it is chosen rather
     * than stumbled into: `NextKey` has no error channel, and every value it could return is a
     * character some caller will act on -- zero is below the line editor's lowest accepted
     * character, so returning it would put `MT26` into an unbounded beep. Handing control back into
     * a routine that cannot be told the game is over is worse than not handing it back, so this
     * ends the process where it stands. Nothing is lost: the commander is on disk or it is not, and
     * a save is a menu item rather than a shutdown hook.
     */
    std::uint8_t NextKey() override;

    /*
     * `TradeScreenEffects` AND `ChartEffects` WERE ANSWERED HERE AND ARE NOT ANY MORE (M3-b-3b).
     *
     * `SetUpTradeScreen` was `ClearToView` and `FlushKeyboard`; `ClearToView` was
     * `Elite::SetUpScreen`; `ClearBottomRows` was `Elite::ClearMessageRows`; `BeepAndPause` was
     * `Elite::Beep` and `WaitFrames`. Four seams, and every one of them a forwarding call.
     *
     * `ClearToView` SURVIVES AS A PRIVATE HELPER, because `Run(9)` and this file's own screen
     * changes need it before the composition root has lent the shell its ports.
     */

    /// `Elite::SetUpScreen` once the ports are lent, and the view byte alone before
    /// then. Public because `Main.cpp` changes screens through it.
    void ClearToView(std::uint8_t _view);

    /// Empty the keyboard buffer.
    void Flush() override;

    // `Elite::StartUpEffects` WAS ANSWERED HERE AND IS NOT ANY MORE (M6-0-h-2): `ShowTitleScreen`
    // was a forward to `Elite::ShowTitleShip`, which `BR1` calls itself now.

    // ---- Elite::Presenter's four ------------------------------------------------------------------

    void WaitFrames(std::uint8_t _frames) override;

    /// The vertical sync the VIC-II was giving `HFS2` for free while it drew the next circle.
    void Present() override;

    /// The title screen's spin, held on its own cost curve -- see `Presenter.h`, and §6.110 for the
    /// 165 Hz panel that span the ship twenty times too fast when this was a plain present.
    void HoldTitleFrame(std::uint8_t _distance) override;

    /*
     * `Elite::ControlCodes` WAS ANSWERED HERE AND IS NOT ANY MORE (M3-b-4b).
     *
     * `Run` dispatched codes 8, 9 and 21 and forwarded the rest to `Elite::MissionCodes`. All three
     * were `GameLogic` reached through the executable -- two cursor stores and `ClearMessageRows` --
     * so `Elite::RunControlCode` is the whole dispatch now and the extended printer reaches it
     * directly.
     */

    /// QQ11 -- which screen is showing. See `m_view`: the byte is `Game`'s universe's, because
    /// the flight half writes it too.
    [[nodiscard]] std::uint8_t View() const noexcept
    {
      return *m_view;
    }

    /// The extended token printer, for the control codes that print. Set by the composition root
    /// after construction, because the printer needs this object to exist first.

    /*
     * The flight universe, for `RESET`, `RES2` and the raster handler.
     *
     * The start sequence reaches both resets through this object and both of them are ported now,
     * so what was a stub is a forward (§6.73 again: a seam scoped before the thing behind it
     * existed). `QQ12` comes with them because `RESET` writes it, and it belongs to the composition
     * root rather than to either half -- the docked dispatch reads it on every key.
     */
    void AttachFlight(FlightSession& _flight) noexcept
    {
      m_flight = &_flight;
    }

    /*
     * The VIC-II sprite registers the presenter composites from (ADR-005 §1).
     *
     * Attached separately from the flight session although it comes from it, because what this
     * object needs is the DATA and not the session: a shell handed the registers can present them
     * on a docked screen, where there is no flight loop running and the laser sights are still
     * switched off by `NOSPRITES` rather than by nobody having asked.
     */
    void AttachVideo(const Elite::VideoState& _video) noexcept
    {
      m_video = &_video;
    }

    /// The seams, which the composition root owns and this object is four of. Everything the shell
    /// forwards into `GameLogic` takes them beside the universe since M3-a.
    void AttachPorts(Elite::Ports& _ports) noexcept
    {
      m_ports = &_ports;
    }

    /// The SID and what feeds it. Set by the composition root, like the flight, because the sound
    /// buffer and the music player are the game's and the output is the platform's, and this object
    /// is where the two halves of the loop meet.
    void AttachSound(SoundOutput& _audio, Elite::SoundBuffer& _sound, Elite::MusicPlayer& _music, Elite::Game& _game) noexcept
    {
      m_audio = &_audio;
      m_sound = &_sound;
      m_music = &_music;
      m_game = &_game; // whose `Sounds()` the pump drains, since M5-e-1
    }

  private:
    /// Ends the process. See `NextKey`.
    [[noreturn]] void Abandon();

    Window& m_window;
    ScreenPresenter& m_presenter;
    Elite::Canvas* m_canvas = nullptr; ///< attached by `AttachUniverse`

    /// The 640x400 picture this presents, and the canvas above is what it presents it FROM until
    /// every region draws itself (Resolution.md §3.3). Both, because neither is the other's copy.
    Elite::Picture* m_picture = nullptr;

    /// The sprite registers, null until the composition root attaches them.
    const Elite::VideoState* m_video = nullptr;

    FlightSession* m_flight = nullptr;
    Elite::Ports* m_ports = nullptr;

    SoundOutput* m_audio = nullptr;
    Elite::SoundBuffer* m_sound = nullptr;
    Elite::MusicPlayer* m_music = nullptr;
    Elite::Game* m_game = nullptr;

    /*
     * QQ11 -- which screen is showing, and it is a REFERENCE because both halves write it.
     *
     * The shell owned the byte while `TT66` was the only writer, and slice 3d-d-iii-b gave the
     * flight loop `ChangeView`, `TT110` and the whole of `FlightScreen`, all of which write the
     * same address. Two copies would have agreed until the first launch.
     */
    std::uint8_t* m_view = nullptr; ///< attached by `AttachUniverse`

    /*
     * What paces the title screen's ship, and it is a CLOCK because the thing being paced is not
     * frames (`Presentation.h`, `TitleTurnSeconds`).
     *
     * `TLL2` has no `WSCAN` in it, so a turn of the ship costs what `MVEIT` and `LL9` cost and the
     * rate is a consequence. Presenting once per turn ties it to the display instead, which is
     * twenty times too fast on a 165 Hz panel; these two carry the accumulator that decouples
     * them, the same arrangement `Main.cpp` gives the flight loop and for the same reason.
     */
    std::chrono::steady_clock::time_point m_lastSpin{};
    double m_spinLeftover = 0.0;

    /// The same pair again for `HoldFlightFrame`, kept apart from the title's so that a death
    /// does not inherit whatever backlog the title screen had left over.
    std::chrono::steady_clock::time_point m_lastFlightFrame{};
    double m_flightFrameLeftover = 0.0;
  };

} // namespace Outpost
