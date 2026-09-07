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
#include "SoundEffects.h"
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
   * THE PLATFORM ARRIVES AS FOUR REFERENCES, which is `Ports` minus the four members that are this
   * library's own: the token printer, the character printer, the sink and the extended printer are
   * built HERE, over the universe, because nothing about them is the platform's. A `ControlEffects`
   * arrived beside them until M6-0-h-3 -- 6502: JSR DOCKIT, which was never in `Ports` (§4.5) and
   * is a library call now.
   */
  class Game
  {
  public:
    Game(Presenter& _present, Keyboard& _keyboard, CommanderStore& _store) noexcept;

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

    /*
     * 6502: what `M%` answered on the last `Step`, for a caller that needs more than "may I step
     * again".
     *
     * The M0-c replay records the outcome of every pass and compares the record digest for digest,
     * so it wants the answer rather than the decision taken from it. `Continued` until the first
     * `Step`, which is what the loop is in before it has run.
     */
    [[nodiscard]] LoopOutcome LastOutcome() const noexcept
    {
      return m_lastOutcome;
    }

    /*
     * 6502: QQ12, and the one state the original does not have (M4-d).
     *
     * `FRCE` is `LDA QQ12 / BEQ P%+5 / JMP MLOOP / JMP TT100` -- a two-way dispatch on a byte the
     * game keeps -- so two of these three values are the game's own. `Paused` is the third and it
     * is the PORT's: `FREEZE` is a loop that reads the keyboard and does not return, and a windowed
     * program cannot stop pumping messages, so the freeze is a state the outer loop is in rather
     * than a loop inside it (ADR-005 §3 makes the same trade for the frame rate).
     *
     * IT IS ONE ANSWER BECAUSE THE THREE ARE ORDERED. A frozen game is frozen in BOTH halves, so
     * the pause test has to come above the `QQ12` test; the executable did that with two calls and
     * a comment explaining the order, which is a rule a caller could get wrong. One value cannot be.
     */
    enum class Mode : std::uint8_t
    {
      Flight, ///< 6502: QQ12 = 0 -- `FRCE`'s `JMP TT100`
      Docked, ///< 6502: QQ12 non-zero -- `FRCE`'s `JMP MLOOP`
      Paused, ///< 6502: DK4's `CPX #&40` freeze, which is a state here and a loop there
    };

    [[nodiscard]] Mode ModeNow() const noexcept
    {
      if (m_paused)
      {
        return Mode::Paused;
      }
      return (m_universe.dockedFlag != 0u) ? Mode::Docked : Mode::Flight;
    }

    /// 6502: QQ12 -- which half of the main loop the game is in, for a caller that wants the byte
    /// rather than the state. `ModeNow` is what the loop should ask.
    [[nodiscard]] bool Docked() const noexcept
    {
      return m_universe.dockedFlag != 0u;
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
    /*
     * 6502: SID -- the register writes the game side has made since the executable last drained
     * them, in the order it made them (§4.4's `Sounds()`, built M5-e-1).
     *
     * THE LOG IS THE GAME'S NOW AND THE EXECUTABLE READS IT. Until M5-e-1 the executable owned the
     * log and handed the game a reference to write into -- the one place the app reached INTO
     * library state rather than being handed a value. `Ports::sid` still binds to it, because the
     * library's routines take the port; what changed is who owns the bytes behind the reference.
     * It answers the whole log rather than §4.4's sketched span because the log carries its own
     * `dropped` count and the executable's `Apply` takes a log; a span would lose the count.
     */
    [[nodiscard]] const SidWriteLog& Sounds() const noexcept
    {
      return m_sid;
    }

    /// The executable has applied what `Sounds()` answered; the next frame's writes start clean.
    void ClearSounds() noexcept
    {
      m_sid.Clear();
    }

    /// §4.4's `StateHash`: every byte of game state, library-native (`HashState`, M5-e-3).
    [[nodiscard]] std::uint64_t StateHash() const noexcept;

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
     * Every byte of game state, in one place (§4.4, slice M3-a) -- and a MEMBER since M5-e-2, which
     * is what §2.1 asked for and M3-c could not give it.
     *
     * What stopped it was the executable's two sessions binding the universe at CONSTRUCTION while
     * this object needed both of them at its own: whichever was built first needed the other, so the
     * composition root held the universe and lent it to all three. M5-e-2 had the sessions take it
     * afterwards instead -- `AttachUniverse`, the same shape as the four `Attach`es `GameShell`
     * already had -- and the cycle was never in the library at all. The composition root holds no
     * game state now; `State()` is where the executable reads it and the test ports too.
     */
    Universe m_universe;

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

    /*
     * THE SEVEN BYTES THAT WERE HERE ARE IN `Universe` SINCE THE ADR-007 §3 FOLLOW-ON.
     *
     * `crosshairStep`, `jumpTarget`, `jumpDistance`, `joystickGeometry`, `joystickEnabled`,
     * `musicSwitchWas` and `soundDisabled` -- `TT17`'s X and Y, `safehouse`, `QQ8`, `JSTGY`,
     * `JSTE`, `MUTOKOLD` and `DNOIZ`. Every one has a 6502 name, which makes it game state, which
     * §4.4's rule puts in `Universe`. They were here because M3-c carried them across from
     * `Main.cpp`'s composition struct, not because anything decided they belonged.
     *
     * SIX, SINCE M5-a-5: `soundDisabled` was a SECOND `DNOIZ` beside `SoundBuffer::soundOff`, so
     * the pause screen wrote a byte `NOISE` never read. It is gone and the pause screen writes the
     * one the sound system reads.
     */

    /*
     * 6502: DK4's `CPX #&40 / BNE DK2` -- and the frozen state it leaves behind.
     *
     * The one of the eight that STAYS. The original does not have this byte: it FREEZES, in a loop
     * that reads the keyboard and does not return until CLR/HOME. A windowed program cannot stop
     * pumping messages, so the freeze is a state the outer loop is in rather than a loop inside it
     * -- which is the same trade `PlanSteps` makes for the frame rate (ADR-005 §3), and a port
     * decision with no 6502 byte behind it.
     */
    bool m_paused = false;

    /// What `LastOutcome` answers. It is the return value of a call held, not game state, and it is
    /// not in `Universe` for that reason -- which is the rule the seven bytes above obey from the
    /// other side.
    LoopOutcome m_lastOutcome = LoopOutcome::Continued;

    /// LAST, because every reference in it is bound at construction (§4.5).
    SidWriteLog m_sid; ///< 6502: SID -- the game side's writes, declared before the `Ports` that binds to it
    Ports m_ports;
  };

} // namespace Elite
