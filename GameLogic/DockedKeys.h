#pragma once

#include <cstdint>

namespace Elite
{

  /*
   * What a key press means when the game is docked or flying (slice 2e).
   *
   * TT102's dispatch, which is the game's whole top-level keyboard. Every screen in phases 2
   * and 3 is reached from here, and the routine is a chain of comparisons rather than a table -- so
   * the ORDER matters, two of the keys are tested against a different byte from the rest, and one
   * of them is not read from the accumulator at all.
   *
   * This is the DECISION and not the action. It performs nothing: it takes the key and the state
   * the comparisons read, and says which label the shipped routine would reach. That is deliberate
   * -- the labels it names span three phases (the trading screens are built, `LOOK1` and `hyp` are
   * the flight model's) and a function that performed them would have to wait for all of them. What
   * a caller does with the answer is the caller's, and the answer itself is comparable against the
   * original today, for every one of the 256 key codes.
   */

  /// The internal key numbers, which are the C64's keyboard matrix positions and not ASCII.
  /// They are here rather than in the executable's key map because TT102 compares against them.
  inline constexpr std::uint8_t KEY_LAUNCH = 0x3C;         ///< F1
  inline constexpr std::uint8_t KEY_BUY_CARGO = 0x08;      ///< "1"
  inline constexpr std::uint8_t KEY_SELL_CARGO = 0x05;     ///< "2"
  inline constexpr std::uint8_t KEY_EQUIP_SHIP = 0x38;     ///< "3"
  inline constexpr std::uint8_t KEY_LONG_RANGE = 0x35;     ///< "4"
  inline constexpr std::uint8_t KEY_SHORT_RANGE = 0x30;    ///< "5"
  inline constexpr std::uint8_t KEY_DATA_ON_SYSTEM = 0x2D; ///< "6"
  inline constexpr std::uint8_t KEY_MARKET_PRICE = 0x28;   ///< "7"
  inline constexpr std::uint8_t KEY_STATUS = 0x25;         ///< "8"
  inline constexpr std::uint8_t KEY_INVENTORY = 0x20;      ///< "9"
  inline constexpr std::uint8_t KEY_REAR_VIEW = 0x3B;      ///< F3
  inline constexpr std::uint8_t KEY_LEFT_VIEW = 0x3A;      ///< F5
  inline constexpr std::uint8_t KEY_RIGHT_VIEW = 0x3D;     ///< F7
  inline constexpr std::uint8_t KEY_DISK_ACCESS = 0x12;    ///< &12 -- "@"
  inline constexpr std::uint8_t KEY_DISTANCE = 0x2E;       ///< "D"
  inline constexpr std::uint8_t KEY_FIND_SYSTEM = 0x2B;    ///< "F"
  inline constexpr std::uint8_t KEY_HOME = 0x1A;           ///< "O"

  /// The three view numbers `LOOK1` is given, reached by falling through two `EQUB &2C`s, so
  /// the assembler's own bytes decide which one a jump lands on.
  inline constexpr std::uint8_t VIEW_REAR = 1;
  inline constexpr std::uint8_t VIEW_LEFT = 2;
  inline constexpr std::uint8_t VIEW_RIGHT = 3;

  /// The routine tests the TOP TWO BITS and nothing else, so any view with either of
  /// them set counts as a chart.
  [[nodiscard]] constexpr bool IsChartView(std::uint8_t _view) noexcept
  {
    return (_view & 0xC0u) != 0u;
  }

  /// Which label TT102 reaches. Named for the label rather than for what the label does, because
  /// two of them do the same thing from different states and one does nothing at all.
  enum class KeyAction
  {
    Nothing,            ///< T95, which is an RTS -- the key was not one of these
    StatusMode,
    LongRangeChart,
    ShortRangeChart,
    DataOnSystem,       ///< TT111 then TT25 -- the only target reached by two calls
    Inventory,
    MarketPrice,
    Launch,             ///< TT110, and it is tested BEFORE the docked check
    EquipShip,          ///< EQSHP, docked only
    BuyCargo,           ///< TT219, docked only
    DiskAccess,         ///< SVE, then QU5 or BAY on its carry -- docked only
    SellCargo,          ///< TT208, docked only
    ChangeView,         ///< LOOK1, in flight only -- the view is in the result
    Hyperspace,         ///< hyp, and it is NOT decided by the key in A
    ShowDistance,
    SearchBySystemName,
    HomeCrosshairs,     ///< TT103 / ping / TT103, which is a TAIL call and skips the counter
    MoveCrosshairs,     ///< TT16, and then the counter
    CountdownOnly,      ///< TT107 reached without moving anything
  };

  struct KeyOutcome
  {
    KeyAction action = KeyAction::Nothing;
    std::uint8_t view = 0; ///< X for LOOK1. Only meaningful for ChangeView.
  };

  /*
   * One key press, and which of eighteen places the game goes next.
   *
   * Four things about it are worth knowing, and none of them is visible from the list of keys.
   *
   * LAUNCH IS TESTED BEFORE THE DOCKED CHECK. A test of `QQ12`'s top bit splits the docked keys
   * from the flight ones, and F1, the four screens and the two charts are all ABOVE it -- so the
   * status screen and the charts work in space, and pressing F1 in space reaches `TT110` just as it
   * does on the pad. What is below the split is the shop, the two trading screens, the disk menu
   * and, on the other branch, the three view changes.
   *
   * "H" IS NOT READ FROM THE KEY. `BIT KLO+HINT` reads the keyboard MATRIX directly, so hyperspace
   * is checked against whether the key is HELD rather than against the key that was translated into
   * A. A player holding H while pressing something else gets hyperspace, and the key they pressed
   * is discarded. That is why `_hyperspaceHeld` is an argument here and not a comparison.
   *
   * QQ12 IS TESTED TWO WAYS. The docked/flight split reads its top bit and NOTHING ELSE; the
   * system search asks whether the WHOLE BYTE is zero. `BAY` sets it to &FF and the flight code sets it to 0, so the two agree
   * for every value the game produces -- and they would disagree for, say, 1. The port keeps both
   * tests rather than one flag, because the byte is what the original branches on.
   *
   * AND THE THREE VIEW KEYS SHARE THEIR TAIL THROUGH TWO `EQUB &2C`s. The load of 3 falls past the
   * load of 2 and the load of 1 because each is swallowed by a `BIT absolute` opcode assembled from
   * the byte before it -- so the three entry points are one instruction stream read three ways, and
   * the view number depends on where the jump landed rather than on any comparison.
   */
  [[nodiscard]] KeyOutcome ActionForKey(std::uint8_t _key, std::uint8_t _dockedFlag, std::uint8_t _view, std::uint8_t _countdown,
                                        bool _hyperspaceHeld) noexcept;

} // namespace Elite
