#include "pch.h"

#include "DiscScreen.h"
#include "GameRig.h"
#include "Input.h"
#include "Replay.h"
#include "Repository.h"
#include "SaveLoad.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The keys the tests press, as scan codes.
constexpr std::uint8_t SCAN_ESCAPE = 0x01;
constexpr std::uint8_t SCAN_1 = 0x02;
constexpr std::uint8_t SCAN_3 = 0x04;
constexpr std::uint8_t SCAN_6 = 0x07;
constexpr std::uint8_t SCAN_BACKSPACE = 0x0E;
constexpr std::uint8_t SCAN_Y = 0x15;
constexpr std::uint8_t SCAN_ENTER = 0x1C;
constexpr std::uint8_t SCAN_SEMICOLON = 0x27;
constexpr std::uint8_t SCAN_N = 0x31;
constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_UP = 0x48;
constexpr std::uint8_t SCAN_DOWN = 0x50;

// The cash of the prepared commanders in Replays/: JAMESON's 100.0 credits, and the 30 000.0 of the one rich at Leesti.
constexpr std::uint32_t JAMESON_CREDITS_TENTHS = 1000;
constexpr std::uint32_t LEESTI_CREDITS_TENTHS = 300000;

// _key pressed on D10 while _prompt waits.
[[nodiscard]] Elite::DiscInput Key(std::uint8_t _scanCode, Elite::DiscPrompt _prompt = Elite::DiscPrompt::None, bool _shift = false)
{
  return Elite::DiscKey(Elite::KeyPress{.taken = true, .scanCode = _scanCode, .shift = _shift}, _prompt);
}

// A file of the catalogue, with no date and _creditsTenths.
[[nodiscard]] Elite::DiscFile File(std::string_view _name, std::uint32_t _creditsTenths = JAMESON_CREDITS_TENTHS)
{
  Elite::DiscFile file;
  file.name = std::string(_name);
  file.creditsTenths = _creditsTenths;
  return file;
}

// What D10 knows: _files on the disc, the keyboard steering, no mouse unless _mouse, and JAMESON, the commander's name as the
// game holds it.
[[nodiscard]] Elite::DiscFacts Facts(std::vector<Elite::DiscFile> _files, bool _mouse = false)
{
  return Elite::DiscFacts{.files = std::move(_files), .deviceInUse = 0, .mouse = _mouse, .commanderName = "JAMESON "};
}

// _screen after _input with _facts.
[[nodiscard]] Elite::DiscStep Step(const Elite::DiscScreen& _screen, const Elite::DiscInput& _input, const Elite::DiscFacts& _facts)
{
  return Elite::StepDisc(_screen, _input, _facts);
}

// _action alone.
[[nodiscard]] Elite::DiscInput Do(Elite::DiscAction _action)
{
  return Elite::DiscInput{.action = _action};
}

// Item _item chosen.
[[nodiscard]] Elite::DiscInput ChooseItem(Elite::DiscItem _item)
{
  return Elite::DiscInput{.action = Elite::DiscAction::Choose, .item = _item};
}

// _character typed into the name.
[[nodiscard]] Elite::DiscInput Type(char _character)
{
  return Elite::DiscInput{.action = Elite::DiscAction::Type, .character = static_cast<std::uint8_t>(_character)};
}

// _steps, in Replay.h's syntax, played on _rig; each must run to its end.
void Play(GameRig& _rig, std::string_view _steps)
{
  std::vector<Elite::Step> steps;
  std::string error;
  Assert::IsTrue(Elite::ParseSteps(_steps, steps, error), std::wstring(error.begin(), error.end()).c_str());
  Elite::ReplayPlayer player(_rig.Host(), _rig.State());
  for (const Elite::Step& step : steps)
  {
    Elite::Checkpoint checkpoint;
    Assert::IsTrue(player.Play(step, checkpoint) == Elite::ExpectedStop(step), L"the step runs");
  }
}

// D10 shown, which must be.
[[nodiscard]] const Elite::DiscView& Shown(GameRig& _rig)
{
  const Elite::DiscView* const view = _rig.Host().ShownDisc();
  Assert::IsTrue(view != nullptr, L"D10 is shown");
  static const Elite::DiscView NONE;
  return view != nullptr ? *view : NONE;
}

