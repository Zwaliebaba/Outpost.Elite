#include "pch.h"

#include "FlightPort.h"
#include "NullSeams.h"

#include "Controls.h"
#include "MemoryMap.h"
#include "Universe.h"
#include "VideoState.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The input path, pinned at what it answers today (Design/InputTimer.md slice I-6).
 *
 * THIS FILE IS NOT A COMPARISON AND NOTHING IN IT IS EVIDENCE ABOUT THE C64. Every other suite that
 * carried this name was: `ControlsTests.cpp` set the same bytes on both machines, ran `DOKEY` and
 * `TT17` on each and compared, and it was deleted with the oracle at M6-b-5 along with 336 other
 * comparisons. **The numbers below are the port's own answers, read out of the port and written
 * down**, and they are worth exactly what a characterisation test is worth: they cannot say the
 * port is right, only that it has stopped being what it was.
 *
 * WHY THAT IS WORTH HAVING ANYWAY, and why InputTimer.md added this slice on 2026-09-09 rather than
 * leaving the gap. I-2 rewrites this path -- `InputFrame`, scan codes, a layered key map -- and its
 * acceptance criterion was written as "`ControlsTests` for `ScanKeyboard` unchanged on the space
 * view". That names a file that no longer exists, so the slice's gate was void: I-2 could have
 * changed any of these four answers and nothing in the suite would have moved. The replay digest
 * does not help here either, because the scripted flight drives the ship through `FlightPort` and
 * never presses a key through `ScanKeyboard` at all.
 *
 * SO THE TABLE IS THE POINT, not the assertions around it. Each row is a matrix state and the four
 * routines' answers to it. A deliberate one-byte change to any of the four moves a row, which is
 * the property I-6's acceptance asks for and the only one this file can offer.
 */
namespace GameLogicTests
{

  namespace
  {
    /// A keyboard whose held set the test writes. `NullSeams` holds nothing and cannot script a
    /// matrix; everything else it answers is inherited unchanged.
    struct HeldKeys final : NullSeams
    {
      std::vector<std::size_t> down;

      bool Held(std::size_t _key) override
      {
        for (const std::size_t key : down)
        {
          if (key == _key)
          {
            return true;
          }
        }
        return false;
      }
    };

    /*
     * A hand at the keyboard, moved between scans by the PRESENTER.
     *
     * `ReadKey` blocks: a matrix that never presses loops for ever waiting for one, and a matrix
     * that never releases loops for ever in the other loop. So the script has to change between
     * sweeps, and the only seam that runs between them is the presenter -- `ReadKey` calls
     * `WaitFrames` once per outer pass and `Present` once per inner one, which is exactly the
     * "one vertical sync a scan" `Controls.h` describes as the port's sampling rate.
     *
     * So this attaches where `FlightPort` already offers to be watched, and writes the next entry
     * of the script into the port's own `held` array. Nothing about the routine under test is
     * touched, and nothing here knows how many keys a sweep reads -- a number I-2 will change.
     */
    struct AHandAtTheKeyboard final : Elite::Presenter
    {
      FlightPort* port = nullptr;
      std::vector<std::vector<std::size_t>> sweeps;
      std::size_t at = 0;

      void Advance()
      {
        ++at;
        port->held = {};
        const std::vector<std::size_t>& now = sweeps[(at < sweeps.size()) ? at : (sweeps.size() - 1u)];
        for (const std::size_t key : now)
        {
          port->held[key] = 0xFFu;
        }
      }

      void WaitFrames(std::uint8_t) override
      {
        Advance();
      }
      void Present() override
      {
        Advance();
      }
      void HoldFlightFrame(std::uint8_t) override {}
      void HoldTitleFrame(std::uint8_t) override {}
    };

    /// What `KEY_TRANSLATION` turns the fire key into. Recorded from the port rather than derived:
    /// the table is `Controls.cpp`'s own and this file is a characterisation (I-6).
    constexpr std::uint8_t RECORDED_FIRE_CHARACTER = 0x41; // "A"

    /// `ZEKTRAN` leaves a held key at 255 and everything else at zero -- the shape `ScanKeyboard`
    /// writes and the two readers below consume.
    Elite::KeyLogger LoggerWith(std::initializer_list<std::size_t> _held)
    {
      Elite::KeyLogger keys{};
      for (const std::size_t key : _held)
      {
        keys[key] = 0xFFu;
      }
      return keys;
    }
  } // namespace

