#include "pch.h"

#include "Canvas.h"
#include "DockedStatus.h"
#include "GoldenImage.h"
#include "Renderer.h"

#include <optional>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace OutpostTests
{

namespace
{

constexpr std::uint32_t DESIGN_WIDTH_PIXELS = 1920;
constexpr std::uint32_t DESIGN_HEIGHT_PIXELS = 1080;
constexpr std::uint32_t BACKGROUND = 0x070605;

// Jameson as `docked/08-commander-status.png` draws them: at Lave, Diso targeted, 7.0 light years and 1250.0 credits, Clean and
// Harmless, three missiles and a pulse laser on the front mount, and 4 t of food, 2 t of computers and 6 g of gem-stones.
[[nodiscard]] Elite::StatusView AsDrawn()
{
  Elite::StatusView view;
  view.sequence = 1;
  view.systemName = "LAVE";
  view.title = "COMMANDER";
  view.commanderName = "JAMESON";
  view.presentSystem = "LAVE";
  view.hyperspaceTarget = "DISO";
  view.fuelTenths = 70;
  view.creditsTenths = 12500;
  view.equipment = {Elite::FittedItem{.kind = Elite::FittedKind::Missiles, .count = 3, .mounts = 0},
                    Elite::FittedItem{.kind = Elite::FittedKind::PulseLaser, .count = 1, .mounts = Elite::MOUNT_FORE}};
  view.held[0] = 4;
  view.held[7] = 2;
  view.held[15] = 6;
  view.usedTonnes = 6;
  view.bayTonnes = 20;
  return view;
}

// _view drawn on WARP at 1920 x 1080.
[[nodiscard]] GoldenImage::Picture Drawn(const Elite::StatusView& _view)
{
  Engine::Renderer renderer;
  Assert::IsTrue(
    renderer.Create(Engine::Renderer::Desc{.warp = true, .sourceWidth = 4, .sourceHeight = 2, .logicalWidth = 8, .logicalHeight = 4}),
    L"WARP");
  Assert::IsTrue(renderer.HasBahnschrift(), L"Bahnschrift installed: the design's face");
  Engine::Canvas canvas;
  const Outpost::TextMeasure measure = [&renderer](const Engine::CanvasText& _text) { return renderer.MeasureText(_text); };
  Outpost::DockedStatus::Draw(_view, std::nullopt, measure, canvas);
  GoldenImage::Picture picture{.width = DESIGN_WIDTH_PIXELS,
                               .height = DESIGN_HEIGHT_PIXELS,
                               .pixels = renderer.Capture({}, BACKGROUND, DESIGN_WIDTH_PIXELS, DESIGN_HEIGHT_PIXELS, nullptr, &canvas)};
  Assert::AreEqual(std::size_t{DESIGN_WIDTH_PIXELS} * DESIGN_HEIGHT_PIXELS, picture.pixels.size(), L"the whole picture captured");
  return picture;
}

} // namespace

// D08, the commander's status, as the shell draws it (ADR-063, D82, D75): on WARP, against its golden images (§6.5, D94); and its
// equipment's words.
TEST_CLASS(DockedStatusTests)
{
public:
  // The screen as `docked/08-commander-status.png` draws it.
  TEST_METHOD(TheStatusAsDrawn)
  {
    GoldenImage::Check("D08-Status", Drawn(AsDrawn()));
  }

  // What the drawing leaves to build: a fugitive, a rating past Harmless, a laser on two mounts and refugees in the hold.
  TEST_METHOD(TheStatusOfAFugitive)
  {
    Elite::StatusView view = AsDrawn();
    view.legal = Elite::LegalStanding::Fugitive;
    view.rating = 6;
    view.equipment.push_back(
      Elite::FittedItem{.kind = Elite::FittedKind::BeamLaser, .count = 2, .mounts = Elite::MOUNT_AFT | Elite::MOUNT_LEFT});
    view.refugeesTonnes = 3;
    view.usedTonnes = 9;
    GoldenImage::Check("D08-Status-Fugitive", Drawn(view));
  }

  // The equipment list's words: the missiles with their count, a laser with its mounts in D03's order, front, left, right and
  // rear (D104), and an item with none.
  TEST_METHOD(EachItemIsWorded)
  {
    Assert::AreEqual(std::wstring(L"Missiles (3)"),
                     Outpost::DockedStatus::ItemText(Elite::FittedItem{.kind = Elite::FittedKind::Missiles, .count = 3, .mounts = 0}));
    Assert::AreEqual(
      std::wstring(L"Pulse Laser (front)"),
      Outpost::DockedStatus::ItemText(Elite::FittedItem{.kind = Elite::FittedKind::PulseLaser, .count = 1, .mounts = Elite::MOUNT_FORE}));
    Assert::AreEqual(std::wstring(L"Beam Laser (front, left, right, rear)"),
                     Outpost::DockedStatus::ItemText(Elite::FittedItem{.kind = Elite::FittedKind::BeamLaser, .count = 4, .mounts = 0x0F}));
    Assert::AreEqual(std::wstring(L"Galactic Hyperdrive"), Outpost::DockedStatus::ItemText(Elite::FittedItem{
                                                             .kind = Elite::FittedKind::GalacticHyperdrive, .count = 1, .mounts = 0}));
  }
};

} // namespace OutpostTests
