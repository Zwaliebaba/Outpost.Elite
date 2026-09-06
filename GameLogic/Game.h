#pragma once

#include "Charts.h"
#include "Controls.h"
#include "DockedKeys.h"
#include "Docking.h"
#include "ExtendedTokens.h"
#include "FlightLoop.h"
#include "Hyperspace.h"
#include "PauseScreen.h"
#include "Ports.h"
#include "StateTokens.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Universe.h"

#include <cstdint>

namespace Elite
{

  /*
   * The whole game, and the executable is a window around it (Modernize.md §2.1, slice M3-c).
   *
   * ADR-004 §1 drew this seam "from day one" and it did not exist: `Outpost/Main.cpp` held the
   * universe, the text system, the ports, eight bytes of game state that are in no struct, and
   * every dispatch the main loop makes -- `TT102`'s actions, `M%`'s outcomes, `FREEZE`, the docked
   * pass and the flight pass. Eight hundred lines of 6502 in a file no Linux runner compiles, which
   * is R15's whole surface and the reason `check_outpost.py` has grown seven halves.
   *
   * WHAT IS LEFT IN THE EXECUTABLE IS WHAT CANNOT BE HERE. The determinism guard forbids this
   * library a clock, a float, a file and a Win32 call (AGENTS.md §5), and that is exactly the line
   * the split falls on: the window, the swap chain, the audio device and the SECONDS are the
   * executable's, and everything the game does with a key is this object's.
   *
   * SO THE STEPS ARE COUNTED OUTSIDE AND TAKEN INSIDE. `Advance` used to do both, over a `double`
   * accumulator -- and the plan's row said it would move here, which it cannot: ADR-005 §3's
   * accumulator is floating point by construction. `Step`, `StepDocked` and `StepPaused` each take
   * ONE key and run ONE pass, and how many passes a wall-clock second is worth stays where the
   * clock is. That is `Step(InputFrame)` as §2.1 wrote it, arrived at from the other direction.
   *
   * THE PLATFORM ARRIVES AS SEVEN REFERENCES AND A `ControlEffects`, which is `Ports` minus the
   * four members that are this library's own: the token printer, the character printer, the sink
   * and the extended printer are built HERE, over the universe, because nothing about them is the
   * platform's. `ControlEffects` is separate because it is not in `Ports` -- `DOCKIT` is passed
   * beside it (§4.5) -- and putting it in would push `aggregate-refs` up, which rule 5 forbids.
   */
  class Game
  {
  public:
    Game(Universe& _universe, ShipDrawEffects& _drawing, SpawnChildEffects& _spawning, SidWriteLog& _sid, StartUpEffects& _start,
         Presenter& _present, Keyboard& _keyboard, CommanderStore& _store, ControlEffects& _controls) noexcept;

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    /*
     * 6502: the loader's parts 5 and 6, then `NA%`, then `TT170` -- the cold start, end to end.
     *
     * It ends by pressing "8" for the player and PERFORMING that outcome rather than deciding it
     * again: `BAY` forces the key and `TT102` has already dispatched it, so deciding twice would
     * work today and stop working the moment the dispatch depends on something the first decision
     * changed.
     */
    void Reset() noexcept;

    /*
     * 6502: TT100 -- one pass of the flight half, with the key the window had (zero for none).
     *
     * Answers whether the caller may step again. False is `M%` leaving the flight half or the
     * pause key freezing it, both of which the loop this came from expressed as a `return` out of
     * the whole batch of steps -- so the boolean is that `return`, and a caller that ignored it
     * would run a docked pass through the flight loop.
     */
    [[nodiscard]] bool Step(std::uint8_t _key) noexcept;

    /// 6502: MLOOP's tail on a docked pass -- the countdowns, `TT17` and `TT102`, once.
    void StepDocked(std::uint8_t _key) noexcept;

    /// 6502: FREEZE -- one pass of the pause loop, which is one key.
    void StepPaused(std::uint8_t _key) noexcept;

    /// 6502: QQ12 -- which half of the main loop the game is in.
    [[nodiscard]] bool Docked() const noexcept
    {
      return m_universe.dockedFlag != 0u;
    }

    /// 6502: DK4's `CPX #&40` -- see `m_paused`. The executable asks because a frozen game is
    /// frozen in both halves and the test has to be above them.
    [[nodiscard]] bool Paused() const noexcept
    {
      return m_paused;
    }

    /*
     * 6502: FRIN's occupied slots -- how many ships the bubble holds.
     *
     * The executable asks because the cost of a flight frame depends on it and the cost is what the
     * accumulator counts against (§6.114). It is counted here rather than there because `FRIN`'s
     * zero-terminated list is game state.
     */
    [[nodiscard]] std::uint8_t ShipsInBubble() const noexcept;

    /// Every byte of game state, for the composition root, the replay and the suites.
    [[nodiscard]] Universe& State() noexcept
    {
      return m_universe;
    }
    [[nodiscard]] const Universe& State() const noexcept
    {
      return m_universe;
    }

    /// The seams, for a caller that has to reach one directly -- the loader screen on start-up and
    /// the suites that drive a routine rather than a pass.
    [[nodiscard]] Ports& PortsOf() noexcept
    {
      return m_ports;
    }

