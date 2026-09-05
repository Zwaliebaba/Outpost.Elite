#include "pch.h"

#include "KeyMap.h"

#include "Controls.h"
#include "DockedKeys.h"
#include "FlightLoop.h"
#include "LookupTables.h"
#include "StartUp.h"

namespace Outpost
{

  namespace
  {
    /*
     * The map. Every C64 value here is either a constant `GameLogic` already names or a position
     * taken from `TRANTABLE`, so the test can check both ends of every row.
     *
     * The internal numbers for the characters the line editor needs -- RETURN, DELETE, ESCAPE -- are
     * looked up rather than remembered: 63 translates to 13, 64 to 127 and 7 to 27, and a test walks
     * the table to prove it rather than leaving three magic numbers in a comment.
     *
     * The `// 6502:` comment on a flight row names the KY label AND the key the C64 player used, so
     * the row can be read against `KEYLOOK` without opening two files. Where the two disagree with
     * the received wisdom they follow `KEYLOOK`: the energy bomb is the COMMODORE key on a C64 and
     * not TAB, and the escape pod is the "left arrow" key and not ESCAPE -- those are the BBC
     * Micro's keys, which is where `Controls.h` and `FlightLoop.h` took their comments from.
     */
    constexpr KeyBinding BINDINGS[] = {
      /*
       * The seven primary flight controls, 6502: KY1 to KY7.
       *
       * The arrows steer and the two keys beside them set speed, which is the swap that makes this
       * map modern rather than faithful: on the C64 the arrangement is the other way round, with
       * roll on "," and "." (the unshifted "<" and ">") and speed on Space and "/".
       */
      {0x25, Elite::KEY_ROLL_LEFT, "Left -- roll left"},    // VK_LEFT,       6502: KY3, C64 "<"
      {0x27, Elite::KEY_ROLL_RIGHT, "Right -- roll right"}, // VK_RIGHT,      6502: KY4, C64 ">"
      {0x26, Elite::KEY_PITCH_UP, "Up -- climb"},           // VK_UP,         6502: KY5, C64 "X"
      {0x28, Elite::KEY_PITCH_DOWN, "Down -- dive"},        // VK_DOWN,       6502: KY6, C64 "S"
      {0xBE, Elite::KEY_SPEED_UP, ". -- increase speed"},   // VK_OEM_PERIOD, 6502: KY2, C64 Space
      {0xBC, Elite::KEY_SLOW_DOWN, ", -- decrease speed"},  // VK_OEM_COMMA,  6502: KY1, C64 "?"
      {0x41, Elite::KEY_FIRE, "A -- fire lasers"},          // VK_A,          6502: KY7, C64 "A"

      /*
       * AND SPACE, WHICH IS THE ONE KEY THE GAME NAMES IN ITS OWN TEXT.
       *
       * "PRESS SPACE OR FIRE, COMMANDER." is token 7, and `TITLE` waits for it: `JSR RDKEY / BIT
       * KY7 / BMI TL3 / BCC TLL2` loops until the fire button or ANY key. Any key is what the
       * shell does too -- and Space was not one, because the layout above moved speed onto "." and
       * left nothing on the key the prompt asks for. The title screen ignored the one press a
       * player is told to make.
       *
       * It maps to position 4, which IS the C64's Space, so this is the original's binding rather
       * than an invented one: it dismisses every "press space" prompt AND increases speed in
       * flight, exactly as it did on the machine. `.` keeps the same position beside it, which is
       * the seventh alias in a map that has six -- and the only one that is not a moved screen.
       */
      {0x20, Elite::KEY_SPEED_UP, "Space -- increase speed, and every \"press space\" prompt"}, // VK_SPACE, 6502: KY2

      /*
       * The nine secondary flight controls, 6502: KY12 to KY20.
       *
       * Seven of them keep the C64's own letter. The two that do not are the two the C64 put on
       * keys a PC has no equivalent of: the energy bomb was the COMMODORE key and is TAB here, and
       * the escape pod was the "left arrow" key and is ESCAPE -- and ESCAPE is the same binding the
       * line editor already needed, because position 7 is what translates to 27.
       */
      {0x09, Elite::KEY_ENERGY_BOMB, "Tab -- energy bomb"},         // VK_TAB, 6502: KY12, C64 "C="
      {0x54, Elite::KEY_ARM_MISSILE, "T -- target missile"},        // VK_T,   6502: KY14
      {0x55, Elite::KEY_UNARM_MISSILE, "U -- unarm missile"},       // VK_U,   6502: KY15
      {0x4D, Elite::KEY_FIRE_MISSILE, "M -- fire missile"},         // VK_M,   6502: KY16
      {0x45, Elite::KEY_ECM, "E -- E.C.M."},                        // VK_E,   6502: KY17
      {0x4A, Elite::KEY_WARP, "J -- in-system jump"},               // VK_J,   6502: KY18
      {0x43, Elite::KEY_DOCKING_COMPUTER, "C -- docking computer"}, // VK_C,   6502: KY19
      {0x50, Elite::KEY_CANCEL_DOCKING, "P -- cancel docking"},     // VK_P,   6502: KY20

      /*
       * The six information screens, on F1 to F6.
       *
       * AND THE NUMBER ROW KEEPS THEM TOO, which is the many-to-one `KeyMap.h` explains: the digit
       * is what `gnum` and the line editor read, so it cannot be given up, and giving it up is the
       * only thing that would stop it reaching the screen as well.
       */
      {0x70, Elite::KEY_LONG_RANGE, "F1 -- Galactic Chart"},     // VK_F1, 6502: f4
      {0x71, Elite::KEY_SHORT_RANGE, "F2 -- local chart"},       // VK_F2, 6502: f5
      {0x72, Elite::KEY_DATA_ON_SYSTEM, "F3 -- data on system"}, // VK_F3, 6502: f6
      {0x73, Elite::KEY_MARKET_PRICE, "F4 -- market prices"},    // VK_F4, 6502: f7
      {0x74, Elite::KEY_STATUS, "F5 -- status"},                 // VK_F5, 6502: f8
      {0x75, Elite::KEY_INVENTORY, "F6 -- inventory"},           // VK_F6, 6502: f9

      /*
       * The four views, on F7 to F10.
       *
       * F7 IS ALSO LAUNCH, and that is the game's doing rather than a shortage of keys: `f0` is one
       * key and `TT110` is one routine, which leaves the pad when docked and shows the front view
       * when not. Moving the front view moves launch with it.
       *
       * F10 reaches the window as WM_SYSKEYDOWN rather than WM_KEYDOWN and opens the system menu on
       * the way past. `Window::OnMessage` handles both, which is why the right view is reachable.
       */
      {0x76, Elite::KEY_LAUNCH, "F7 -- forward view, and launch"}, // VK_F7,  6502: f0
      {0x77, Elite::KEY_REAR_VIEW, "F8 -- rear view"},             // VK_F8,  6502: f12
      {0x78, Elite::KEY_LEFT_VIEW, "F9 -- left view"},             // VK_F9,  6502: f22
      {0x79, Elite::KEY_RIGHT_VIEW, "F10 -- right view"},          // VK_F10, 6502: f32

      // The number row. Three of these are the only way to the trading screens; the other six are
      // aliases of F1 to F6 above, and all nine are what the two number readers see as digits.
      {0x31, Elite::KEY_BUY_CARGO, "1 -- buy cargo"},                   // VK_1
      {0x32, Elite::KEY_SELL_CARGO, "2 -- sell cargo"},                 // VK_2
      {0x33, Elite::KEY_EQUIP_SHIP, "3 -- equip ship"},                 // VK_3
      {0x34, Elite::KEY_LONG_RANGE, "4 -- Galactic Chart, F1's digit"}, // VK_4
      {0x35, Elite::KEY_SHORT_RANGE, "5 -- local chart, F2's digit"},   // VK_5
      {0x36, Elite::KEY_DATA_ON_SYSTEM, "6 -- data, F3's digit"},       // VK_6
      {0x37, Elite::KEY_MARKET_PRICE, "7 -- market, F4's digit"},       // VK_7
      {0x38, Elite::KEY_STATUS, "8 -- status, F5's digit"},             // VK_8
      {0x39, Elite::KEY_INVENTORY, "9 -- inventory, F6's digit"},       // VK_9

      // The letter keys the game names as constants.
      {0x44, Elite::KEY_DISTANCE, "D -- distance to system"}, // VK_D
      {0x46, Elite::KEY_FIND_SYSTEM, "F -- find system"},     // VK_F
      {0x48, 0x23, "H -- hyperspace"},                        // VK_H, 6502: HINT
      {0x4F, Elite::KEY_HOME, "O -- crosshairs home"},        // VK_O
      {0x59, Elite::KEY_YES_INTERNAL, "Y -- yes"},            // VK_Y
      {0x4E, 0x19, "N -- no"},                                // VK_N

      /*
       * The disk menu's "@". A PC keyboard has no C64 "@" key; VK_OEM_3 is where it sits on a UK
       * layout and is the same physical key a C64 player used, which is the closest thing to not
       * choosing. On a US layout that key is backquote, and remapping is phase 6.
       */
      {0xC0, Elite::KEY_DISK_ACCESS, "@ -- disk access menu"}, // VK_OEM_3

      /*
       * The three the line editor compares numbers against. Their positions translate to 13, 127 and
       * 27, which is checked rather than asserted here.
       *
       * RETURN is 63 and NOT 59, which is worth the sentence because 59 is the obvious guess and it
       * is the rear view: TRANTABLE's tail runs 8, 9, 10, 11, 12, 14, 13, 127 over positions 57..64,
       * so the function keys sit either side of RETURN and one off in the wrong direction gives F3.
       *
       * ESCAPE is the escape capsule as well, because position 7 is the C64's "left arrow" key and
       * that is the key `KY13` watches. One binding, two jobs, exactly as the original has it.
       */
      {0x0D, 63, "RETURN"}, // VK_RETURN
      {0x08, 64, "DELETE"}, // VK_BACK -- backspace is where a PC player expects delete
      {0x1B, Elite::KEY_ESCAPE_POD, "ESCAPE -- escape capsule, and 27 for the line editor"}, // VK_ESCAPE, 6502: KY13
    };

