#include "pch.h"

#include "Cga.h"
#include "FlightSnapshot.h"
#include "GameState.h"
#include "Platform.h"
#include "PlatformRig.h"
#include "ReferenceRig.h"
#include "Replay.h"
#include "SpaceViewLayers.h"
#include "Video.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The replay the layers are held to, every frame of its flight: a fight, with ships drawn as models, the sun and the planet,
// stardust, beams and the sights.
constexpr char REPLAY[] = "combat-orerve.replay";
constexpr std::uint64_t SLICE_MILLISECONDS = 1; // far less than a frame, so a frame's view is still on the screen
// Within this of the frame before, a frame follows it in the same flight with nothing drawn between: across the corpus a frame
// follows the one before within 51 ms, or after 1,085 ms or more where a jump, a death or a screen came between (ADR-033).
constexpr std::uint64_t MOST_FRAME_GAP_MILLISECONDS = 250;

[[nodiscard]] std::string ReadText(const std::filesystem::path& _path)
{
  std::ifstream file(_path, std::ios::binary);
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

// Pixel _x of view row _row as the screen shows it: video memory, the 320x200 mode's banks of even and odd lines.
[[nodiscard]] std::uint8_t ShownPixel(const Elite::GameState& _state, std::size_t _row, std::size_t _x)
{
  const std::size_t line = _row + Elite::SPACE_VIEW_TOP_LINES;
  const std::size_t column = Elite::SPACE_VIEW_LEFT_PIXELS + _x;
  const std::uint8_t byte = _state.VideoByte(static_cast<std::uint16_t>((line % 2) * 0x2000 + (line / 2) * 80 + column / 4));
  return static_cast<std::uint8_t>((byte >> (2 * (3 - column % 4))) & 3);
}

// A pattern of every pixel value, which differs from row to row and pixel to pixel: what a test object draws.
[[nodiscard]] std::uint8_t Pattern(std::size_t _row, std::size_t _x) noexcept
{
  return static_cast<std::uint8_t>((_row * 7 + _x * 3 + _x / 5) % 4);
}

// Pixel _x of view row _row set to _value in the drawing buffer, whose row 1 is the view's first line.
void DrawPixel(Elite::GameState& _state, std::size_t _row, std::size_t _x, std::uint8_t _value)
{
  Elite::Counted<std::uint8_t>& byte = Elite::DrawingBuffer(_state)[(_row + 1) * 64 + _x / 4];
  const auto shift = static_cast<unsigned>(2 * (3 - _x % 4));
  byte = static_cast<std::uint8_t>((std::uint8_t{byte} & ~(3u << shift)) | (unsigned{_value} << shift));
}

// What a frame's layers were found to hold, over the frames checked.
struct Seen
{
  std::size_t frames = 0;
  std::size_t models = 0;
  std::size_t underPixels = 0;
  std::size_t overPixels = 0;
  std::size_t modelPixels = 0;
  std::size_t titleModels = 0; ///< the title's turning ship, drawn as a model
  std::size_t titleFrames = 0;
};

// The latest frame's layers explain what the screen shows: where the over layer holds a pixel it is the screen's, where a model
// drew last the renderer's model shows, and everywhere else the screen shows the under layer, or the background, colour 0, where
// that is clear. And the screen is what the CGA shows of it, in the colours GraphicsColors names.
void CheckFrame(ReferenceRig& _rig, Elite::FrameImage& _image, Seen& _seen)
{
  const Elite::SpaceViewLayers& layers = _rig.Host().SpaceView();
  _rig.Host().Render(_image);
  const std::array<std::uint8_t, 4> colors = _rig.Host().GraphicsColors();
  for (std::size_t row = 0; row < Elite::SPACE_VIEW_HEIGHT_PIXELS; ++row)
  {
    for (std::size_t x = 0; x < Elite::SPACE_VIEW_WIDTH_PIXELS; ++x)
    {
      const std::size_t pixel = row * Elite::SPACE_VIEW_WIDTH_PIXELS + x;
      const std::uint8_t shown = ShownPixel(_rig.State(), row, x);
      const auto where = [&_seen, row, x](const wchar_t* _what) {
        return L"frame " + std::to_wstring(_seen.frames) + L", row " + std::to_wstring(row) + L", x " + std::to_wstring(x) + L": " + _what;
      };
      const std::uint8_t color = _image.At(static_cast<std::uint32_t>(2 * (Elite::SPACE_VIEW_LEFT_PIXELS + x)),
                                           static_cast<std::uint32_t>(Elite::SPACE_VIEW_TOP_LINES + row));
      if (color != colors[shown])
      {
        Assert::Fail(where(L"the CGA's colour").c_str());
      }
      const std::uint8_t over = layers.Over()[pixel];
      const std::uint8_t under = layers.Under()[pixel];
      _seen.underPixels += under != Elite::LAYER_CLEAR ? 1 : 0;
      if (over != Elite::LAYER_CLEAR)
      {
        ++_seen.overPixels;
        if (shown != over)
        {
          Assert::Fail(where(L"over").c_str());
        }
        continue;
      }
      if (layers.ModelOwns(pixel))
      {
        ++_seen.modelPixels;
        continue;
      }
      if (shown != (under == Elite::LAYER_CLEAR ? std::uint8_t{0} : under))
      {
        Assert::Fail(where(L"under").c_str());
      }
    }
  }
  Assert::IsTrue(_rig.Host().SpaceViewShown(), L"the screen shows the frame just published");
  const Elite::FlightSnapshot& snapshot = _rig.Host().LatestFlightSnapshot();
  // The title's dashboard is the cockpit's picture, nothing on it live, with the title on its message line; flight's is live (D64).
  Assert::AreEqual(!snapshot.title, snapshot.dashboard.live, L"the dashboard live in flight and not on the title");
  if (snapshot.title)
  {
    Assert::AreEqual(std::string("---- E L I T E ----"), std::string(snapshot.dashboard.message.data()), L"the title's message line");
    Assert::AreEqual(std::size_t{0}, snapshot.dashboard.blipCount, L"no blips on the title's scanner");
    ++_seen.titleFrames;
  }
  for (std::size_t index = 0; index < snapshot.objectCount; ++index)
  {
    _seen.models += snapshot.objects[index].model ? 1 : 0;
    _seen.titleModels += snapshot.title && snapshot.objects[index].model ? 1 : 0;
  }
  ++_seen.frames;
}

} // namespace

