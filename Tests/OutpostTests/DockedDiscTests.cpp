#include "pch.h"

#include "Canvas.h"
#include "DiscView.h"
#include "DockedDisc.h"
#include "GoldenImage.h"
#include "Renderer.h"

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

// A file of the catalogue, saved on _day-_month-87 with _creditsTenths.
[[nodiscard]] Elite::DiscFile File(const char* _name, std::uint8_t _day, std::uint8_t _month, std::uint32_t _creditsTenths)
{
  Elite::DiscFile file;
  file.name = _name;
  file.year = 1987;
  file.month = _month;
  file.day = _day;
  file.creditsTenths = _creditsTenths;
  return file;
}

// D10 as `docked/10-disc-menu.png` draws it: docked at Lave with 1250.0 credits, Catalogue disc chosen, and four files, JAMESON's
// selected.
[[nodiscard]] Elite::DiscView AsDrawn()
{
  Elite::DiscView view;
  view.sequence = 1;
  view.systemName = "LAVE";
  view.creditsTenths = 12500;
  view.item = Elite::DiscItem::Catalogue;
  view.files = {File("JAMESON.CDR", 14, 10, 12500), File("ELITE01.CDR", 9, 10, 234186), File("TRADER.CDR", 2, 10, 6124),
                File("DEADLY.CDR", 28, 9, 1047721)};
  return view;
}

// _view drawn on WARP at the design's size, in the frame.
[[nodiscard]] GoldenImage::Picture Drawn(const Elite::DiscView& _view)
{
  Engine::Renderer renderer;
  Assert::IsTrue(renderer.Create(Engine::Renderer::Desc{.warp = true}), L"WARP");
  Assert::IsTrue(renderer.HasBahnschrift(), L"Bahnschrift installed: the design's face");
  Engine::Canvas canvas;
  const Outpost::TextMeasure measure = [&renderer](const Engine::CanvasText& _text) { return renderer.MeasureText(_text); };
  Outpost::DockedDisc::Draw(_view, std::nullopt, measure, canvas);
  GoldenImage::Picture picture{.width = DESIGN_WIDTH_PIXELS,
                               .height = DESIGN_HEIGHT_PIXELS,
                               .pixels = renderer.Capture(BACKGROUND, DESIGN_WIDTH_PIXELS, DESIGN_HEIGHT_PIXELS, nullptr, &canvas)};
  Assert::AreEqual(std::size_t{DESIGN_WIDTH_PIXELS} * DESIGN_HEIGHT_PIXELS, picture.pixels.size(), L"the whole picture captured");
  return picture;
}

// Whether _click is on _target, at _index.
[[nodiscard]] bool Is(const std::optional<Elite::DiscClick>& _click, Elite::DiscTarget _target, std::size_t _index = 0)
{
  return _click && _click->target == _target && _click->index == _index;
}

} // namespace