// Replays/_source written to _rig's disc as _name, stamped _stamp, as the game saves a file.
void WriteCommander(GameRig& _rig, std::string_view _name, std::string_view _source, const Elite::FileStamp& _stamp)
{
  const std::filesystem::path source = Elite::FindInRepository(std::filesystem::path("Replays") / _source);
  Assert::IsFalse(source.empty(), L"the commander in Replays/");
  const std::vector<std::uint8_t> bytes = Elite::ReadWholeFile(source);
  std::unique_ptr<Elite::OpenFile> file;
  Assert::IsTrue(_rig.Store().Create(_name, 0, _stamp, file) == Elite::DosError::None, L"created");
  std::uint32_t count = 0;
  Assert::IsTrue(file->Write(bytes, _stamp, count) == Elite::DosError::None && count == bytes.size(), L"written");
  file.reset();
  Assert::IsTrue(_rig.Store().SetAttribute(_name, 0) == Elite::DosError::None, L"its attribute cleared");
}

// From power-on to the status screen at Lave, as a new game docks.
constexpr std::string_view TO_THE_STATUS_SCREEN = "wait 3\nkey space; wait 4";

} // namespace

// D10, Disc access (D102, D117 to D119, ADR-067): its keys and steps as values, the catalogue as the disc holds it, and the
// screen as the game runs it docked, through Start's disk work.
TEST_CLASS(DiscScreenTests)
{
public:
  // 1 to 6 choose, the arrows move, Enter acts and Esc goes back; in the name, its letters and digits, upper case, Backspace, Enter
  // and Esc; at a question, Y, N and Esc, and Enter does not answer it. The F-keys are the screens'.
  TEST_METHOD(TheKeysAreTheScreensKeys)
  {
    const Elite::DiscInput load = Key(SCAN_1);
    Assert::IsTrue(load.action == Elite::DiscAction::Choose && load.item == Elite::DiscItem::Load, L"1 Load commander");
    Assert::IsTrue(Key(SCAN_6).item == Elite::DiscItem::Device, L"6 Input device");
    Assert::IsTrue(Key(SCAN_UP).action == Elite::DiscAction::Up && Key(SCAN_DOWN).action == Elite::DiscAction::Down, L"the arrows");
    Assert::IsTrue(Key(SCAN_ENTER).action == Elite::DiscAction::Act, L"Enter");
    Assert::IsTrue(Key(SCAN_ESCAPE).action == Elite::DiscAction::Back, L"Esc");
    Assert::IsTrue(Key(SCAN_Y).action == Elite::DiscAction::None, L"Y asks nothing without a question");
    Assert::IsTrue(Key(SCAN_F1).action == Elite::DiscAction::None, L"F1 is the screens'");

    const Elite::DiscInput typed = Key(SCAN_Y, Elite::DiscPrompt::Name);
    Assert::IsTrue(typed.action == Elite::DiscAction::Type && typed.character == 'Y', L"a letter, upper case");
    const Elite::DiscInput digit = Key(SCAN_3, Elite::DiscPrompt::Name);
    Assert::IsTrue(digit.action == Elite::DiscAction::Type && digit.character == '3', L"a digit is typed, not chosen");
    Assert::IsTrue(Key(SCAN_SEMICOLON, Elite::DiscPrompt::Name).action == Elite::DiscAction::None, L"no other character");
    Assert::IsTrue(Key(SCAN_BACKSPACE, Elite::DiscPrompt::Name).action == Elite::DiscAction::Erase, L"Backspace");
    Assert::IsTrue(Key(SCAN_ENTER, Elite::DiscPrompt::Name).action == Elite::DiscAction::Act, L"Enter");
    Assert::IsTrue(Key(SCAN_ESCAPE, Elite::DiscPrompt::Name).action == Elite::DiscAction::Back, L"Esc");

    Assert::IsTrue(Key(SCAN_Y, Elite::DiscPrompt::Delete).action == Elite::DiscAction::Yes, L"Y");
    Assert::IsTrue(Key(SCAN_N, Elite::DiscPrompt::Delete).action == Elite::DiscAction::No, L"N");
    Assert::IsTrue(Key(SCAN_ESCAPE, Elite::DiscPrompt::Quit).action == Elite::DiscAction::Back, L"Esc");
    Assert::IsTrue(Key(SCAN_ENTER, Elite::DiscPrompt::Overwrite).action == Elite::DiscAction::None, L"Enter answers nothing");
    Assert::IsTrue(Key(SCAN_1, Elite::DiscPrompt::Retry).action == Elite::DiscAction::None, L"nor does an item");

    Assert::IsTrue(Elite::DiscClickInput(Elite::DiscClick{.target = Elite::DiscTarget::Item, .index = 4}).item == Elite::DiscItem::Quit,
                   L"a click on item 5");
    const Elite::DiscInput row = Elite::DiscClickInput(Elite::DiscClick{.target = Elite::DiscTarget::Row, .index = 2});
    Assert::IsTrue(row.action == Elite::DiscAction::Pick && row.index == 2, L"a click on a row picks it");
    Assert::IsTrue(Elite::DiscClickInput(Elite::DiscClick{.target = Elite::DiscTarget::Primary}).action == Elite::DiscAction::Primary,
                   L"the lit button");
  }

  // The catalogue: the arrows held to its rows, a click on a row, Enter loading the one selected, and Enter on an empty disc
  // saying so. Esc with no question goes back to the status screen (D119).
  TEST_METHOD(LoadPicksFromTheCatalogue)
  {
    const Elite::DiscFacts facts = Facts({File("ELITE01.CDR"), File("JAMESON.CDR"), File("TRADER.CDR")});
    Elite::DiscScreen screen;
    screen = Step(screen, Do(Elite::DiscAction::Up), facts).screen;
    Assert::AreEqual(std::size_t{0}, screen.file, L"Up stops at the first");
    for (int down = 0; down < 5; ++down)
    {
      screen = Step(screen, Do(Elite::DiscAction::Down), facts).screen;
    }
    Assert::AreEqual(std::size_t{2}, screen.file, L"Down stops at the last");
    screen = Step(screen, Elite::DiscInput{.action = Elite::DiscAction::Pick, .index = 7}, facts).screen;
    Assert::AreEqual(std::size_t{2}, screen.file, L"a click past the rows picks nothing");
    screen = Step(screen, Elite::DiscInput{.action = Elite::DiscAction::Pick, .index = 1}, facts).screen;
    const Elite::DiscStep loaded = Step(screen, Do(Elite::DiscAction::Act), facts);
    Assert::IsTrue(loaded.request == Elite::DiscRequest::Load && loaded.screen.subject == "JAMESON.CDR", L"Enter loads JAMESON");
    Assert::IsTrue(Step(screen, Do(Elite::DiscAction::Primary), facts).request == Elite::DiscRequest::Load, L"as the button does");

    const Elite::DiscStep empty = Step(Elite::DiscScreen{}, Do(Elite::DiscAction::Act), Facts({}));
    Assert::IsTrue(empty.request == Elite::DiscRequest::None && empty.screen.note == Elite::DiscNote::NoFiles, L"nothing to load");
    Assert::IsTrue(Step(screen, Do(Elite::DiscAction::Back), facts).request == Elite::DiscRequest::Leave, L"Esc leaves");
    const Elite::DiscStep catalogued = Step(screen, ChooseItem(Elite::DiscItem::Catalogue), facts);
    Assert::IsTrue(catalogued.request == Elite::DiscRequest::Catalogue && catalogued.screen.note == Elite::DiscNote::Catalogued,
                   L"4 reads the disc again");
  }

  // Save commander: the name starts as the commander's, takes eight letters or digits, refuses one that starts with a digit,
  // and asks before it overwrites a file (D118); Esc in the name goes back to Load commander, N at the question back to the name.
  TEST_METHOD(SaveTakesANameAndAsksBeforeOverwriting)
  {
    const Elite::DiscFacts facts = Facts({File("JAMESON.CDR")});
    Elite::DiscScreen screen = Step(Elite::DiscScreen{}, ChooseItem(Elite::DiscItem::Save), facts).screen;
    Assert::IsTrue(screen.prompt == Elite::DiscPrompt::Name && screen.typed == "JAMESON", L"the commander's name, without its space");
    const Elite::DiscStep asked = Step(screen, Do(Elite::DiscAction::Act), facts);
    Assert::IsTrue(asked.request == Elite::DiscRequest::None && asked.screen.prompt == Elite::DiscPrompt::Overwrite, L"on the disc: asked");
    Assert::AreEqual("JAMESON.CDR", asked.screen.subject.c_str(), L"about JAMESON.CDR");
    Assert::IsTrue(Step(asked.screen, Do(Elite::DiscAction::Act), facts).request == Elite::DiscRequest::None, L"Enter is no Y");
    Assert::IsTrue(Step(asked.screen, Do(Elite::DiscAction::Yes), facts).request == Elite::DiscRequest::Save, L"Y saves");
    Assert::IsTrue(Step(asked.screen, Do(Elite::DiscAction::Primary), facts).request == Elite::DiscRequest::Save, L"as its button does");
    const Elite::DiscScreen declined = Step(asked.screen, Do(Elite::DiscAction::No), facts).screen;
    Assert::IsTrue(declined.prompt == Elite::DiscPrompt::Name && declined.typed == "JAMESON", L"N, back to the name");

    screen = Step(declined, Type('X'), facts).screen;
    Assert::AreEqual("JAMESONX", screen.typed.c_str(), L"eight");
    screen = Step(screen, Type('Y'), facts).screen;
    Assert::AreEqual("JAMESONX", screen.typed.c_str(), L"and no more");
    const Elite::DiscStep saved = Step(screen, Do(Elite::DiscAction::Act), facts);
    Assert::IsTrue(saved.request == Elite::DiscRequest::Save && saved.screen.subject == "JAMESONX.CDR", L"a new name saves at once");

    for (int erase = 0; erase < 10; ++erase)
    {
      screen = Step(screen, Do(Elite::DiscAction::Erase), facts).screen;
    }
    Assert::IsTrue(screen.typed.empty() && screen.prompt == Elite::DiscPrompt::Name, L"erased to empty, and still asking");
    Assert::IsTrue(Step(screen, Do(Elite::DiscAction::Act), facts).screen.note == Elite::DiscNote::BadName, L"no name");
    screen = Step(screen, Type('1'), facts).screen;
    const Elite::DiscStep bad = Step(screen, Do(Elite::DiscAction::Act), facts);
    Assert::IsTrue(bad.request == Elite::DiscRequest::None && bad.screen.note == Elite::DiscNote::BadName, L"a name starts with a letter");
    const Elite::DiscScreen canceled = Step(bad.screen, Do(Elite::DiscAction::Back), facts).screen;
    Assert::IsTrue(canceled.item == Elite::DiscItem::Load && canceled.prompt == Elite::DiscPrompt::None, L"Esc, Load commander");
  }

  // Delete file asks about the file selected; Quit game asks; Input device takes the mouse only when one is attached.
  TEST_METHOD(DeleteQuitAndTheDeviceAsk)
  {
    const Elite::DiscFacts facts = Facts({File("JAMESON.CDR"), File("LEESTI.CDR", LEESTI_CREDITS_TENTHS)});
    Elite::DiscScreen screen = Step(Elite::DiscScreen{}, ChooseItem(Elite::DiscItem::Delete), facts).screen;
    screen = Step(screen, Do(Elite::DiscAction::Down), facts).screen;
    const Elite::DiscStep asked = Step(screen, Do(Elite::DiscAction::Act), facts);
    Assert::IsTrue(asked.screen.prompt == Elite::DiscPrompt::Delete && asked.screen.subject == "LEESTI.CDR", L"delete LEESTI.CDR?");
    Assert::IsTrue(Step(asked.screen, Do(Elite::DiscAction::Down), facts).screen.file == 1, L"the selection holds while it asks");
    Assert::IsTrue(Step(asked.screen, Do(Elite::DiscAction::Yes), facts).request == Elite::DiscRequest::Delete, L"Y deletes");
    const Elite::DiscScreen kept = Step(asked.screen, Do(Elite::DiscAction::Back), facts).screen;
    Assert::IsTrue(kept.item == Elite::DiscItem::Delete && kept.prompt == Elite::DiscPrompt::None, L"Esc, back to the list");

    const Elite::DiscScreen quit = Step(kept, ChooseItem(Elite::DiscItem::Quit), facts).screen;
    Assert::IsTrue(quit.prompt == Elite::DiscPrompt::Quit, L"5 asks");
    Assert::IsTrue(Step(quit, Do(Elite::DiscAction::Yes), facts).request == Elite::DiscRequest::Quit, L"Y quits");
    const Elite::DiscScreen stayed = Step(quit, Do(Elite::DiscAction::No), facts).screen;
    Assert::IsTrue(stayed.item == Elite::DiscItem::Load && stayed.prompt == Elite::DiscPrompt::None, L"N, back to Load commander");

    Elite::DiscScreen device = Step(stayed, ChooseItem(Elite::DiscItem::Device), facts).screen;
    Assert::AreEqual(std::uint8_t{0}, device.device, L"on the keyboard, which steers");
    device = Step(device, Do(Elite::DiscAction::Down), facts).screen;
    const Elite::DiscStep noMouse = Step(device, Do(Elite::DiscAction::Act), facts);
    Assert::IsTrue(noMouse.request == Elite::DiscRequest::None && noMouse.screen.note == Elite::DiscNote::NoMouse, L"no mouse");
    const Elite::DiscStep mouse = Step(device, Do(Elite::DiscAction::Act), Facts({}, true));
    Assert::IsTrue(mouse.request == Elite::DiscRequest::Device && mouse.screen.note == Elite::DiscNote::Mouse, L"the mouse, attached");
  }

  // The catalogue lists the .CDR files with no attribute, by name, each with the day its stamp holds and the cash of the commander
  // in it; a file too short to hold one has no cash.
  TEST_METHOD(TheCatalogueReadsStampsAndCash)
  {
    GameRig rig("DiscCatalogue");
    WriteCommander(rig, "LEESTI.CDR", "rich-at-leesti.cdr", Elite::StampOf(Elite::DosDateTime{1987, 9, 28, 10, 0, 0, 0}));
    WriteCommander(rig, "JAMESON.CDR", "jameson.cdr", Elite::StampOf(Elite::DosDateTime{1987, 10, 14, 9, 30, 0, 0}));
    WriteCommander(rig, "NOTES.TXT", "jameson.cdr", Elite::StampOf(Elite::DosDateTime{}));
    WriteCommander(rig, "HIDDEN.CDR", "jameson.cdr", Elite::StampOf(Elite::DosDateTime{}));
    Assert::IsTrue(rig.Store().SetAttribute("HIDDEN.CDR", Elite::FileStore::ATTRIBUTE_HIDDEN) == Elite::DosError::None, L"hidden");
    std::unique_ptr<Elite::OpenFile> file;
    const Elite::FileStamp stamp{};
    Assert::IsTrue(rig.Store().Create("SHORT.CDR", 0, stamp, file) == Elite::DosError::None, L"created");
    std::uint32_t count = 0;
    const std::vector<std::uint8_t> bytes(10, 0x41);
    Assert::IsTrue(file->Write(bytes, stamp, count) == Elite::DosError::None, L"written");
    file.reset();
    Assert::IsTrue(rig.Store().SetAttribute("SHORT.CDR", 0) == Elite::DosError::None, L"its attribute cleared");

    const std::vector<Elite::DiscFile> files = Elite::CommanderFilesOf(rig.Store());
    Assert::AreEqual(std::size_t{3}, files.size(), L"the three .CDR files with no attribute");
    Assert::AreEqual("JAMESON.CDR", files[0].name.c_str(), L"by name");
    Assert::IsTrue(files[0].year == 1987 && files[0].month == 10 && files[0].day == 14, L"saved on 14-10-87");
    Assert::IsTrue(files[0].creditsTenths == JAMESON_CREDITS_TENTHS, L"with 100.0 credits");
    Assert::AreEqual("LEESTI.CDR", files[1].name.c_str());
    Assert::IsTrue(files[1].year == 1987 && files[1].month == 9 && files[1].day == 28, L"28-09-87");
    Assert::IsTrue(files[1].creditsTenths == LEESTI_CREDITS_TENTHS, L"with 30 000.0");
    Assert::AreEqual("SHORT.CDR", files[2].name.c_str());
    Assert::IsFalse(files[2].creditsTenths.has_value(), L"too short to hold a commander");
  }

  // Docked, Esc opens D10 on Load commander with the catalogue. A save leaves for Start and comes back saved, selected; a load
  // comes back loaded and the name the commander's; a load the disc fails asks whether to retry, and N says it failed. F1
  // closes D10, and Esc goes back to the status screen.
  TEST_METHOD(DiscAccessRunsDocked)
  {
    GameRig rig("DiscAccess");
    WriteCommander(rig, "LEESTI.CDR", "rich-at-leesti.cdr", Elite::StampOf(Elite::DosDateTime{1987, 9, 28, 10, 0, 0, 0}));
    Play(rig, TO_THE_STATUS_SCREEN);
    Play(rig, "key Escape; wait 0.2");
    const Elite::DiscView& opened = Shown(rig);
    Assert::IsTrue(opened.item == Elite::DiscItem::Load && opened.prompt == Elite::DiscPrompt::None, L"on Load commander");
    Assert::AreEqual(std::size_t{1}, opened.files.size(), L"LEESTI alone");
    Assert::AreEqual(std::uint32_t{1000}, opened.creditsTenths, L"the commander's cash");

    Play(rig, "key 2; wait 0.05; key BackSpace; key BackSpace; key BackSpace; key BackSpace; key BackSpace; key BackSpace;"
              " key BackSpace; key a; key b; key 1; wait 0.05");
    Assert::IsTrue(Shown(rig).prompt == Elite::DiscPrompt::Name && Shown(rig).typed == "AB1", L"typing");
    Play(rig, "key Return; wait 1");
    const Elite::DiscView& saved = Shown(rig);
    Assert::IsTrue(saved.note == Elite::DiscNote::Saved && saved.subject == "AB1.CDR", L"back from Start, saved");
    Assert::AreEqual(std::size_t{2}, saved.files.size(), L"two files");
    Assert::AreEqual("AB1.CDR", saved.files[saved.selected].name.c_str(), L"the one saved selected");

    Play(rig, "key Down; wait 0.05; key Return; wait 1");
    const Elite::DiscView& loaded = Shown(rig);
    Assert::IsTrue(loaded.note == Elite::DiscNote::Loaded && loaded.subject == "LEESTI.CDR", L"LEESTI loaded");
    Assert::AreEqual(LEESTI_CREDITS_TENTHS, loaded.creditsTenths, L"LEESTI's cash");
    Assert::IsTrue(Elite::TextWithin(rig.State().Data().commander.defaultCommanderName, 0) == "LEESTI", L"and LEESTI the commander's name");

    Assert::IsTrue(rig.Store().Delete("AB1.CDR") == Elite::DosError::None, L"AB1 gone behind the catalogue's back");
    Play(rig, "key Up; wait 0.05; key Return; wait 1");
    Assert::IsTrue(Shown(rig).prompt == Elite::DiscPrompt::Retry && Shown(rig).subject == "AB1.CDR", L"the disc failed: retry?");
    Play(rig, "key Return; wait 0.05\nkey F1; wait 0.05");
    Assert::IsTrue(Shown(rig).prompt == Elite::DiscPrompt::Retry, L"Enter and F1 answer nothing");
    Play(rig, "key n; wait 0.05");
    Assert::IsTrue(Shown(rig).note == Elite::DiscNote::Failed && Shown(rig).prompt == Elite::DiscPrompt::None, L"N: it failed");
    Assert::AreEqual(std::size_t{1}, Shown(rig).files.size(), L"the disc as it is");

    rig.Host().Click(Elite::DiscClick{.target = Elite::DiscTarget::Item, .index = 5});
    Play(rig, "wait 0.05");
    Assert::IsTrue(Shown(rig).item == Elite::DiscItem::Device, L"a click on Input device");
    rig.Host().Click(Elite::DiscClick{.target = Elite::DiscTarget::Back});
    Play(rig, "wait 0.2");
    Assert::IsTrue(rig.Host().ShownDisc() == nullptr, L"ESC \u00B7 BACK closes it");
    Assert::IsTrue(rig.Host().ShownStatus() != nullptr, L"to the status screen");
    Assert::AreEqual(std::uint8_t{0}, rig.State().Data().saveLoad.resumeAtDiskMenu, L"and D10 no longer opens at the dock");
    rig.Host().Click(Elite::DiscClick{});
    Assert::IsFalse(rig.Host().TakeDiscClick().has_value(), L"a click with D10 closed is dropped");

    Play(rig, "key Escape; wait 0.2\nkey F1; wait 1");
    Assert::IsTrue(rig.Host().ShownDisc() == nullptr && rig.State().Data().flow.inFlight == 1, L"F1 launches from D10");
  }

  // Quit game, Y: the program ends (D80).
  TEST_METHOD(QuitEndsTheGame)
  {
    GameRig rig("DiscQuit");
    Play(rig, TO_THE_STATUS_SCREEN);
    Play(rig, "key Escape; wait 0.2\nkey 5; wait 0.05");
    Assert::IsTrue(Shown(rig).prompt == Elite::DiscPrompt::Quit, L"quit?");
    Play(rig, "key y; end 1");
    Assert::AreEqual(Elite::DISK_QUIT, rig.State().Data().saveLoad.diskOperation, L"Start's request: the quit");
  }
};

} // namespace GameLogicTests