    /*
     * The chart crosshairs, and this is the one place a PC key stands for TWO C64 keys.
     *
     * THE C64 HAS ONE CURSOR KEY PER AXIS. `TT17` reads "cursor left/right" and "cursor up/down"
     * and takes the DIRECTION from whether a SHIFT is held -- two keys and a modifier where a PC
     * has four arrows. So each arrow maps to its axis key, and the two that need the other
     * direction press SHIFT as well; the game reads exactly the five entries it always did.
     *
     * THE SAME FOUR KEYS STEER IN FLIGHT, which is not a conflict on the C64 because there it is
     * `<`, `>`, `X` and `S` that steer and the cursor keys are the chart's alone. Here one key has
     * both jobs, and `FlightSession::ScanKeyboard` is where they are told apart: with a chart
     * showing it drops the steering entries, so the arrows aim rather than roll. That rule is the
     * port's own and is marked as such where it lives.
     *
     * RETURN is the accelerator and is already bound for the line editor, at the same position
     * `TT17` reads -- so holding it moves the crosshairs four at a time, exactly as it does on a
     * C64, for no extra binding at all.
     *
     * AND THE Y PAIR IS THE OTHER WAY ROUND FROM THE X PAIR, which looks like a slip and is not.
     * `TT17` ends the y axis with `EOR #%11111110`, so the UNSHIFTED key steps `QQ10` by -1 and
     * the shifted one by +1 -- and `QQ10` grows DOWNWARDS on both charts, because a system's screen
     * row is its y halved. Unshifted therefore moves the crosshairs UP the screen, and the arrow
     * that means "up" to a player is the one that must not press shift. Measured on the chart
     * rather than reasoned about: the first version had them the obvious way round and the
     * crosshairs went the wrong way.
     */
    struct CursorBinding
    {
      int virtualKey = 0;
      std::uint8_t axis = NO_KEY;  ///< 6502: KLO+&3E or KLO+&39
      std::uint8_t shift = NO_KEY; ///< 6502: KLO+&31, pressed for the reverse direction
      const char* what = "";
    };