// D10, Disc access, as the shell draws it (ADR-067, D82, D102, D117 to D119): on WARP, against its golden images (§6.5, D94); the
// control a click lands on; and the strip's, the catalogue's and the dates' words.
TEST_CLASS(DockedDiscTests)
{
public:
  // D10 as `docked/10-disc-menu.png` draws it, with the count and no free space (D102).
  TEST_METHOD(DiscAccessAsDrawn)
  {
    GoldenImage::Check("D10-Disc-Access", Drawn(AsDrawn()));
  }

  // Save commander, its name on the disc already: the question before it overwrites (D118), which no drawing shows.
  TEST_METHOD(SaveAsksBeforeOverwriting)
  {
    Elite::DiscView view = AsDrawn();
    view.item = Elite::DiscItem::Save;
    view.prompt = Elite::DiscPrompt::Overwrite;
    view.typed = "JAMESON";
    view.subject = "JAMESON.CDR";
    GoldenImage::Check("D10-Save-Overwrite", Drawn(view));
  }

  // Load commander on an empty disc, Enter refused; and a disc error, asking whether to retry: two of the states D10's plan names
  // to build.
  TEST_METHOD(AnEmptyDiscAndADiscError)
  {
    Elite::DiscView empty = AsDrawn();
    empty.item = Elite::DiscItem::Load;
    empty.files.clear();
    empty.note = Elite::DiscNote::NoFiles;
    GoldenImage::Check("D10-Empty-Catalogue", Drawn(empty));
    Elite::DiscView error = AsDrawn();
    error.item = Elite::DiscItem::Load;
    error.prompt = Elite::DiscPrompt::Retry;
    error.subject = "TRADER.CDR";
    GoldenImage::Check("D10-Disc-Error", Drawn(error));
  }

  // A click finds the item, the file's row, or the panel's buttons; the lit button does nothing on an empty disc; the rows scroll
  // to keep the selected one in sight.
  TEST_METHOD(AClickFindsTheControl)
  {
    using Outpost::DockedDisc;
    Elite::DiscView view = AsDrawn();
    Assert::IsTrue(Is(DockedDisc::ControlAt(view, Engine::Point{.x = 500.0f, .y = 272.0f}), Elite::DiscTarget::Item, 0), L"1 Load");
    Assert::IsTrue(Is(DockedDisc::ControlAt(view, Engine::Point{.x = 500.0f, .y = 632.0f}), Elite::DiscTarget::Item, 5), L"6 Input");
    Assert::IsFalse(DockedDisc::ControlAt(view, Engine::Point{.x = 500.0f, .y = 309.0f}).has_value(), L"between two items");
    Assert::IsTrue(Is(DockedDisc::ControlAt(view, Engine::Point{.x = 1000.0f, .y = 306.0f}), Elite::DiscTarget::Row, 0), L"JAMESON");
    Assert::IsTrue(Is(DockedDisc::ControlAt(view, Engine::Point{.x = 1000.0f, .y = 450.0f}), Elite::DiscTarget::Row, 3), L"DEADLY");
    Assert::IsFalse(DockedDisc::ControlAt(view, Engine::Point{.x = 1000.0f, .y = 500.0f}).has_value(), L"below the last file");
    Assert::IsTrue(Is(DockedDisc::ControlAt(view, Engine::Point{.x = 1150.0f, .y = 825.0f}), Elite::DiscTarget::Back), L"ESC BACK");
    Assert::IsTrue(Is(DockedDisc::ControlAt(view, Engine::Point{.x = 1380.0f, .y = 825.0f}), Elite::DiscTarget::Primary), L"ENTER");
    Assert::IsFalse(DockedDisc::ControlAt(view, Engine::Point{.x = 960.0f, .y = 1036.0f}).has_value(), L"the key bar is the frame's");

    view.files.clear();
    Assert::IsFalse(DockedDisc::ControlAt(view, Engine::Point{.x = 1380.0f, .y = 825.0f}).has_value(), L"nothing to load");

    for (std::uint8_t file = 0; file < 8; ++file)
    {
      view.files.push_back(File("PILOT.CDR", 1, 1, 10));
    }
    view.files.resize(12, view.files.front());
    view.selected = 10;
    Assert::AreEqual(std::size_t{3}, DockedDisc::FirstRowShown(view), L"the eight rows shown end at the selected one");
    Assert::IsTrue(Is(DockedDisc::ControlAt(view, Engine::Point{.x = 1000.0f, .y = 306.0f}), Elite::DiscTarget::Row, 3),
                   L"the top row is the fourth file");
    view.item = Elite::DiscItem::Device;
    view.device = 1;
    Assert::IsTrue(Is(DockedDisc::ControlAt(view, Engine::Point{.x = 1000.0f, .y = 354.0f}), Elite::DiscTarget::Row, 1), L"the mouse");
    Assert::IsFalse(DockedDisc::ControlAt(view, Engine::Point{.x = 1000.0f, .y = 402.0f}).has_value(), L"two devices only");
  }

  // The cash with its thousands apart, the day a file was saved, and the strip's words for a question and a note.
  TEST_METHOD(TheWordsAreTheScreens)
  {
    using Outpost::DockedDisc;
    Assert::AreEqual(std::wstring(L"104 772.1"), DockedDisc::Credits(1047721));
    Assert::AreEqual(std::wstring(L"612.4"), DockedDisc::Credits(6124));
    Assert::AreEqual(std::wstring(L"0.0"), DockedDisc::Credits(0));
    Assert::AreEqual(std::wstring(L"09-10-87"), DockedDisc::Saved(File("A.CDR", 9, 10, 0)));

    Elite::DiscView view = AsDrawn();
    view.subject = "LEESTI.CDR";
    view.prompt = Elite::DiscPrompt::Delete;
    Assert::AreEqual(std::wstring(L"DELETE LEESTI.CDR?"), DockedDisc::WordsOf(view).headline);
    view.prompt = Elite::DiscPrompt::None;
    view.note = Elite::DiscNote::Loaded;
    Assert::AreEqual(std::wstring(L"LOADED LEESTI.CDR"), DockedDisc::WordsOf(view).headline);
    view.note = Elite::DiscNote::NoMouse;
    Assert::AreEqual(std::wstring(L"NO MOUSE ATTACHED"), DockedDisc::WordsOf(view).headline);
  }
};

} // namespace OutpostTests
