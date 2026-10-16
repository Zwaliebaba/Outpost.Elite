#include "pch.h"

#include "Canvas.h"
#include "DockedEquip.h"
#include "GoldenImage.h"
#include "Renderer.h"
#include "StatusView.h"

#include <array>
#include <optional>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace OutpostTests
{

namespace
{

constexpr std::uint32_t DESIGN_WIDTH_PIXELS = 1920;
constexpr std::uint32_t DESIGN_HEIGHT_PIXELS = 1080;
constexpr std::uint32_t BACKGROUND = 0x070605;

// The mounts, as the game numbers them.
constexpr std::uint8_t FORE = 0;
constexpr std::uint8_t AFT = 1;
constexpr std::uint8_t RIGHT = 2;
constexpr std::uint8_t LEFT = 3;

// The shipyard as `docked/02-equip-ship-tl6.png` draws it: Lave at tech level 6, its 8 items at the drawing's prices, 7.0 light
// years, three missiles and a pulse laser on the front mount, 1250.0 credits, fuel selected.
[[nodiscard]] Elite::EquipView AsDrawn()
{
  constexpr std::array<std::uint16_t, Elite::EQUIP_ROWS_MOST> BUY = {21,    336,  3950,  6055,  4099,  10207, 5118,
                                                                     10190, 9000, 15000, 15000, 50000, 8000,  60000};
  constexpr std::array<std::uint16_t, Elite::EQUIP_ROWS_MOST> SELL = {0,    325,  3832,  5873,  4034,  9901, 4964,
                                                                      9884, 8730, 14550, 14550, 48500, 7760, 58200};
  Elite::EquipView view;
  view.sequence = 1;
  view.systemName = "LAVE";
  view.creditsTenths = 12500;
  view.techLevel = 6;
  view.rows = 8;
  for (std::size_t row = 0; row < Elite::EQUIP_ROWS_MOST; ++row)
  {
    view.items[row].buyTenths = BUY[row];
    view.items[row].sellTenths = SELL[row];
  }
  view.items[0].count = 0xFF;
  view.items[1].count = 3;
  view.items[4].count = 1;
  view.items[4].mounts = Elite::MOUNT_FORE;
  view.fuelTenths = 70;
  view.buyAllowed = true;
  return view;
}

// The same at Zaonce, tech level 10, all 14 items, with the beam laser selected and the picker open to buy it, as
// `docked/03-equip-ship-tl10-laser-mount.png` draws it: the front mount's pulse laser to trade in and the other three empty.
[[nodiscard]] Elite::EquipView PickingAsDrawn()
{
  Elite::EquipView view = AsDrawn();
  view.systemName = "ZAONCE";
  view.techLevel = 10;
  view.rows = 14;
  view.selected = 5;
  view.picker.open = true;
  view.picker.row = 6;
  for (Elite::MountView& mount : view.picker.mounts)
  {
    mount.allowed = true;
  }
  view.picker.mounts[FORE].laser = true;
  return view;
}

// _view drawn on WARP at 1920 x 1080.
[[nodiscard]] GoldenImage::Picture Drawn(const Elite::EquipView& _view)
{
  Engine::Renderer renderer;
  Assert::IsTrue(
    renderer.Create(Engine::Renderer::Desc{.warp = true, .sourceWidth = 4, .sourceHeight = 2, .logicalWidth = 8, .logicalHeight = 4}),
    L"WARP");
  Assert::IsTrue(renderer.HasBahnschrift(), L"Bahnschrift installed: the design's face");
  Engine::Canvas canvas;
  const Outpost::TextMeasure measure = [&renderer](const Engine::CanvasText& _text) { return renderer.MeasureText(_text); };
  Outpost::DockedEquip::Draw(_view, std::nullopt, measure, canvas);
  GoldenImage::Picture picture{.width = DESIGN_WIDTH_PIXELS,
                               .height = DESIGN_HEIGHT_PIXELS,
                               .pixels = renderer.Capture({}, BACKGROUND, DESIGN_WIDTH_PIXELS, DESIGN_HEIGHT_PIXELS, nullptr, &canvas)};
  Assert::AreEqual(std::size_t{DESIGN_WIDTH_PIXELS} * DESIGN_HEIGHT_PIXELS, picture.pixels.size(), L"the whole picture captured");
  return picture;
}

// Whether _click is _control, with _index.
[[nodiscard]] bool Is(const std::optional<Elite::EquipClick>& _click, Elite::EquipControl _control, std::uint8_t _index = 0)
{
  return _click && _click->control == _control && _click->index == _index;
}

} // namespace

// D02 and D03, the shipyard and its mount picker, as the shell draws them (ADR-064, D82, D103, D104): on WARP, against their golden
// images (§6.5, D94); the controls a click finds; and the words of the Fitted column and the strips.
TEST_CLASS(DockedEquipTests)
{
public:
  // The screen as `docked/02-equip-ship-tl6.png` draws it.
  TEST_METHOD(TheShipyardAsDrawn)
  {
    GoldenImage::Check("D02-Equip", Drawn(AsDrawn()));
  }

  // The picker as `docked/03-equip-ship-tl10-laser-mount.png` draws it.
  TEST_METHOD(ThePickerAsDrawn)
  {
    GoldenImage::Check("D03-Mount-Picker", Drawn(PickingAsDrawn()));
  }

  // What the drawings leave to build: the picker selling (D103), the mounts without the laser dimmed, and a refusal's strip under it.
  TEST_METHOD(ThePickerSelling)
  {
    Elite::EquipView view = PickingAsDrawn();
    view.selected = 4;
    view.sellAllowed = true;
    view.items[4].count = 2;
    view.items[4].mounts = Elite::MOUNT_FORE | Elite::MOUNT_LEFT;
    view.picker.row = 5;
    view.picker.selling = true;
    view.picker.mounts[LEFT].laser = true;
    view.picker.mounts[AFT].allowed = false;
    view.picker.mounts[RIGHT].allowed = false;
    view.message = Elite::EquipMessage{.notice = Elite::EquipNotice::MountWithout, .row = 5, .mount = AFT};
    GoldenImage::Check("D03-Sell-Picker", Drawn(view));
  }

  // A laser fitted, trading another in: D11's success, under the Selected panel.
  TEST_METHOD(TheShipyardFitted)
  {
    Elite::EquipView view = PickingAsDrawn();
    view.picker = Elite::PickerView{};
    view.items[5].count = 1;
    view.items[5].mounts = Elite::MOUNT_AFT;
    view.message = Elite::EquipMessage{.notice = Elite::EquipNotice::LaserFitted, .row = 6, .mount = AFT, .tradedIn = true, .tradedRow = 5};
    GoldenImage::Check("D02-Fitted", Drawn(view));
  }

  // A click finds a row by the list's pitch, 64 units for 8 rows and 52 for 14, the trades, and in the picker its mounts by D104's
  // places and ESC CANCEL.
  TEST_METHOD(AClickFindsItsControl)
  {
    const Elite::EquipView list = AsDrawn();
    Assert::IsTrue(Is(Outpost::DockedEquip::ControlAt(list, Engine::Point{.x = 100.0f, .y = 200.0f}), Elite::EquipControl::Row, 0),
                   L"fuel");
    Assert::IsTrue(Is(Outpost::DockedEquip::ControlAt(list, Engine::Point{.x = 100.0f, .y = 660.0f}), Elite::EquipControl::Row, 7),
                   L"the escape capsule, the eighth row");
    Assert::IsFalse(Outpost::DockedEquip::ControlAt(list, Engine::Point{.x = 100.0f, .y = 700.0f}).has_value(), L"under the last row");
    Assert::IsTrue(Is(Outpost::DockedEquip::ControlAt(list, Engine::Point{.x = 1500.0f, .y = 370.0f}), Elite::EquipControl::Buy), L"BUY");
    Assert::IsTrue(Is(Outpost::DockedEquip::ControlAt(list, Engine::Point{.x = 1700.0f, .y = 370.0f}), Elite::EquipControl::Sell), L"SELL");
    Assert::IsFalse(Outpost::DockedEquip::ControlAt(list, Engine::Point{.x = 1620.0f, .y = 570.0f}).has_value(), L"no picker");

    const Elite::EquipView picking = PickingAsDrawn();
    Assert::IsTrue(Is(Outpost::DockedEquip::ControlAt(picking, Engine::Point{.x = 100.0f, .y = 890.0f}), Elite::EquipControl::Row, 13),
                   L"the military laser, the fourteenth row");
    Assert::IsTrue(Is(Outpost::DockedEquip::ControlAt(picking, Engine::Point{.x = 1620.0f, .y = 570.0f}), Elite::EquipControl::Mount, FORE),
                   L"1 front");
    Assert::IsTrue(Is(Outpost::DockedEquip::ControlAt(picking, Engine::Point{.x = 1470.0f, .y = 640.0f}), Elite::EquipControl::Mount, LEFT),
                   L"2 left");
    Assert::IsTrue(
      Is(Outpost::DockedEquip::ControlAt(picking, Engine::Point{.x = 1760.0f, .y = 640.0f}), Elite::EquipControl::Mount, RIGHT),
      L"3 right");
    Assert::IsTrue(Is(Outpost::DockedEquip::ControlAt(picking, Engine::Point{.x = 1620.0f, .y = 710.0f}), Elite::EquipControl::Mount, AFT),
                   L"4 rear, the original's aft");
    Assert::IsTrue(Is(Outpost::DockedEquip::ControlAt(picking, Engine::Point{.x = 1780.0f, .y = 498.0f}), Elite::EquipControl::Cancel),
                   L"ESC CANCEL");
    Assert::IsFalse(Outpost::DockedEquip::ControlAt(picking, Engine::Point{.x = 1620.0f, .y = 640.0f}).has_value(), L"the ship's place");
  }

  // The Fitted column: the fuel in light years, the missiles of four, a laser's mounts in D104's order, an item fitted, and a dash.
  TEST_METHOD(TheFittedColumnIsWorded)
  {
    Elite::EquipView view = AsDrawn();
    view.items[5].mounts = Elite::MOUNT_AFT | Elite::MOUNT_LEFT | Elite::MOUNT_FORE;
    view.items[3].count = 1;
    Assert::AreEqual(std::wstring(L"7.0 LY"), Outpost::DockedEquip::FittedText(view, 1));
    Assert::AreEqual(std::wstring(L"3 / 4"), Outpost::DockedEquip::FittedText(view, 2));
    Assert::AreEqual(std::wstring(L"—"), Outpost::DockedEquip::FittedText(view, 3));
    Assert::AreEqual(std::wstring(L"FITTED"), Outpost::DockedEquip::FittedText(view, 4));
    Assert::AreEqual(std::wstring(L"FRONT"), Outpost::DockedEquip::FittedText(view, 5));
    Assert::AreEqual(std::wstring(L"FRONT · LEFT · REAR"), Outpost::DockedEquip::FittedText(view, 6));
  }

  // D11's Equipment column, split at its dash.
  TEST_METHOD(TheStripsAreD11s)
  {
    const auto words = [](const Elite::EquipMessage& _message)
    {
      const Outpost::DockedEquip::Words split = Outpost::DockedEquip::WordsOf(_message);
      return split.headline + L" — " + split.detail;
    };
    Assert::AreEqual(std::wstring(L"FUEL TANKS FULL — 7.0 LIGHT YEARS"),
                     words(Elite::EquipMessage{.notice = Elite::EquipNotice::FuelFull, .row = 1, .fuelTenths = 70}));
    Assert::AreEqual(std::wstring(L"FOUR MISSILES ONLY — ALL PYLONS LOADED"),
                     words(Elite::EquipMessage{.notice = Elite::EquipNotice::FourMissiles, .row = 2}));
    Assert::AreEqual(std::wstring(L"FOUR LASERS — EVERY MOUNT ALREADY CARRIES A BEAM LASER"),
                     words(Elite::EquipMessage{.notice = Elite::EquipNotice::FourLasers, .row = 6}));
    Assert::AreEqual(std::wstring(L"TOO MUCH CARGO TO SELL THE EXTENSION — UNLOAD 6 T FIRST"),
                     words(Elite::EquipMessage{.notice = Elite::EquipNotice::TooMuchCargo, .row = 3, .tonnes = 6}));
    Assert::AreEqual(
      std::wstring(L"NOT ENOUGH CREDITS — 1020.7 CR NEEDED, 41.2 CR AVAILABLE"),
      words(Elite::EquipMessage{.notice = Elite::EquipNotice::NotEnoughCredits, .row = 6, .tenths = 10207, .creditsTenths = 412}));
    Assert::AreEqual(
      std::wstring(L"BEAM LASER FITTED — REAR MOUNT · PULSE LASER TRADED IN"),
      words(Elite::EquipMessage{.notice = Elite::EquipNotice::LaserFitted, .row = 6, .mount = AFT, .tradedIn = true, .tradedRow = 5}));
  }
};

} // namespace OutpostTests