  TEST_CLASS(TheInputPath)
  {
  public:
    /*
     * `ScanKeyboard` answers the LOWEST numbered key held, and says so with a carry.
     *
     * The walk counts down and stores `thiskey` on every hit, so two keys held answer the smaller
     * number -- which is not the first one found and not the most recently pressed. Pinned because
     * I-2 replaces the walk with a layered map over scan codes, and "which key wins" is precisely
     * the kind of answer a rewrite changes without noticing.
     */
    TEST_METHOD(ScanKeyboardAnswersTheLowestKeyHeld)
    {
      auto universe = std::make_unique<Elite::Universe>();
      HeldKeys keyboard;

      const auto scan = [&](std::initializer_list<std::size_t> _down) {
        keyboard.down.assign(_down);
        universe->keys = Elite::KeyLogger{};
        return Elite::ScanKeyboard(universe->keys, universe->video, universe->memoryMap, 0u, keyboard);
      };

      const Elite::TitleKey nothing = scan({});
      Assert::IsFalse(nothing.pressed, L"an empty matrix reported a press");
      Assert::AreEqual<std::uint8_t>(0u, nothing.key, L"an empty matrix named a key");

      const Elite::TitleKey one = scan({Elite::KEY_FIRE});
      Assert::IsTrue(one.pressed, L"a held key reported no press");
      Assert::AreEqual<std::uint8_t>(static_cast<std::uint8_t>(Elite::KEY_FIRE), one.key, L"the wrong key came back");

      // Fire is 54 and roll-left is 17, so the LOWER number wins however they are listed.
      const Elite::TitleKey two = scan({Elite::KEY_FIRE, Elite::KEY_ROLL_LEFT});
      Assert::AreEqual<std::uint8_t>(static_cast<std::uint8_t>(Elite::KEY_ROLL_LEFT), two.key,
                                     L"two keys held did not answer the lowest numbered one");
      const Elite::TitleKey reversed = scan({Elite::KEY_ROLL_LEFT, Elite::KEY_FIRE});
      Assert::AreEqual<std::uint8_t>(two.key, reversed.key, L"the answer depended on the order the keys were listed");
    }

    /// And it fills the logger the way `ZEKTRAN` does: cleared, then DECREMENTED, so a held key
    /// reads 255 and an untouched one zero. Everything downstream tests for non-zero, and a scan
    /// that stored rather than cleared would leave the autopilot's synthetic presses standing.
    TEST_METHOD(ScanKeyboardDecrementsRatherThanStores)
    {
      auto universe = std::make_unique<Elite::Universe>();
      HeldKeys keyboard;
      keyboard.down = {Elite::KEY_PITCH_UP};

      // A stale entry the scan must clear, and it is not one of the held keys.
      universe->keys = LoggerWith({Elite::KEY_SPEED_UP});
      static_cast<void>(Elite::ScanKeyboard(universe->keys, universe->video, universe->memoryMap, 0u, keyboard));

      Assert::AreEqual<std::uint8_t>(0xFFu, universe->keys[Elite::KEY_PITCH_UP], L"a held key did not read 255");
      Assert::AreEqual<std::uint8_t>(0u, universe->keys[Elite::KEY_SPEED_UP], L"a stale entry survived the scan");
    }

    /*
     * `ReadCrosshairKeys` -- the y axis is inverted and the x axis is not.
     *
     * Both axes start at 1 and become 255 when a SHIFT is held; then the y one is exclusive-ored
     * with &FE, which turns 1 into 255 and 255 into 1. So an unshifted cursor key moves y NEGATIVE
     * and a shifted one moves it positive, the opposite way round from x. That asymmetry is the
     * single most rewrite-fragile fact in this file.
     */
    TEST_METHOD(TheCrosshairYAxisIsInvertedAndXIsNot)
    {
      const auto step = [](std::initializer_list<std::size_t> _held) { return Elite::ReadCrosshairKeys(LoggerWith(_held)); };

      const Elite::CrosshairStep idle = step({});
      Assert::AreEqual<std::uint8_t>(0u, idle.x, L"x moved with nothing held");
      Assert::AreEqual<std::uint8_t>(0u, idle.y, L"y moved with nothing held");

      const Elite::CrosshairStep across = step({Elite::KEY_CURSOR_X});
      Assert::AreEqual<std::uint8_t>(1u, across.x, L"the unshifted x cursor key did not step +1");

      const Elite::CrosshairStep shiftedAcross = step({Elite::KEY_CURSOR_X, Elite::KEY_SHIFT_LEFT});
      Assert::AreEqual<std::uint8_t>(0xFFu, shiftedAcross.x, L"the shifted x cursor key did not step -1");

      const Elite::CrosshairStep down = step({Elite::KEY_CURSOR_Y});
      Assert::AreEqual<std::uint8_t>(0xFFu, down.y, L"the unshifted y cursor key did not step -1 -- the EOR is the point");

      const Elite::CrosshairStep shiftedDown = step({Elite::KEY_CURSOR_Y, Elite::KEY_SHIFT_LEFT});
      Assert::AreEqual<std::uint8_t>(1u, shiftedDown.y, L"the shifted y cursor key did not step +1");

      // Either SHIFT does it: the two entries are ORed in, not tested in turn.
      const Elite::CrosshairStep rightShift = step({Elite::KEY_CURSOR_X, Elite::KEY_SHIFT_RIGHT});
      Assert::AreEqual<std::uint8_t>(shiftedAcross.x, rightShift.x, L"the right SHIFT did not do what the left one does");
    }

    /// RETURN makes the step four times as big, on both axes and in both directions.
    TEST_METHOD(ReturnMakesTheCrosshairStepFourTimesAsBig)
    {
      const auto step = [](std::initializer_list<std::size_t> _held) { return Elite::ReadCrosshairKeys(LoggerWith(_held)); };

      const Elite::CrosshairStep fast = step({Elite::KEY_CURSOR_X, Elite::KEY_CROSSHAIR_FAST});
      Assert::AreEqual<std::uint8_t>(4u, fast.x, L"RETURN did not multiply the x step by four");

      const Elite::CrosshairStep fastDown = step({Elite::KEY_CURSOR_Y, Elite::KEY_CROSSHAIR_FAST});
      Assert::AreEqual<std::uint8_t>(static_cast<std::uint8_t>(0u - 4u), fastDown.y, L"RETURN did not multiply the y step by four");

      const Elite::CrosshairStep fastShifted = step({Elite::KEY_CURSOR_X, Elite::KEY_SHIFT_LEFT, Elite::KEY_CROSSHAIR_FAST});
      Assert::AreEqual<std::uint8_t>(static_cast<std::uint8_t>(0u - 4u), fastShifted.x, L"RETURN did not multiply a negative x step");

      // RETURN alone moves nothing: it scales a step rather than being one.
      const Elite::CrosshairStep alone = step({Elite::KEY_CROSSHAIR_FAST});
      Assert::AreEqual<std::uint8_t>(0u, alone.x, L"RETURN alone moved x");
      Assert::AreEqual<std::uint8_t>(0u, alone.y, L"RETURN alone moved y");
    }

    /*
     * `ReadFlightControls` -- the rates the flight keys leave behind.
     *
     * `DOKEY` bumps a rate by `CONTROL_STEP` while its key is held and damps it back towards centre
     * when it is not. What is pinned here is the FIRST pass off centre in each direction, because
     * that is the number a rewrite of the key map would move, and the damping's direction, because
     * that is what I-4's joystick finding was about.
     */
    TEST_METHOD(TheFlightKeysMoveTheRatesTheWayTheyDoToday)
    {
      /*
       * The keys go on the PORT'S MATRIX and not into `universe.keys`, because `ReadFlightControls`
       * opens with `RDKEY` -- a scan that clears the logger and refills it from the keyboard. A
       * logger written by hand here would be wiped before the first control was read, which is
       * exactly what the first draft of this test did, and it failed saying "rolling left moved no
       * rate" while rolling left worked perfectly.
       */
      /*
       * The rates are `JSTX` and `JSTY` -- `ControlState` -- and NOT `FlightState`'s `rollRate`.
       * `FlightState`'s pair is derived from these later in the frame, so a test that read it here
       * would see nothing move however hard the keys were held. The first draft did exactly that.
       */
      struct Rates
      {
        std::uint8_t roll = 0;
        std::uint8_t pitch = 0;
        std::uint8_t speed = 0;
      };

      const auto rates = [](std::initializer_list<std::size_t> _held) {
        auto port = std::make_unique<FlightPort>();
        for (const std::size_t key : _held)
        {
          port->held[key] = 0xFFu;
        }
        port->universe.flight.speed = 10u;
        Elite::ReadFlightControls(port->universe, port->Ports());
        return Rates{port->universe.control.roll, port->universe.control.pitch, port->universe.flight.speed};
      };

      const Rates idle = rates({});
      const Rates left = rates({Elite::KEY_ROLL_LEFT});
      const Rates right = rates({Elite::KEY_ROLL_RIGHT});
      Assert::IsTrue(left.roll != idle.roll, L"rolling left moved no rate");
      Assert::IsTrue(right.roll != idle.roll, L"rolling right moved no rate");
      Assert::IsTrue(left.roll != right.roll, L"the two roll keys did the same thing");

      // One step off centre in each direction, which is `CONTROL_STEP` and its negation.
      Assert::AreEqual<std::uint8_t>(static_cast<std::uint8_t>(idle.roll + Elite::CONTROL_STEP), left.roll,
                                     L"rolling left did not bump the rate by one control step");
      Assert::AreEqual<std::uint8_t>(static_cast<std::uint8_t>(idle.roll - Elite::CONTROL_STEP), right.roll,
                                     L"rolling right did not reduce the rate by one control step");

      const Rates up = rates({Elite::KEY_PITCH_UP});
      const Rates down = rates({Elite::KEY_PITCH_DOWN});
      Assert::IsTrue(up.pitch != idle.pitch, L"pitching up moved no rate");
      Assert::IsTrue(up.pitch != down.pitch, L"the two pitch keys did the same thing");

      // And the pitch keys are the other way round from the roll's, which is the asymmetry
      // `DK15` spells out and a rewrite would tidy away.
      Assert::AreEqual<std::uint8_t>(static_cast<std::uint8_t>(idle.pitch - Elite::CONTROL_STEP), up.pitch,
                                     L"pitching up did not REDUCE the rate -- the axes are not symmetric");

      /*
       * AND THE SPEED KEYS DO NOTHING HERE, which is a boundary worth pinning rather than an
       * omission. Space and "?" are read by the FLIGHT LOOP's part 3 (`MA17`), not by this
       * routine: `ReadFlightControls` leaves them in the logger and part 3 acts on them, with the
       * 40 cap and the "one is the floor" rule that live there. So a rewrite that moved the speed
       * into the key reader would be moving a decision across a seam, and this says so.
       */
      Assert::AreEqual<std::uint8_t>(10u, rates({Elite::KEY_SPEED_UP}).speed, L"the key reader changed the speed; part 3 owns that");
      Assert::AreEqual<std::uint8_t>(10u, rates({Elite::KEY_SLOW_DOWN}).speed, L"the key reader changed the speed; part 3 owns that");
      Assert::AreEqual<std::uint8_t>(10u, idle.speed, L"the speed moved with no key held");
    }

    /*
     * `ReadKey` is RELEASE-THEN-PRESS, and that is the whole of what I-1 bought.
     *
     * The routine waits until nothing is held, and only then waits for a press. A key already down
     * when the prompt appears is not the answer -- which is the fix for the defect InputTimer.md §0
     * describes, where a key held a little too long was delivered to the next screen as a press the
     * original would never have seen, and a screen answered itself.
     *
     * IT CANNOT BE TESTED WITH A STILL KEYBOARD. `ReadKey` blocks; a matrix that never presses
     * loops for ever and a matrix that never releases loops for ever in the other loop. So the
     * keyboard below plays a script, and the script is the test: held, released, pressed again.
     */
    TEST_METHOD(ReadKeyWaitsForTheKeyToBeReleasedFirst)
    {
      auto port = std::make_unique<FlightPort>();
      AHandAtTheKeyboard hand;
      hand.port = port.get();
      hand.sweeps = {
        {Elite::KEY_FIRE}, // already down when the prompt appears -- not the answer
        {},                // released, which is what lets the wait for a press begin
        {},                // nothing yet
        {Elite::KEY_FIRE}, // and now a real press
      };
      port->held[Elite::KEY_FIRE] = 0xFFu; // the script's first entry, before the first sweep
      port->watching = &hand;

      const std::uint8_t answer = Elite::ReadKey(port->universe, port->Ports());

      Assert::IsTrue(hand.at >= 3u, L"ReadKey answered before the key was released and pressed again");
      Assert::AreEqual<std::uint8_t>(RECORDED_FIRE_CHARACTER, answer,
                                     L"the translated character moved; if that is deliberate, record the new one here");
    }
  };

} // namespace GameLogicTests