// The space view's layers (ADR-033): each frame's under and over layers and its models' pixels explain the screen.
TEST_CLASS(SpaceViewLayersTests)
{
public:
  // Nothing is finished before a frame of flight: both layers are clear and no slot is a model.
  TEST_METHOD(StartsClear)
  {
    const auto layers = std::make_unique<Elite::SpaceViewLayers>();
    for (const std::uint8_t pixel : layers->Under())
    {
      Assert::AreEqual(Elite::LAYER_CLEAR, pixel);
    }
    for (const std::uint8_t pixel : layers->Over())
    {
      Assert::AreEqual(Elite::LAYER_CLEAR, pixel);
    }
    Assert::IsFalse(layers->DrawnAsModel(3) || layers->ModelOwns(0));
    const auto state = std::make_unique<Elite::GameState>();
    Assert::IsFalse(layers->Shown(*state), L"no frame is shown before one is finished");
  }

  // Where the layers put a pixel, held to the game's own copy of the view to the screen and to the CGA's picture of it. Before
  // any object a row is drawn, as the title draws its words; an object that is not a model draws a pattern over the rest of the
  // view; a model draws over part of it; and a background, as the stardust is, over a row of the model. The early row is over.
  // The under layer is the pattern, pixel for pixel, but for its 0s, which changed nothing of the cleared buffer, and the
  // background's row; where the model drew last it is the model's. The screen shows them where PresentSpaceView copies them, in
  // the CGA's colours, and the view is shown in graphics mode until a byte of it changes.
  TEST_METHOD(PixelsAreWhereTheScreenShowsThem)
  {
    constexpr std::size_t MODEL_FIRST_ROW = 40;
    constexpr std::size_t MODEL_LAST_ROW = 60;
    constexpr std::uint8_t MODEL_SLOT = 3;
    constexpr std::size_t EARLY_ROW = 120;     // drawn before any object
    constexpr std::size_t BACKGROUND_ROW = 50; // a background's, over the model
    constexpr std::uint8_t EARLY_VALUE = 3;
    const auto background = [](std::size_t _row, std::size_t _x) { return static_cast<std::uint8_t>(Pattern(_row, _x) ^ 2); };
    PlatformRig rig("SpaceViewPixels");
    rig.Start(
      [&rig, &background]
      {
        Elite::Hardware& hardware = rig.Native();
        Elite::GameState& state = rig.State();
        Elite::SetGraphicsMode(hardware); // the game's mode 4, in which the CGA shows the view
        hardware.BeginSpaceViewFrame();
        for (std::size_t x = 0; x < Elite::SPACE_VIEW_WIDTH_PIXELS; ++x)
        {
          DrawPixel(state, EARLY_ROW, x, EARLY_VALUE);
        }
        hardware.BeginSpaceViewObject();
        for (std::size_t row = 0; row < Elite::SPACE_VIEW_HEIGHT_PIXELS; ++row)
        {
          for (std::size_t x = 0; x < Elite::SPACE_VIEW_WIDTH_PIXELS && row != EARLY_ROW; ++x)
          {
            DrawPixel(state, row, x, Pattern(row, x));
          }
        }
        hardware.EndSpaceViewObject(0);
        hardware.BeginSpaceViewObject();
        hardware.NoteSpaceViewModel();
        for (std::size_t row = MODEL_FIRST_ROW; row <= MODEL_LAST_ROW; ++row)
        {
          for (std::size_t x = 0; x < Elite::SPACE_VIEW_WIDTH_PIXELS; ++x)
          {
            DrawPixel(state, row, x, static_cast<std::uint8_t>(Pattern(row, x) ^ 1));
          }
        }
        hardware.EndSpaceViewObject(MODEL_SLOT);
        hardware.BeginSpaceViewObject();
        for (std::size_t x = 0; x < Elite::SPACE_VIEW_WIDTH_PIXELS; ++x)
        {
          DrawPixel(state, BACKGROUND_ROW, x, background(BACKGROUND_ROW, x));
        }
        hardware.EndSpaceViewBackground();
        Elite::PresentSpaceView(state, hardware, false);
        hardware.PublishFlightSnapshot(Elite::TakeFlightSnapshot(state), false);
      });
    Assert::IsTrue(rig.Host().RunUntil(10'000'000) == Elite::StopReason::Terminated, L"the frame is drawn");

    const Elite::SpaceViewLayers& layers = rig.Host().SpaceView();
    const auto image = std::make_unique<Elite::FrameImage>();
    rig.Host().Render(*image);
    const std::array<std::uint8_t, 4> colors = rig.Host().GraphicsColors();
    Assert::IsTrue(layers.DrawnAsModel(MODEL_SLOT) && !layers.DrawnAsModel(0), L"the model's slot");
    for (std::size_t row = 0; row < Elite::SPACE_VIEW_HEIGHT_PIXELS; ++row)
    {
      const bool modelRow = row >= MODEL_FIRST_ROW && row <= MODEL_LAST_ROW && row != BACKGROUND_ROW;
      for (std::size_t x = 0; x < Elite::SPACE_VIEW_WIDTH_PIXELS; ++x)
      {
        const std::size_t pixel = row * Elite::SPACE_VIEW_WIDTH_PIXELS + x;
        std::uint8_t drawn = modelRow ? static_cast<std::uint8_t>(Pattern(row, x) ^ 1) : Pattern(row, x);
        std::uint8_t under = Pattern(row, x) == 0 ? Elite::LAYER_CLEAR : Pattern(row, x); // 0 is what was there
        std::uint8_t over = Elite::LAYER_CLEAR;
        if (row == BACKGROUND_ROW)
        {
          drawn = background(row, x);
          under = drawn;
        }
        else if (row == EARLY_ROW)
        {
          drawn = EARLY_VALUE;
          under = Elite::LAYER_CLEAR;
          over = EARLY_VALUE;
        }
        if (layers.Under()[pixel] != under || layers.Over()[pixel] != over || layers.ModelOwns(pixel) != modelRow ||
            ShownPixel(rig.State(), row, x) != drawn ||
            image->At(static_cast<std::uint32_t>(2 * (Elite::SPACE_VIEW_LEFT_PIXELS + x)),
                      static_cast<std::uint32_t>(Elite::SPACE_VIEW_TOP_LINES + row)) != colors[drawn])
        {
          Assert::Fail((L"row " + std::to_wstring(row) + L", x " + std::to_wstring(x)).c_str());
        }
      }
    }
    Assert::IsTrue(rig.Host().SpaceViewShown(), L"the screen shows the frame");
    const std::uint8_t graphics = rig.Host().Video().ModeControl(); // as the BIOS's mode 4 set it
    rig.Host().Video().SetModeControl(0x08); // 40-column text, as the escape pod's screen has it, over the same bytes
    Assert::IsFalse(rig.Host().SpaceViewShown(), L"not in text mode");
    rig.Host().Video().SetModeControl(graphics);
    Assert::IsTrue(rig.Host().SpaceViewShown(), L"in graphics mode again");
    rig.State().SetVideoByte(static_cast<std::uint16_t>((40 / 2) * 80 + 20), 0x5A); // screen line 40, view row 28: bank 0
    Assert::IsFalse(rig.Host().SpaceViewShown(), L"not once the screen has changed");
  }

  // Every frame of a fight, each checked a millisecond after it was published: every pixel of the view is its over layer's, a model's,
  // or its under layer's, and between them the frames hold models, under pixels and over pixels.
  TEST_METHOD(EachFrameExplainsTheScreen)
  {
    const std::filesystem::path directory = Elite::FindInRepository("Replays");
    Assert::IsFalse(directory.empty(), L"Replays/ at the repository root");
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps(ReadText(directory / REPLAY), steps, error), L"the replay parses");

    ReferenceRig rig("SpaceViewLayers");
    Elite::ReplayPlayer player(rig.Host(), rig.State(), rig.Store(), directory);
    const auto image = std::make_unique<Elite::FrameImage>();
    Seen seen;
    std::uint64_t checkedSequence = 0;
    // By each frame's sequence: whether the screen stopped showing it before the next, and when it was published.
    std::vector<bool> hidden(1, false);
    std::vector<std::uint64_t> publishedMilliseconds(1, 0);
    std::vector<bool> titles(1, false);
    std::uint64_t elapsedMilliseconds = 0;
    for (const Elite::Step& step : steps)
    {
      Elite::Checkpoint checkpoint;
      if (step.kind != Elite::StepKind::Wait)
      {
        Assert::IsTrue(player.Play(step, checkpoint) == Elite::ExpectedStop(step), L"the step runs");
        continue;
      }
      // A wait a millisecond at a time, which ends on the same cycle as the whole (ReplayTests' WaitsAddUpExactly).
      Elite::Step slice = step;
      slice.waitMilliseconds = SLICE_MILLISECONDS;
      for (std::uint64_t waited = 0; waited < step.waitMilliseconds; waited += SLICE_MILLISECONDS)
      {
        Assert::IsTrue(player.Play(slice, checkpoint) == Elite::StopReason::Reached, L"the wait runs");
        elapsedMilliseconds += SLICE_MILLISECONDS;
        const std::uint64_t sequence = rig.Host().LatestFlightSnapshot().sequence;
        if (sequence != checkedSequence)
        {
          checkedSequence = sequence;
          CheckFrame(rig, *image, seen);
          hidden.push_back(false);
          publishedMilliseconds.push_back(elapsedMilliseconds);
          titles.push_back(rig.Host().LatestFlightSnapshot().title);
        }
        if (sequence != 0 && !rig.Host().SpaceViewShown())
        {
          hidden.back() = true;
        }
      }
    }
    Assert::IsTrue(seen.frames > 0, L"frames of flight");
    // Between two frames of one flight, or of the title, the screen shows the first until the second: a renderer drawing in its
    // place never falls back to the game's picture mid-flight. A frame followed only later, or by the other kind, may be hidden:
    // a jump, a death, the credits or a screen came between.
    std::size_t ends = 0;
    for (std::size_t sequence = 1; sequence < hidden.size(); ++sequence)
    {
      const bool followed = sequence + 1 < publishedMilliseconds.size() && titles[sequence + 1] == titles[sequence] &&
                            publishedMilliseconds[sequence + 1] - publishedMilliseconds[sequence] <= MOST_FRAME_GAP_MILLISECONDS;
      Assert::IsFalse(followed && hidden[sequence], (L"frame " + std::to_wstring(sequence) + L" hidden before the next").c_str());
      ends += hidden[sequence] ? 1 : 0;
    }
    Logger::WriteMessage(("frames hidden before a tunnel, a jump or a screen: " + std::to_string(ends)).c_str());
    Assert::IsTrue(seen.models > 0 && seen.modelPixels > 0, L"ships drawn as models");
    Assert::IsTrue(seen.titleModels > 0, L"the title's ship drawn as a model");
    Assert::IsTrue(seen.titleFrames > 0, L"frames of the title");
    Assert::IsTrue(seen.underPixels > 0, L"under pixels: the sun, the planet or a dot");
    Assert::IsTrue(seen.overPixels > 0, L"over pixels: stardust, beams or the sights");
    Logger::WriteMessage(("frames " + std::to_string(seen.frames) + ", models " + std::to_string(seen.models) + ", model pixels " +
                          std::to_string(seen.modelPixels) + ", under " + std::to_string(seen.underPixels) + ", over " +
                          std::to_string(seen.overPixels))
                           .c_str());
  }
};

} // namespace GameLogicTests
