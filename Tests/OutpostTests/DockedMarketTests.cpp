#include "pch.h"

#include "Canvas.h"
#include "DockedFrame.h"
#include "DockedMarket.h"
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

// Lave's market as `docked/01-market.png` draws it: its prices, its stock, 1250.0 credits, 4 t of food, 2 t of computers and 6 g of
// gem-stones in the hold, food selected and a quantity of 1.
[[nodiscard]] Elite::MarketView AsDrawn()
{
  constexpr std::array<std::uint16_t, Elite::COMMODITY_KINDS> BUY = {28,  63,  203, 93,  264, 972, 717, 937, 621,
                                                                     382, 796, 664, 115, 369, 675, 215, 589};
  constexpr std::array<std::uint16_t, Elite::COMMODITY_KINDS> SELL = {27,  61,  196, 90,  255, 941, 694, 907, 601,
                                                                      370, 771, 643, 111, 357, 653, 208, 570};
  constexpr std::array<std::uint8_t, Elite::COMMODITY_KINDS> STOCK = {11, 21, 18, 0, 19, 12, 9, 27, 0, 18, 23, 16, 5, 0, 0, 14, 0};
  Elite::MarketView view;
  view.sequence = 1;
  view.systemName = "LAVE";
  view.creditsTenths = 12500;
  view.buyTenths = BUY;
  view.sellTenths = SELL;
  view.stock = STOCK;
  view.held[0] = 4;
  view.held[7] = 2;
  view.held[15] = 6;
  view.selected = 0;
  view.quantity = 1;
  view.buyAllowed = true;
  view.sellAllowed = true;
  view.usedTonnes = 6;
  view.bayTonnes = 20;
  return view;
}

// _view drawn on WARP at 1920 x 1080, the pointer at _pointer.
[[nodiscard]] GoldenImage::Picture Drawn(const Elite::MarketView& _view, std::optional<Engine::Point> _pointer)
{
  Engine::Renderer renderer;
  Assert::IsTrue(
    renderer.Create(Engine::Renderer::Desc{.warp = true, .sourceWidth = 4, .sourceHeight = 2, .logicalWidth = 8, .logicalHeight = 4}),
    L"WARP");
  Assert::IsTrue(renderer.HasBahnschrift(), L"Bahnschrift installed: the design's face");
  Engine::Canvas canvas;
  const Outpost::TextMeasure measure = [&renderer](const Engine::CanvasText& _text) { return renderer.MeasureText(_text); };
  Outpost::DockedMarket::Draw(_view, _pointer, measure, canvas);
  GoldenImage::Picture picture{.width = DESIGN_WIDTH_PIXELS,
                               .height = DESIGN_HEIGHT_PIXELS,
                               .pixels = renderer.Capture({}, BACKGROUND, DESIGN_WIDTH_PIXELS, DESIGN_HEIGHT_PIXELS, nullptr, &canvas)};
  Assert::AreEqual(std::size_t{DESIGN_WIDTH_PIXELS} * DESIGN_HEIGHT_PIXELS, picture.pixels.size(), L"the whole picture captured");
  return picture;
}

[[nodiscard]] bool Is(const std::optional<Elite::MarketClick>& _click, Elite::MarketControl _control, std::uint8_t _row = 0)
{
  return _click && _click->control == _control && _click->row == _row;
}

} // namespace