    constexpr CursorBinding CURSORS[] = {
      {0x27, Elite::KEY_CURSOR_X, NO_KEY, "Right -- crosshairs right"},              // VK_RIGHT
      {0x25, Elite::KEY_CURSOR_X, Elite::KEY_SHIFT_LEFT, "Left -- crosshairs left"}, // VK_LEFT
      {0x26, Elite::KEY_CURSOR_Y, NO_KEY, "Up -- crosshairs up"},                    // VK_UP
      {0x28, Elite::KEY_CURSOR_Y, Elite::KEY_SHIFT_LEFT, "Down -- crosshairs down"}, // VK_DOWN
    };

    constexpr int BINDING_COUNT = static_cast<int>(sizeof(BINDINGS) / sizeof(BINDINGS[0]));
  } // namespace

  const KeyBinding* Bindings() noexcept
  {
    return BINDINGS;
  }

  int BindingCount() noexcept
  {
    return BINDING_COUNT;
  }

  std::uint8_t C64KeyFor(int _virtualKey) noexcept
  {
    for (const KeyBinding& binding : BINDINGS)
    {
      if (binding.virtualKey == _virtualKey)
      {
        return binding.c64Key;
      }
    }
    return NO_KEY;
  }

  CursorKeys CursorKeysFor(int _virtualKey) noexcept
  {
    for (const CursorBinding& binding : CURSORS)
    {
      if (binding.virtualKey == _virtualKey)
      {
        return {binding.axis, binding.shift};
      }
    }
    return {};
  }

  std::uint8_t CharacterFor(std::uint8_t _c64Key) noexcept
  {
    // 6502: LDA TRANTABLE,X -- and X cannot exceed 64, because that is what RDKEY produces. A key
    // outside the table is not a key the hardware could have reported.
    if (_c64Key >= Elite::KEY_TRANSLATION.size())
    {
      return 0;
    }
    return Elite::KEY_TRANSLATION[_c64Key];
  }

} // namespace Outpost