  private:
    // ---- the argument lists three routines want, gathered where the bytes live ------------------
    [[nodiscard]] ChartView ChartOf();
    [[nodiscard]] OptionBlock OptionsOf();
    [[nodiscard]] JumpState JumpOf();

    void DrawChart();
    void ShowChart(std::uint8_t _view);

    /*
     * 6502: FRCE -- the main loop entered with a key already "pressed".
     *
     * Declared ahead of `Perform` because `BAY2` forces one, and `BAY2` is reached from inside two
     * of the actions `Perform` performs. The recursion is one level deep and cannot be more: the
     * key it forces is f9, and the Inventory screen forces nothing.
     */
    void PressKey(std::uint8_t _key);

    /*
     * One key, and whatever screen it reaches.
     *
     * 6502: what `TT102` does with the label it chose. The dispatch itself is `ActionForKey`, which
     * is compared against the shipped routine over 16,384 states; this is the other half, and the
     * actions that need phase 4 are refused rather than silently ignored -- a game that did nothing
     * for the hyperspace key would look exactly like one that had wired it up.
     */
    void Perform(const KeyOutcome& _outcome);

    /// 6502: the six exits `DOENTRY` can take, which are the missions plus the bay itself.
    [[nodiscard]] ForcedKey MissionOf(DockingOutcome _outcome);

    /// 6502: what `M%` answers with, and what the loop does about it.
    void Leave(LoopOutcome _outcome);

    // ---- the universe, the text system and the seams --------------------------------------------

    /*
     * Every byte of game state, in one place (§4.4, slice M3-a) -- and a REFERENCE rather than a
     * member, which §2.1 does not ask for and this slice cannot yet give it.
     *
     * `Outpost::FlightSession` binds the universe at construction and answers three seams this
     * object's `Ports` needs, so the two cannot both own it: whichever is built first needs the
     * other. Two of those three seams are already scheduled to go -- `ShipDrawEffects` when the
     * emulator models the banking §6.108 found, `SpawnChildEffects` in M4-a -- and what is left of
     * that class afterwards is `DOCKIT`. When it goes, this becomes a member and the composition
     * root stops holding any game state at all. Named here rather than discovered later (§8).
     */
    Universe& m_universe;

    /*
     * The printers, which are NOT in the universe: `Universe` copies and has no vtable, which is
     * the property the replay hash is built on, and these are objects with virtual calls in them.
     * They travel in `Ports` instead, which is where the library's own four members of it are.
     */
    TextPrinter m_screen;
    CharacterPrinter m_characters;
    TokenPrinter m_recursive;
    StateTokens m_values;
    ExtendedTokenPrinter m_extended;

    /// 6502: JSR DOCKIT -- not in `Ports`, so it arrives on its own and is held here.
    ControlEffects& m_controls;

    /*
     * 6502: what `TT17` leaves in X and Y -- the crosshair steps, held between the scan and the
     * dispatch that uses them.
     *
     * On the 6502 they are registers and the two routines are consecutive; here `TT102`'s work is
     * a function call away, so they have to live somewhere. This is that somewhere, and both halves
     * of the loop write it.
     */
    CrosshairStep m_crosshairStep;

    /*
     * 6502: safehouse -- the seeds of the system the countdown is running towards.
     *
     * Separate from `selectedSeeds` (`QQ15`) because the player keeps moving the crosshairs while
     * the countdown runs, and `TT18` arrives at what was chosen when the key was pressed rather
     * than at whatever is under the crosshairs when it expires. `QQ8` is here for the same reason:
     * `hyp` measures the distance once and `TT18` spends that much fuel.
     */
    SystemSeeds m_jumpTarget{};
    std::uint16_t m_jumpDistance = 0;

    /*
     * 6502: DK4's `CPX #&40 / BNE DK2` -- and the frozen state it leaves behind.
     *
     * The original does not have this byte: it FREEZES, in a loop that reads the keyboard and does
     * not return until CLR/HOME. A windowed program cannot stop pumping messages, so the freeze is
     * a state the outer loop is in rather than a loop inside it -- which is the same trade
     * `PlanSteps` makes for the frame rate (ADR-005 §3).
     */
    bool m_paused = false;

    /*
     * 6502: JSTGY and JSTE -- two of the thirteen that NOTHING ELSE IN THE PORT READS.
     *
     * They are the joystick's y-inversion and its enable, and the flight controls read `JSTK` for
     * both. They are here because `DKS3` walks a contiguous run and the run is thirteen long: a
     * port that left them out would shift every option after them by two, and the "D" key would
     * switch the music instead of the disk.
     */
    std::uint8_t m_joystickGeometry = 0;
    std::uint8_t m_joystickEnabled = 0;

    /// 6502: MUTOKOLD -- what `MUTOKCH` saw last, which is how it notices the switch moving.
    std::uint8_t m_musicSwitchWas = 0;

    /// 6502: DNOIZ -- non-zero disables the sound, and the pause screen stores the KEY CODE in it.
    std::uint8_t m_soundDisabled = 0;

    /// LAST, because every reference in it is bound at construction (§4.5).
    Ports m_ports;
  };

} // namespace Elite