// D01, the market, as the shell draws it (ADR-058, D82): on WARP, against its golden images (§6.5, D94); where a click lands
// (D83); and its strips' words, as D11 has them.
TEST_CLASS(DockedMarketTests)
{
public:
  // The screen as `docked/01-market.png` draws it.
  TEST_METHOD(TheMarketAsDrawn)
  {
    GoldenImage::Check("D01-Market", Drawn(AsDrawn(), std::nullopt));
  }

  // What the drawing leaves to build: a refusal's strip, [B] BUY off for a trade it would refuse, the pointer on [S] SELL, and a
  // row under the pointer.
  TEST_METHOD(TheMarketsStates)
  {
    Elite::MarketView view = AsDrawn();
    view.selected = 7;
    view.quantity = 12;
    view.buyAllowed = false;
    view.sellAllowed = false;
    view.message = Elite::MarketMessage{
      .notice = Elite::MarketNotice::NotEnoughCredits, .commodity = 7, .amount = 12, .tenths = 11244, .creditsTenths = 12500};
    GoldenImage::Check("D01-Market-States", Drawn(view, Engine::Point{.x = 1700.0f, .y = 460.0f}));
    view.message = Elite::MarketMessage{.notice = Elite::MarketNotice::Bought, .commodity = 0, .amount = 3, .tenths = 84};
    GoldenImage::Check("D01-Market-Bought", Drawn(view, Engine::Point{.x = 300.0f, .y = 400.0f}));
  }

  // A click on a row selects it, on −, +, MAX, [B] BUY and [S] SELL presses it, and anywhere else does nothing; the key bar is
  // the frame's.
  TEST_METHOD(AClickFindsItsControl)
  {
    Assert::IsTrue(Is(Outpost::DockedMarket::ControlAt(Engine::Point{.x = 100.0f, .y = 196.0f}), Elite::MarketControl::Row, 0), L"Food");
    Assert::IsTrue(Is(Outpost::DockedMarket::ControlAt(Engine::Point{.x = 1300.0f, .y = 870.0f}), Elite::MarketControl::Row, 16),
                   L"Alien Items");
    Assert::IsFalse(Outpost::DockedMarket::ControlAt(Engine::Point{.x = 100.0f, .y = 930.0f}).has_value(), L"under the last row");
    Assert::IsTrue(Is(Outpost::DockedMarket::ControlAt(Engine::Point{.x = 1580.0f, .y = 393.0f}), Elite::MarketControl::Less));
    Assert::IsFalse(Outpost::DockedMarket::ControlAt(Engine::Point{.x = 1658.0f, .y = 393.0f}).has_value(), L"the quantity itself");
    Assert::IsTrue(Is(Outpost::DockedMarket::ControlAt(Engine::Point{.x = 1735.0f, .y = 393.0f}), Elite::MarketControl::More));
    Assert::IsTrue(Is(Outpost::DockedMarket::ControlAt(Engine::Point{.x = 1798.0f, .y = 393.0f}), Elite::MarketControl::Most));
    Assert::IsTrue(Is(Outpost::DockedMarket::ControlAt(Engine::Point{.x = 1513.0f, .y = 468.0f}), Elite::MarketControl::Buy));
    Assert::IsTrue(Is(Outpost::DockedMarket::ControlAt(Engine::Point{.x = 1727.0f, .y = 468.0f}), Elite::MarketControl::Sell));
    Assert::IsTrue(Outpost::DockedFrame::KeyAt(Engine::Point{.x = 753.0f, .y = 1036.0f}) == Outpost::DockedKey::Market, L"F2 MARKET");
    Assert::IsTrue(Outpost::DockedFrame::KeyAt(Engine::Point{.x = 503.0f, .y = 1036.0f}) == Outpost::DockedKey::Disc, L"ESC DISC");
    Assert::IsFalse(Outpost::DockedFrame::KeyAt(Engine::Point{.x = 550.0f, .y = 1036.0f}).has_value(), L"between two keys");
  }

  // The strips of D11's Market column, word for word, as headline and detail.
  TEST_METHOD(TheStripsSayWhatD11Says)
  {
    const auto words = [](const Elite::MarketMessage& _message)
    {
      const Outpost::DockedMarket::Words found = Outpost::DockedMarket::WordsOf(_message);
      return found.detail.empty() ? found.headline : found.headline + L" — " + found.detail;
    };
    Assert::AreEqual(std::wstring(L"NOT ENOUGH CREDITS — 281.1 CR NEEDED, 41.2 CR AVAILABLE"),
                     words(Elite::MarketMessage{.notice = Elite::MarketNotice::NotEnoughCredits, .tenths = 2811, .creditsTenths = 412}));
    Assert::AreEqual(std::wstring(L"CARGO HOLD FULL — 2 T FREE OF 20 T"),
                     words(Elite::MarketMessage{.notice = Elite::MarketNotice::HoldFull, .freeUnits = 2, .capacityUnits = 20}));
    Assert::AreEqual(std::wstring(L"ONLY 9 T IN STOCK — YOU ASKED FOR 12 T"),
                     words(Elite::MarketMessage{.notice = Elite::MarketNotice::NotEnoughInStock, .amount = 12, .stock = 9}));
    Assert::AreEqual(std::wstring(L"ALIEN ITEMS — NOT FOR SALE AT THIS STATION"),
                     words(Elite::MarketMessage{.notice = Elite::MarketNotice::NotForSale, .commodity = 16}));
    Assert::AreEqual(std::wstring(L"ONLY 2 T COMPUTERS IN THE HOLD"),
                     words(Elite::MarketMessage{.notice = Elite::MarketNotice::NotEnoughHeld, .commodity = 7, .amount = 3, .held = 2}));
    Assert::AreEqual(std::wstring(L"BOUGHT 3 T FOOD — 8.4 CR"),
                     words(Elite::MarketMessage{.notice = Elite::MarketNotice::Bought, .commodity = 0, .amount = 3, .tenths = 84}));
  }
};

} // namespace OutpostTests
