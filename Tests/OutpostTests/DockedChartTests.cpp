#include "pch.h"

#include "Canvas.h"
#include "ChartView.h"
#include "DockedChart.h"
#include "GoldenImage.h"
#include "Renderer.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace OutpostTests
{

namespace
{

constexpr std::uint32_t DESIGN_WIDTH_PIXELS = 1920;
constexpr std::uint32_t DESIGN_HEIGHT_PIXELS = 1080;
constexpr std::uint32_t BACKGROUND = 0x070605;

// The first galaxy's 256 systems on the galactic chart, at their x and half their y: its seeds, each system the next after four
// twists of them, as the game makes them (Galaxy.h's AdvanceToNextSystem).
[[nodiscard]] std::vector<Elite::ChartStar> FirstGalaxy()
{
  std::array<std::uint16_t, 3> seeds = {0x5A4A, 0x0248, 0xB753};
  std::vector<Elite::ChartStar> stars;
  for (int system = 0; system < 256; ++system)
  {
    stars.push_back(Elite::ChartStar{
      .x = static_cast<std::uint8_t>(seeds[1] >> 8), .row = static_cast<std::uint8_t>((seeds[0] >> 8) / 2), .large = false, .name = {}});
    for (int twist = 0; twist < 4; ++twist)
    {
      seeds = {seeds[1], seeds[2], static_cast<std::uint16_t>(seeds[0] + seeds[1] + seeds[2])};
    }
  }
  return stars;
}

// Lave's neighbors on its short-range chart, in the chart's units (3.5 to a galaxy unit about 80, 64), sized as
// `docked/06-short-range-chart.png` draws them.
[[nodiscard]] std::vector<Elite::ChartStar> AboutLave()
{
  return {{.x = 80, .row = 64, .large = true, .name = "LAVE"},      {.x = 76, .row = 25, .large = true, .name = "REORTE"},
          {.x = 20, .row = 78, .large = true, .name = "RIEDQUAT"},  {.x = 55, .row = 88, .large = false, .name = "LEESTI"},
          {.x = 143, .row = 99, .large = true, .name = "TIONISLA"}, {.x = 125, .row = 85, .large = true, .name = "ZAONCE"},
          {.x = 48, .row = 67, .large = false, .name = "DISO"},     {.x = 20, .row = 29, .large = true, .name = "USZAA"},
          {.x = 31, .row = 11, .large = true, .name = "ORRERE"},    {.x = 52, .row = 116, .large = true, .name = "ORERVE"}};
}

// The galactic chart as `docked/04-galactic-chart.png` draws it: docked at Lave with a full tank and 1250.0 credits, the cursor by
// Riedquat, and Riedquat's facts as the panel prints them.
[[nodiscard]] Elite::ChartView GalacticAsDrawn()
{
  Elite::ChartView view;
  view.sequence = 1;
  view.kind = Elite::ChartKind::Galactic;
  view.systemName = "LAVE";
  view.creditsTenths = 12500;
  view.galaxyNumber = 1;
  view.currentX = 20;
  view.currentRow = 86;
  view.reachTenths = 70;
  view.ringRadius = 17.5f;
  view.cursorX = 4;
  view.cursorRow = 89;
  view.stars = FirstGalaxy();
  view.target = Elite::ChartTarget{.facts = Elite::SystemFacts{.name = "RIEDQUAT",
                                                               .index = 46,
                                                               .distanceTenths = 64,
                                                               .economy = "Mainly Industrial",
                                                               .government = "Anarchy",
                                                               .techLevel = 9,
                                                               .populationTenthsBillion = 42,
                                                               .species = "Human Colonials",
                                                               .productivityMillionCredits = 16900,
                                                               .radiusKm = 5720,
                                                               .description = "Riedquat is a dangerous system, enter with caution."},
                                   .x = 3,
                                   .row = 90,
                                   .inRange = true,
                                   .shortTenths = 0};
  return view;
}

// The short-range chart as `docked/06-short-range-chart.png` draws it: the cursor by Zaonce, which is targeted.
[[nodiscard]] Elite::ChartView ShortRangeAsDrawn()
{
  Elite::ChartView view = GalacticAsDrawn();
  view.kind = Elite::ChartKind::ShortRange;
  view.currentX = 80;
  view.currentRow = 64;
  view.ringRadius = 61.25f;
  view.cursorX = 114;
  view.cursorRow = 81;
  view.stars = AboutLave();
  view.target = Elite::ChartTarget{.facts = Elite::SystemFacts{.name = "ZAONCE",
                                                               .index = 129,
                                                               .distanceTenths = 52,
                                                               .economy = "Average Industrial",
                                                               .government = "Corporate State",
                                                               .techLevel = 10,
                                                               .populationTenthsBillion = 49,
                                                               .species = "Human Colonials",
                                                               .productivityMillionCredits = 18560,
                                                               .radiusKm = 6140,
                                                               .description = "Zaonce is reasonably famous for its vast rain forests."},
                                   .x = 125,
                                   .row = 85,
                                   .inRange = true,
                                   .shortTenths = 0};
  return view;
}

// _view drawn on WARP at 1920 x 1080.
[[nodiscard]] GoldenImage::Picture Drawn(const Elite::ChartView& _view)
{
  Engine::Renderer renderer;
  Assert::IsTrue(
    renderer.Create(Engine::Renderer::Desc{.warp = true, .sourceWidth = 4, .sourceHeight = 2, .logicalWidth = 8, .logicalHeight = 4}),
    L"WARP");
  Assert::IsTrue(renderer.HasBahnschrift(), L"Bahnschrift installed: the design's face");
  Engine::Canvas canvas;
  const Outpost::TextMeasure measure = [&renderer](const Engine::CanvasText& _text) { return renderer.MeasureText(_text); };
  Outpost::DockedChart::Draw(_view, std::nullopt, measure, canvas);
  GoldenImage::Picture picture{.width = DESIGN_WIDTH_PIXELS,
                               .height = DESIGN_HEIGHT_PIXELS,
                               .pixels = renderer.Capture({}, BACKGROUND, DESIGN_WIDTH_PIXELS, DESIGN_HEIGHT_PIXELS, nullptr, &canvas)};
  Assert::AreEqual(std::size_t{DESIGN_WIDTH_PIXELS} * DESIGN_HEIGHT_PIXELS, picture.pixels.size(), L"the whole picture captured");
  return picture;
}

// Whether _click is the point _x, _row.
[[nodiscard]] bool Is(const std::optional<Elite::ChartClick>& _click, std::uint8_t _x, std::uint8_t _row)
{
  return _click && _click->x == _x && _click->row == _row;
}

// Whether _canvas holds the cursor's cross about _middle: its left arm ends 10 units short of it.
[[nodiscard]] bool CrossAt(const Engine::Canvas& _canvas, Engine::Point _middle)
{
  for (const Engine::CanvasShape& shape : _canvas.Shapes())
  {
    if (shape.kind == Engine::ShapeKind::Line && shape.params[0] == _middle.x - 32.0f && shape.params[1] == _middle.y &&
        shape.params[2] == _middle.x - 10.0f && shape.params[3] == _middle.y)
    {
      return true;
    }
  }
  return false;
}

} // namespace

// D04 to D07, the galactic and the short-range chart, as the shell draws them (ADR-065, D82, D98 to D100, D109, D110): on WARP,
// against their golden images (§6.5, D94); the chart's point a click finds; the keys a click sends in flight; and the panel's words.
TEST_CLASS(DockedChartTests)
{
public:
  // The galactic chart as `docked/04-galactic-chart.png` draws it, with the first galaxy's systems where they are.
  TEST_METHOD(TheGalacticChartAsDrawn)
  {
    GoldenImage::Check("D04-Galactic-Chart", Drawn(GalacticAsDrawn()));
  }

  // Find and out of range, as `docked/05-galactic-chart-find-out-of-range.png` draws them: Orrere, 8.3 light years, 1.3 short; and
  // RAXXLA not on this chart.
  TEST_METHOD(FindAndOutOfRangeAsDrawn)
  {
    Elite::ChartView view = GalacticAsDrawn();
    view.cursorX = 6;
    view.cursorRow = 71;
    view.target = Elite::ChartTarget{.facts = Elite::SystemFacts{.name = "ORRERE",
                                                                 .index = 227,
                                                                 .distanceTenths = 83,
                                                                 .economy = "Mainly Agricultural",
                                                                 .government = "Communist",
                                                                 .techLevel = 5,
                                                                 .populationTenthsBillion = 21,
                                                                 .species = "Green Fat Insects",
                                                                 .productivityMillionCredits = 9840,
                                                                 .radiusKm = 4920,
                                                                 .description = "A quiet world known for its slow rain forests."},
                                     .x = 6,
                                     .row = 71,
                                     .inRange = false,
                                     .shortTenths = 13};
    view.find = Elite::FindState::NotOnChart;
    view.typed = "raxxla";
    GoldenImage::Check("D05-Find-Out-Of-Range", Drawn(view));
  }

  // The short-range chart as `docked/06-short-range-chart.png` draws it.
  TEST_METHOD(TheShortRangeChartAsDrawn)
  {
    GoldenImage::Check("D06-Short-Range-Chart", Drawn(ShortRangeAsDrawn()));
  }

  // In flight, the countdown at 7 and Diso locked, as `docked/07-short-range-chart-in-flight-locked.png` draws it, the cursor
  // elsewhere; and Find being typed, which no drawing shows.
  TEST_METHOD(TheChartInFlightLocked)
  {
    Elite::ChartView view = ShortRangeAsDrawn();
    view.flight = true;
    view.locked = true;
    view.countdown = 7;
    view.cursorX = 94;
    view.cursorRow = 41;
    view.target = Elite::ChartTarget{.facts = Elite::SystemFacts{.name = "DISO",
                                                                 .index = 147,
                                                                 .distanceTenths = 33,
                                                                 .economy = "Average Agricultural",
                                                                 .government = "Democracy",
                                                                 .techLevel = 7,
                                                                 .populationTenthsBillion = 31,
                                                                 .species = "Human Colonials",
                                                                 .productivityMillionCredits = 12110,
                                                                 .radiusKm = 5880,
                                                                 .description = "Diso is mildly noted for its zero-g hockey."},
                                     .x = 48,
                                     .row = 67,
                                     .inRange = true,
                                     .shortTenths = 0};
    view.find = Elite::FindState::Typing;
    view.typed = "ries";
    GoldenImage::Check("D07-In-Flight-Locked", Drawn(view));
  }

  // A click finds the chart's point under it, held to the chart's units, and nothing off the chart's panel: on the galactic chart
  // five design units to a unit, filling the panel across; on the short-range chart the ring's scale about its middle.
  TEST_METHOD(AClickFindsThePointOfTheChart)
  {
    using Outpost::DockedChart;
    const Engine::Point lave = DockedChart::PlaceOf(Elite::ChartKind::Galactic, 20.0f, 86.0f);
    Assert::IsTrue(Is(DockedChart::ChartAt(Elite::ChartKind::Galactic, lave), 20, 86), L"Lave, where it is drawn");
    Assert::IsTrue(Is(DockedChart::ChartAt(Elite::ChartKind::Galactic, Engine::Point{.x = 62.0f, .y = 120.0f}), 0, 0), L"the top left");
    Assert::IsTrue(Is(DockedChart::ChartAt(Elite::ChartKind::Galactic, Engine::Point{.x = 1339.0f, .y = 910.0f}), 255, 127),
                   L"held at the bottom right");
    Assert::IsFalse(DockedChart::ChartAt(Elite::ChartKind::Galactic, Engine::Point{.x = 1500.0f, .y = 500.0f}).has_value(),
                    L"the side panel");
    Assert::IsFalse(DockedChart::ChartAt(Elite::ChartKind::Galactic, Engine::Point{.x = 500.0f, .y = 940.0f}).has_value(), L"the footer");

    const Engine::Point riedquat = DockedChart::PlaceOf(Elite::ChartKind::ShortRange, 20.0f, 78.0f);
    Assert::IsTrue(Is(DockedChart::ChartAt(Elite::ChartKind::ShortRange, riedquat), 20, 78), L"Riedquat, where it is drawn");
    Assert::IsTrue(Is(DockedChart::ChartAt(Elite::ChartKind::ShortRange, Engine::Point{.x = 800.0f, .y = 537.0f}), 80, 64),
                   L"Lave at the ring's middle");
    const Engine::Point edge = DockedChart::PlaceOf(Elite::ChartKind::ShortRange, 80.0f + 61.25f, 64.0f);
    Assert::AreEqual(340.0f, edge.x - 800.0f, 0.01f, L"7.0 light years is the drawn ring's 340 units");
  }

  // The cross follows the pointer while it is on the chart, and stands at the cursor while it is off it; the cursor and the
  // target move only on a click (D116).
  TEST_METHOD(TheCrossFollowsThePointerOnTheChart)
  {
    const Elite::ChartView view = GalacticAsDrawn();
    const Engine::Point cursor = Outpost::DockedChart::PlaceOf(view.kind, view.cursorX, view.cursorRow);
    const Engine::Point onChart{.x = 700.0f, .y = 500.0f};
    Engine::Canvas canvas;
    const Outpost::TextMeasure measure = [](const Engine::CanvasText&) { return 0.0f; };
    Outpost::DockedChart::Draw(view, onChart, measure, canvas);
    Assert::IsTrue(CrossAt(canvas, onChart) && !CrossAt(canvas, cursor), L"at the pointer on the chart");
    Outpost::DockedChart::Draw(view, Engine::Point{.x = 1500.0f, .y = 500.0f}, measure, canvas);
    Assert::IsTrue(CrossAt(canvas, cursor), L"at the cursor with the pointer on the side panel");
    Outpost::DockedChart::Draw(view, std::nullopt, measure, canvas);
    Assert::IsTrue(CrossAt(canvas, cursor), L"at the cursor with no pointer");
  }

  // In flight a click on the bar sends the flight's own key for the screen it names (D110); docked keys are the frame's.
  TEST_METHOD(TheBarSendsTheFlightsKeysInFlight)
  {
    using Outpost::DockedChart;
    using Outpost::DockedKey;
    Assert::AreEqual(std::uint8_t{0x3B}, DockedChart::FlightScanCodeOf(DockedKey::Launch).value_or(0), L"F1 FRONT, F1");
    Assert::AreEqual(std::uint8_t{0x42}, DockedChart::FlightScanCodeOf(DockedKey::Market).value_or(0), L"F2 MARKET, F8");
    Assert::AreEqual(std::uint8_t{0x3F}, DockedChart::FlightScanCodeOf(DockedKey::Galaxy).value_or(0), L"F4 GALAXY, F5");
    Assert::AreEqual(std::uint8_t{0x40}, DockedChart::FlightScanCodeOf(DockedKey::Local).value_or(0), L"F5 LOCAL, F6");
    Assert::AreEqual(std::uint8_t{0x43}, DockedChart::FlightScanCodeOf(DockedKey::Status).value_or(0), L"F6 STATUS, F9");
    Assert::IsFalse(DockedChart::FlightScanCodeOf(DockedKey::Disc).has_value(), L"ESC DISC off");
    Assert::IsFalse(DockedChart::FlightScanCodeOf(DockedKey::Equip).has_value(), L"F3 EQUIP off");
  }

  // The badge: in range, out of range by how far, locked, and the system the ship is at; and the thousands apart.
  TEST_METHOD(ThePanelIsWorded)
  {
    using Outpost::DockedChart;
    Elite::ChartView view = GalacticAsDrawn();
    Assert::AreEqual(std::wstring(L"IN RANGE"), DockedChart::BadgeOf(view));
    view.target.inRange = false;
    view.target.facts.distanceTenths = 83;
    view.target.shortTenths = 13;
    Assert::AreEqual(std::wstring(L"OUT OF RANGE \u00B7 1.3 LY SHORT"), DockedChart::BadgeOf(view));
    view.target.facts.distanceTenths = 0;
    Assert::AreEqual(std::wstring(L"CURRENT SYSTEM"), DockedChart::BadgeOf(view));
    view.locked = true;
    Assert::AreEqual(std::wstring(L"LOCKED \u00B7 IN RANGE"), DockedChart::BadgeOf(view));
    Assert::AreEqual(std::wstring(L"16 900"), DockedChart::Thousands(16900));
    Assert::AreEqual(std::wstring(L"980"), DockedChart::Thousands(980));
    Assert::AreEqual(std::wstring(L"1 234 567"), DockedChart::Thousands(1234567));
  }
};

} // namespace OutpostTests
