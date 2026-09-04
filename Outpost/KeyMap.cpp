#include "pch.h"

#include "KeyMap.h"

#include "DockedKeys.h"
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
     */
    constexpr KeyBinding BINDINGS[] = {
      // The docked screens, on the number row exactly as the C64 has them.
      {0x31, Elite::KEY_BUY_CARGO, "1 -- buy cargo"},           // VK_1
      {0x32, Elite::KEY_SELL_CARGO, "2 -- sell cargo"},         // VK_2
      {0x33, Elite::KEY_EQUIP_SHIP, "3 -- equip ship"},         // VK_3
      {0x34, Elite::KEY_LONG_RANGE, "4 -- long-range chart"},   // VK_4
      {0x35, Elite::KEY_SHORT_RANGE, "5 -- short-range chart"}, // VK_5
      {0x36, Elite::KEY_DATA_ON_SYSTEM, "6 -- data on system"}, // VK_6
      {0x37, Elite::KEY_MARKET_PRICE, "7 -- market prices"},    // VK_7
      {0x38, Elite::KEY_STATUS, "8 -- status"},                 // VK_8
      {0x39, Elite::KEY_INVENTORY, "9 -- inventory"},           // VK_9

      // Launch and the three views, on the function keys the masters name.
      {0x70, Elite::KEY_LAUNCH, "F1 -- launch"},         // VK_F1
      {0x72, Elite::KEY_REAR_VIEW, "F3 -- rear view"},   // VK_F3
      {0x74, Elite::KEY_LEFT_VIEW, "F5 -- left view"},   // VK_F5
      {0x76, Elite::KEY_RIGHT_VIEW, "F7 -- right view"}, // VK_F7

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
       */
      {0x0D, 63, "RETURN"}, // VK_RETURN
      {0x08, 64, "DELETE"}, // VK_BACK -- backspace is where a PC player expects delete
      {0x1B, 7, "ESCAPE"},  // VK_ESCAPE
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
