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

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
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
// One that pauses in flight (D66).
constexpr char PAUSE_REPLAY[] = "flight-screens.replay";
// One where ships come into the view from beside it (D69).
constexpr char ENTERING_REPLAY[] = "witch-space.replay";
constexpr std::uint64_t SLICE_MILLISECONDS = 1; // far less than a frame, so a frame's view is still on the screen
// Within this of the frame before, a frame follows it in the same flight with nothing drawn between: across the corpus a frame
// follows the one before within 51 ms, or after 1,085 ms or more where a jump, a death or a screen came between (ADR-033).
constexpr std::uint64_t MOST_FRAME_GAP_MILLISECONDS = 250;
// How far a whole disc of the sun or the planet is from its sphere's projection, in centre and in radius: across the corpus at
// most 2.45 and 2.14 pixels, from SizePlanet's integer square root and its rounding down, and the sun's fringe (ADR-035).
constexpr float MOST_DISC_PIXELS = 2.5f;
// Each tunnel's frames, every one published (D68): the station's twenty (PlayStationTunnel), the hyperspace rings' fifty
// (PlayHyperspaceTunnel).
constexpr std::size_t STATION_TUNNEL_FRAMES = 20;
constexpr std::size_t HYPERSPACE_TUNNEL_FRAMES = 50;

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
  Elite::Field<std::uint8_t>& byte = Elite::DrawingBuffer(_state)[(_row + 1) * 64 + _x / 4];
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
  std::size_t rendererPixels = 0; ///< no slot's pixels the renderer draws: the stardust, the beams, the sights, the title's planet
  std::size_t dust = 0;
  std::size_t beams = 0;
  std::size_t sights = 0;
  std::size_t discs = 0;      ///< the sun's and the planet's discs measured against their spheres
  std::size_t wordPixels = 0; ///< the game's words' ink (D66)
  std::size_t titleWordPixels = 0;
  std::size_t wordsFrames = 0; ///< frames of only the game's words: the credits, the pause screen
  std::size_t creditsFrames = 0;
  std::size_t pictureFrames = 0;  ///< frames of a tunnel, the game's own picture (D68)
  std::size_t ruledModels = 0;    ///< objects in the view the game drew as models, which DrawingOf says it would (D69)
  std::size_t ruledDots = 0;      ///< and those it did not, which DrawingOf says are dots
  std::size_t ruledOutside = 0;   ///< objects out of the view's cone that DrawingOf would draw
  std::size_t ruledFlashing = 0;  ///< flashing objects in the view, not held to the rule
  std::size_t ruledEntering = 0;  ///< objects that came into the view, which the rule foresaw from beside it
  float worstCenterPixels = 0.0f; ///< the furthest a disc's centre was from its sphere's
  float worstRadiusPixels = 0.0f; ///< the furthest a disc's radius was from its sphere's projected radius
  float leastRadiusShare = 1e9f;  ///< a disc's radius over its sphere's projected radius, at least and at most
  float mostRadiusShare = 0.0f;
};

// The distance from (_x, _y) to the segment from (_fromX, _fromY) to (_toX, _toY).
[[nodiscard]] float SegmentDistance(float _x, float _y, float _fromX, float _fromY, float _toX, float _toY) noexcept
{
  const float alongX = _toX - _fromX;
  const float alongY = _toY - _fromY;
  const float length = alongX * alongX + alongY * alongY;
  const float t = length == 0.0f ? 0.0f : std::clamp(((_x - _fromX) * alongX + (_y - _fromY) * alongY) / length, 0.0f, 1.0f);
  const float dx = _x - (_fromX + t * alongX);
  const float dy = _y - (_fromY + t * alongY);
  return std::sqrt(dx * dx + dy * dy);
}

// The sights' place in the view (DrawLaserSights): 16 pixels from x 120 and 16 rows from row 55.
constexpr std::size_t SIGHTS_X = 120;
constexpr std::size_t SIGHTS_ROW = 55;
constexpr std::size_t SIGHTS_PIXELS = 16;
// How far a line's pixel may be from the line itself: DrawLine steps along it a pixel at a time, and DrawClippedLine's clipped
// ends are whole pixels, which move a long line up to 1.2 pixels across the corpus.
constexpr float LINE_SLACK_PIXELS = 1.5f;

// Whether the snapshot puts something the renderer draws at view pixel (_x, _row), no slot's: a stardust particle or its streak,
// a beam, the sights, or the title's planet, whose disc DrawDisc draws its rows each side and as wide, the fringe a pixel more.
[[nodiscard]] bool SnapshotExplains(const Elite::FlightSnapshot& _snapshot, std::size_t _x, std::size_t _row)
{
  const auto x = static_cast<float>(_x);
  const auto row = static_cast<float>(_row + 1); // the drawing buffer's
  for (std::size_t index = 0; index < _snapshot.dustCount; ++index)
  {
    const Elite::SnapshotDust& dust = _snapshot.dust[index];
    const auto fromX = static_cast<float>(dust.streak ? dust.fromX : dust.x);
    const auto fromRow = static_cast<float>(dust.streak ? dust.fromRow : dust.row);
    if (SegmentDistance(x, row, static_cast<float>(dust.x), static_cast<float>(dust.row), fromX, fromRow) <=
        (dust.streak ? LINE_SLACK_PIXELS : 0.0f))
    {
      return true;
    }
  }
  for (std::size_t index = 0; index < _snapshot.beamCount; ++index)
  {
    const Elite::SnapshotBeam& beam = _snapshot.beams[index];
    if (SegmentDistance(x, row, beam.fromX, beam.fromRow, beam.toX, beam.toRow) <= LINE_SLACK_PIXELS)
    {
      return true;
    }
  }
  if (_snapshot.sight != Elite::FlightSnapshot::NO_SIGHT && _x - SIGHTS_X < SIGHTS_PIXELS && _row - SIGHTS_ROW < SIGHTS_PIXELS)
  {
    return true;
  }
  const Elite::SnapshotDisc& planet = _snapshot.titlePlanet;
  const auto reach = static_cast<float>(planet.rows) + 1.0f;
  return planet.shown && std::abs(row - planet.row) <= reach && std::abs(x - planet.x) <= reach + 1.0f;
}

// _vector turned by _rotation, a unit quaternion.
[[nodiscard]] std::array<float, 3> Rotate(const Elite::Orientation& _rotation, const std::array<float, 3>& _vector) noexcept
{
  const std::array<float, 3> twice{2.0f * (_rotation.y * _vector[2] - _rotation.z * _vector[1]),
                                   2.0f * (_rotation.z * _vector[0] - _rotation.x * _vector[2]),
                                   2.0f * (_rotation.x * _vector[1] - _rotation.y * _vector[0])};
  return {_vector[0] + _rotation.w * twice[0] + (_rotation.y * twice[2] - _rotation.z * twice[1]),
          _vector[1] + _rotation.w * twice[1] + (_rotation.z * twice[0] - _rotation.x * twice[2]),
          _vector[2] + _rotation.w * twice[2] + (_rotation.x * twice[1] - _rotation.y * twice[0])};
}

// The disc the game drew for the sun or the planet in _slot, measured against the sphere the renderer draws in its place:
// its pixels' bounds against the sphere's centre and radius projected as the view projects. A disc of r rows spans 2r - 1 rows
// and 2r + 1 pixels across, so its radius is the mean of its half-height and its half-width. A disc the view cut, or one a nearer
// object painted over in part, is not measured: its bounds are no longer as wide as they are high, or it holds less than a whole
// disc's share of their ellipse.
void MeasureDisc(const Elite::SpaceViewLayers& _layers, const Elite::FlightSnapshot& _snapshot, const Elite::SnapshotObject& _object,
                 Seen& _seen)
{
  std::size_t left = Elite::SPACE_VIEW_WIDTH_PIXELS;
  std::size_t right = 0;
  std::size_t top = Elite::SPACE_VIEW_HEIGHT_PIXELS;
  std::size_t bottom = 0;
  std::size_t pixels = 0;
  for (std::size_t row = 0; row < Elite::SPACE_VIEW_HEIGHT_PIXELS; ++row)
  {
    for (std::size_t x = 0; x < Elite::SPACE_VIEW_WIDTH_PIXELS; ++x)
    {
      if (_layers.OwnerOf(row * Elite::SPACE_VIEW_WIDTH_PIXELS + x) == _object.slot)
      {
        ++pixels;
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, row);
        bottom = std::max(bottom, row);
      }
    }
  }
  if (left > right || left == 0 || top == 0 || right + 1 == Elite::SPACE_VIEW_WIDTH_PIXELS || bottom + 1 == Elite::SPACE_VIEW_HEIGHT_PIXELS)
  {
    return;
  }
  const float halfWidth = static_cast<float>(right - left + 1) / 2.0f;
  const float halfHeight = static_cast<float>(bottom - top + 1) / 2.0f;
  constexpr float MOST_ASPECT_PIXELS = 1.5f; // a whole disc is a pixel wider than high, and the sun's fringe a pixel more
  constexpr float WHOLE_SHARE = 0.85f;       // of its bounds' ellipse a whole disc fills, with its stepped edge
  if (std::abs(halfWidth - halfHeight) > MOST_ASPECT_PIXELS ||
      static_cast<float>(pixels) < WHOLE_SHARE * std::numbers::pi_v<float> * halfWidth * halfHeight)
  {
    return;
  }
  const Elite::SnapshotSphere sphere = Elite::SphereOf(_snapshot, _object);
  const std::array<float, 3> camera = Rotate(Elite::CameraRotation(_snapshot), sphere.position);
  const float distance = std::sqrt(camera[0] * camera[0] + camera[1] * camera[1] + camera[2] * camera[2]);
  if (camera[2] <= sphere.radius || distance <= sphere.radius)
  {
    return;
  }
  const float centerX = Elite::VIEW_AXIS_X + Elite::VIEW_FOCAL * camera[0] / camera[2];
  const float centerRow = Elite::VIEW_AXIS_ROW + Elite::VIEW_FOCAL * camera[1] / camera[2];
  const float projected = Elite::VIEW_FOCAL * sphere.radius / std::sqrt(distance * distance - sphere.radius * sphere.radius);
  const float discX = static_cast<float>(left + right + 1) / 2.0f;
  const float discRow = static_cast<float>(top + bottom + 1) / 2.0f + 1.0f; // the buffer's
  const float radius = (halfWidth + halfHeight) / 2.0f;
  ++_seen.discs;
  _seen.worstCenterPixels = std::max(_seen.worstCenterPixels, std::hypot(discX - centerX, discRow - centerRow));
  _seen.worstRadiusPixels = std::max(_seen.worstRadiusPixels, std::abs(radius - projected));
  _seen.leastRadiusShare = std::min(_seen.leastRadiusShare, radius / projected);
  _seen.mostRadiusShare = std::max(_seen.mostRadiusShare, radius / projected);
}

// "Press any key to start game", where the title draws it in the view (PRESS_ANY_KEY_PLACE, 1C05h of the drawing buffer): buffer
// row 112, the view's 111, from byte 5, pixel 20, 27 characters of 8 pixels across and 8 rows down.
constexpr std::size_t PRESS_ANY_KEY_ROW = 111;
constexpr std::size_t PRESS_ANY_KEY_X = 20;
constexpr std::size_t PRESS_ANY_KEY_PIXELS = std::size_t{27} * 8;
constexpr std::size_t GLYPH_ROWS = 8;

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
      if (layers.RendererOwns(pixel))
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
  // What the renderer draws that is no slot's is all in the snapshot (ADR-035).
  for (std::size_t row = 0; row < Elite::SPACE_VIEW_HEIGHT_PIXELS; ++row)
  {
    for (std::size_t x = 0; x < Elite::SPACE_VIEW_WIDTH_PIXELS; ++x)
    {
      const std::size_t pixel = row * Elite::SPACE_VIEW_WIDTH_PIXELS + x;
      if (layers.RendererOwns(pixel) && layers.OwnerOf(pixel) == Elite::SpaceViewLayers::NO_SLOT)
      {
        ++_seen.rendererPixels;
        if (!SnapshotExplains(snapshot, x, row))
        {
          Assert::Fail((L"frame " + std::to_wstring(_seen.frames) + L", row " + std::to_wstring(row) + L", x " + std::to_wstring(x) +
                        L": the renderer's pixel in no snapshot's place")
                         .c_str());
        }
      }
    }
  }
  // The game's words (D66): the over layer's pixels no object drew, never the renderer's. On the title's turning ship they are
  // its line's; in a frame of words only, the credits' or the pause screen's, they are all it shows.
  const bool wordsOnly = !snapshot.picture && snapshot.objectCount == 0 && snapshot.dustCount == 0 &&
                         snapshot.sight == Elite::FlightSnapshot::NO_SIGHT && !snapshot.titlePlanet.shown;
  for (std::size_t row = 0; row < Elite::SPACE_VIEW_HEIGHT_PIXELS; ++row)
  {
    for (std::size_t x = 0; x < Elite::SPACE_VIEW_WIDTH_PIXELS; ++x)
    {
      const std::size_t pixel = row * Elite::SPACE_VIEW_WIDTH_PIXELS + x;
      const auto where = [&_seen, row, x](const wchar_t* _what) {
        return L"frame " + std::to_wstring(_seen.frames) + L", row " + std::to_wstring(row) + L", x " + std::to_wstring(x) + L": " + _what;
      };
      const bool ink = layers.HoldsWords(pixel) && layers.Over()[pixel] != 0;
      // A tunnel's frame is all the over layer's, as the screen shows it, and none of it words or the renderer's (D68).
      if (snapshot.picture && (layers.Over()[pixel] == Elite::LAYER_CLEAR || layers.HoldsWords(pixel) || layers.RendererOwns(pixel)))
      {
        Assert::Fail(where(L"a tunnel's frame is the game's picture").c_str());
      }
      if (layers.HoldsWords(pixel) && (layers.Over()[pixel] == Elite::LAYER_CLEAR || layers.RendererOwns(pixel)))
      {
        Assert::Fail(where(L"words are the over layer's").c_str());
      }
      if (wordsOnly && ShownPixel(_rig.State(), row, x) != 0 && !ink)
      {
        Assert::Fail(where(L"a frame of words shows only its words").c_str());
      }
      if (ink && snapshot.title && !wordsOnly)
      {
        ++_seen.titleWordPixels;
        if (row - PRESS_ANY_KEY_ROW >= GLYPH_ROWS || x - PRESS_ANY_KEY_X >= PRESS_ANY_KEY_PIXELS)
        {
          Assert::Fail(where(L"the title's words are its line").c_str());
        }
      }
      _seen.wordPixels += ink ? 1 : 0;
    }
  }
  _seen.wordsFrames += wordsOnly ? 1 : 0;
  _seen.creditsFrames += wordsOnly && snapshot.title ? 1 : 0;
  if (snapshot.picture)
  {
    ++_seen.pictureFrames;
    Assert::IsTrue(snapshot.objectCount == 0 && snapshot.dustCount == 0 && snapshot.beamCount == 0 &&
                     snapshot.sight == Elite::FlightSnapshot::NO_SIGHT,
                   L"a tunnel's frame holds nothing the renderer draws");
  }
  _seen.dust += snapshot.dustCount;
  _seen.beams += snapshot.beamCount;
  _seen.sights += snapshot.sight != Elite::FlightSnapshot::NO_SIGHT ? 1 : 0;
  // The title's dashboard is the cockpit's picture, nothing on it live, with the title on its message line; flight's is live (D64).
  Assert::AreEqual(!snapshot.title, snapshot.dashboard.live, L"the dashboard live in flight and not on the title");
  if (snapshot.title)
  {
    Assert::AreEqual(std::string("---- E L I T E ----"), std::string(snapshot.dashboard.message.data()), L"the title's message line");
    Assert::AreEqual(std::size_t{0}, snapshot.dashboard.blipCount, L"no blips on the title's scanner");
    ++_seen.titleFrames;
  }
  // 1987's rule for the view's sides (D69): of every object in the view's cone, DrawingOf says a model exactly where the game
  // drew one. A flashing object's count has moved on a frame by the time the snapshot reads it, and is not held to it.
  for (std::size_t index = 0; index < snapshot.objectCount; ++index)
  {
    const Elite::SnapshotObject& object = snapshot.objects[index];
    if (!object.inView)
    {
      _seen.ruledOutside += object.drawing != Elite::SnapshotDrawing::None ? 1 : 0;
      continue;
    }
    if (object.flashes)
    {
      ++_seen.ruledFlashing;
      continue;
    }
    if ((object.drawing == Elite::SnapshotDrawing::Model) != object.model)
    {
      Assert::Fail((L"frame " + std::to_wstring(_seen.frames) + L", slot " + std::to_wstring(object.slot) +
                    L": DrawingOf says a model where the game drew one, and only there")
                     .c_str());
    }
    _seen.ruledModels += object.model ? 1 : 0;
    _seen.ruledDots += object.drawing == Elite::SnapshotDrawing::Dot ? 1 : 0;
  }
  // And beside it, where the game draws nothing to hold the rule to: an object that comes into the view, from the frame before,
  // where it was already in a snapshot, is drawn as a model exactly where the rule had it as one. One new in the frame before
  // the game had not classified yet, and draws no sooner than it does in the view.
  const Elite::FlightSnapshot& before = _rig.Host().PreviousFlightSnapshot();
  for (std::size_t index = 0; index < snapshot.objectCount && before.stardust && snapshot.stardust; ++index)
  {
    const Elite::SnapshotObject& object = snapshot.objects[index];
    const auto was = std::ranges::find(before.objects.begin(), before.objects.begin() + static_cast<std::ptrdiff_t>(before.objectCount),
                                       object.slot, &Elite::SnapshotObject::slot);
    if (!object.inView || !object.continues || object.flashes ||
        was == before.objects.begin() + static_cast<std::ptrdiff_t>(before.objectCount) || was->inView || !was->continues)
    {
      continue;
    }
    ++_seen.ruledEntering;
    if ((was->drawing == Elite::SnapshotDrawing::Model) != object.model)
    {
      Assert::Fail((L"frame " + std::to_wstring(_seen.frames) + L", slot " + std::to_wstring(object.slot) +
                    L": a model as it came into the view exactly where DrawingOf had one beside it")
                     .c_str());
    }
  }
  for (std::size_t index = 0; index < snapshot.objectCount; ++index)
  {
    _seen.models += snapshot.objects[index].model ? 1 : 0;
    _seen.titleModels += snapshot.title && snapshot.objects[index].model ? 1 : 0;
    if (Elite::IsSun(snapshot.objects[index]) || Elite::IsPlanet(snapshot.objects[index]))
    {
      MeasureDisc(layers, snapshot, snapshot.objects[index], _seen);
    }
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
    Assert::IsFalse(layers->DrawnAsModel(3) || layers->RendererOwns(0));
    Assert::AreEqual(Elite::SpaceViewLayers::NO_SLOT, layers->OwnerOf(0));
    const auto state = std::make_unique<Elite::GameState>();
    Assert::IsFalse(layers->Shown(*state), L"no frame is shown before one is finished");
  }

  // Where the layers put a pixel, held to the game's own copy of the view to the screen and to the CGA's picture of it. Before
  // any object a row is drawn, as the title draws its words; an object that is not a model draws a pattern over the rest of the
  // view; a model draws over part of it; something the renderer draws, as the stardust is, over a row of the model; and last
  // another plain object a row of its own. The early row is over, and so is the late one, drawn after the renderer's. The under
  // layer is the pattern, pixel for pixel, but for its 0s, which changed nothing of the cleared buffer; where the model or the
  // renderer's drew last the pixel is the renderer's. Each pixel's owner is the slot that changed it last. The screen shows them
  // where PresentSpaceView copies them, in the CGA's colours, and the view is shown in graphics mode until a byte of it changes.
  TEST_METHOD(PixelsAreWhereTheScreenShowsThem)
  {
    constexpr std::size_t MODEL_FIRST_ROW = 40;
    constexpr std::size_t MODEL_LAST_ROW = 60;
    constexpr std::uint8_t MODEL_SLOT = 3;
    constexpr std::size_t EARLY_ROW = 120;     // drawn before any object
    constexpr std::size_t BACKGROUND_ROW = 50; // the renderer's, over the model
    constexpr std::size_t LATE_ROW = 90;       // a plain object's, after the renderer's
    constexpr std::uint8_t PATTERN_SLOT = 0;
    constexpr std::uint8_t LATE_SLOT = 5;
    constexpr std::uint8_t EARLY_VALUE = 3;
    const auto background = [](std::size_t _row, std::size_t _x) { return static_cast<std::uint8_t>(Pattern(_row, _x) ^ 2); };
    const auto late = [](std::size_t _row, std::size_t _x) { return static_cast<std::uint8_t>(Pattern(_row, _x) ^ 3); };
    PlatformRig rig("SpaceViewPixels");
    rig.Start(
      [&rig, &background, &late]
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
        hardware.EndSpaceViewObject(PATTERN_SLOT);
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
        hardware.EndSpaceViewRendered();
        hardware.BeginSpaceViewObject();
        for (std::size_t x = 0; x < Elite::SPACE_VIEW_WIDTH_PIXELS; ++x)
        {
          DrawPixel(state, LATE_ROW, x, late(LATE_ROW, x));
        }
        hardware.EndSpaceViewObject(LATE_SLOT);
        Elite::PresentSpaceView(state, hardware, false);
        hardware.PublishFlightSnapshot(Elite::TakeFlightSnapshot(state), false);
      });
    Assert::IsTrue(rig.Host().RunUntil(10'000'000) == Elite::StopReason::Terminated, L"the frame is drawn");

    const Elite::SpaceViewLayers& layers = rig.Host().SpaceView();
    const auto image = std::make_unique<Elite::FrameImage>();
    rig.Host().Render(*image);
    const std::array<std::uint8_t, 4> colors = rig.Host().GraphicsColors();
    Assert::IsTrue(layers.DrawnAsModel(MODEL_SLOT) && !layers.DrawnAsModel(PATTERN_SLOT), L"the model's slot");
    for (std::size_t row = 0; row < Elite::SPACE_VIEW_HEIGHT_PIXELS; ++row)
    {
      const bool modelRow = row >= MODEL_FIRST_ROW && row <= MODEL_LAST_ROW;
      for (std::size_t x = 0; x < Elite::SPACE_VIEW_WIDTH_PIXELS; ++x)
      {
        const std::size_t pixel = row * Elite::SPACE_VIEW_WIDTH_PIXELS + x;
        std::uint8_t drawn = modelRow ? static_cast<std::uint8_t>(Pattern(row, x) ^ 1) : Pattern(row, x);
        std::uint8_t under = Pattern(row, x) == 0 ? Elite::LAYER_CLEAR : Pattern(row, x); // 0 is what was there
        std::uint8_t over = Elite::LAYER_CLEAR;
        bool renderers = modelRow;
        std::uint8_t owner = modelRow ? MODEL_SLOT : (Pattern(row, x) == 0 ? Elite::SpaceViewLayers::NO_SLOT : PATTERN_SLOT);
        if (row == BACKGROUND_ROW)
        {
          drawn = background(row, x);
          owner = Elite::SpaceViewLayers::NO_SLOT;
        }
        else if (row == LATE_ROW)
        {
          drawn = late(row, x);
          over = drawn;
          owner = LATE_SLOT;
        }
        else if (row == EARLY_ROW)
        {
          drawn = EARLY_VALUE;
          under = Elite::LAYER_CLEAR;
          over = EARLY_VALUE;
          owner = Elite::SpaceViewLayers::NO_SLOT;
        }
        renderers = renderers || row == BACKGROUND_ROW;
        if (layers.Under()[pixel] != under || layers.Over()[pixel] != over || layers.RendererOwns(pixel) != renderers ||
            layers.OwnerOf(pixel) != owner || layers.HoldsWords(pixel) != (row == EARLY_ROW) || ShownPixel(rig.State(), row, x) != drawn ||
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

  // Every frame of a fight, each checked a millisecond after it was published: every pixel of the view is its over layer's, the
  // renderer's, or its under layer's; every pixel the renderer draws that is no slot's is where the snapshot puts stardust, a beam,
  // the sights or the title's planet (ADR-035); and each whole disc of the sun or the planet is where its sphere projects, within
  // MOST_DISC_PIXELS. Between them the frames hold models, stardust, beams, the sights and discs.
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
    std::size_t tunnelRun = 0;
    std::vector<std::size_t> tunnelRuns;
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
          // The tunnels' runs of frames, as each ends.
          if (rig.Host().LatestFlightSnapshot().picture)
          {
            ++tunnelRun;
          }
          else if (tunnelRun != 0)
          {
            tunnelRuns.push_back(tunnelRun);
            tunnelRun = 0;
          }
        }
        if (sequence != 0 && !rig.Host().SpaceViewShown())
        {
          hidden.back() = true;
        }
      }
    }
    if (tunnelRun != 0)
    {
      tunnelRuns.push_back(tunnelRun);
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
    Assert::IsTrue(seen.dust > 0 && seen.beams > 0 && seen.sights > 0 && seen.rendererPixels > 0, L"stardust, beams and the sights");
    Assert::IsTrue(seen.discs > 0, L"whole discs of the sun or the planet");
    Assert::IsTrue(seen.titleWordPixels > 0, L"the title's words");
    Assert::IsTrue(seen.creditsFrames > 0, L"the credits, published and shown");
    Assert::IsTrue(std::ranges::contains(tunnelRuns, STATION_TUNNEL_FRAMES), L"the station's tunnel, every frame published");
    Assert::IsTrue(std::ranges::contains(tunnelRuns, HYPERSPACE_TUNNEL_FRAMES), L"the hyperspace rings, every frame published");
    Assert::IsTrue(seen.worstCenterPixels <= MOST_DISC_PIXELS, L"each disc's centre where its sphere's projects");
    Assert::IsTrue(seen.worstRadiusPixels <= MOST_DISC_PIXELS, L"each disc's radius its sphere's, projected");
    Logger::WriteMessage(("frames " + std::to_string(seen.frames) + ", models " + std::to_string(seen.models) + ", model pixels " +
                          std::to_string(seen.modelPixels) + ", under " + std::to_string(seen.underPixels) + ", over " +
                          std::to_string(seen.overPixels) + ", renderer's " + std::to_string(seen.rendererPixels) + ", dust " +
                          std::to_string(seen.dust) + ", beams " + std::to_string(seen.beams) + ", sights " + std::to_string(seen.sights) +
                          ", discs " + std::to_string(seen.discs) + ", words " + std::to_string(seen.wordPixels) + ", frames of words " +
                          std::to_string(seen.wordsFrames) + ", frames of tunnels " + std::to_string(seen.pictureFrames) +
                          ", ruled models " + std::to_string(seen.ruledModels) + ", dots " + std::to_string(seen.ruledDots) + ", outside " +
                          std::to_string(seen.ruledOutside) + ", entering " + std::to_string(seen.ruledEntering) + ", centre off " +
                          std::to_string(seen.worstCenterPixels) + ", radius off " + std::to_string(seen.worstRadiusPixels) +
                          ", radius share " + std::to_string(seen.leastRadiusShare) + "-" + std::to_string(seen.mostRadiusShare))
                           .c_str());
  }

  // Ships come into the view from beside it, where the renderer draws them by 1987's rule (D69): each as a model exactly where
  // the rule had it as one, every frame checked as the fight's are a millisecond after it was published, while it is shown.
  TEST_METHOD(ShipsComeIntoTheViewAsTheRuleForesaw)
  {
    const std::filesystem::path directory = Elite::FindInRepository("Replays");
    Assert::IsFalse(directory.empty(), L"Replays/ at the repository root");
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps(ReadText(directory / ENTERING_REPLAY), steps, error), L"the replay parses");

    ReferenceRig rig("SpaceViewEntering");
    Elite::ReplayPlayer player(rig.Host(), rig.State(), rig.Store(), directory);
    const auto image = std::make_unique<Elite::FrameImage>();
    Seen seen;
    std::uint64_t checkedSequence = 0;
    for (const Elite::Step& step : steps)
    {
      Elite::Checkpoint checkpoint;
      if (step.kind != Elite::StepKind::Wait)
      {
        Assert::IsTrue(player.Play(step, checkpoint) == Elite::ExpectedStop(step), L"the step runs");
        continue;
      }
      Elite::Step slice = step;
      slice.waitMilliseconds = SLICE_MILLISECONDS;
      for (std::uint64_t waited = 0; waited < step.waitMilliseconds; waited += SLICE_MILLISECONDS)
      {
        Assert::IsTrue(player.Play(slice, checkpoint) == Elite::StopReason::Reached, L"the wait runs");
        const std::uint64_t sequence = rig.Host().LatestFlightSnapshot().sequence;
        if (sequence != checkedSequence && rig.Host().SpaceViewShown())
        {
          checkedSequence = sequence;
          CheckFrame(rig, *image, seen);
        }
      }
    }
    Assert::IsTrue(seen.ruledEntering > 0, L"ships coming into the view");
    Logger::WriteMessage(("entering " + std::to_string(seen.ruledEntering) + ", ruled models " + std::to_string(seen.ruledModels) +
                          ", outside " + std::to_string(seen.ruledOutside))
                           .c_str());
  }

  // The pause screen (D66) is published as a frame of the game's words, checked as the fight's are a millisecond after it was
  // published, and shown.
  TEST_METHOD(ThePauseScreenIsAFrameOfWords)
  {
    const std::filesystem::path directory = Elite::FindInRepository("Replays");
    Assert::IsFalse(directory.empty(), L"Replays/ at the repository root");
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps(ReadText(directory / PAUSE_REPLAY), steps, error), L"the replay parses");

    ReferenceRig rig("SpaceViewPause");
    Elite::ReplayPlayer player(rig.Host(), rig.State(), rig.Store(), directory);
    const auto image = std::make_unique<Elite::FrameImage>();
    Seen seen;
    std::uint64_t checkedSequence = 0;
    std::size_t pausedFrames = 0;
    for (const Elite::Step& step : steps)
    {
      Elite::Checkpoint checkpoint;
      if (step.kind != Elite::StepKind::Wait)
      {
        Assert::IsTrue(player.Play(step, checkpoint) == Elite::ExpectedStop(step), L"the step runs");
        continue;
      }
      Elite::Step slice = step;
      slice.waitMilliseconds = SLICE_MILLISECONDS;
      for (std::uint64_t waited = 0; waited < step.waitMilliseconds; waited += SLICE_MILLISECONDS)
      {
        Assert::IsTrue(player.Play(slice, checkpoint) == Elite::StopReason::Reached, L"the wait runs");
        const std::uint64_t sequence = rig.Host().LatestFlightSnapshot().sequence;
        if (sequence != checkedSequence)
        {
          checkedSequence = sequence;
          const bool paused = rig.State().Data().flight.gamePaused != 0;
          // A frame of flight a screen replaced within the millisecond is not checked, as ADR-035's sweep found; the pause
          // screen's, which holds no object, is shown.
          if (!rig.Host().SpaceViewShown())
          {
            Assert::IsTrue(rig.Host().LatestFlightSnapshot().objectCount != 0, L"the pause screen shown");
            continue;
          }
          // Of flight's frames, not the credits'. The frame of flight that saw the key is published paused too.
          const std::size_t flightWords = seen.wordsFrames - seen.creditsFrames;
          CheckFrame(rig, *image, seen);
          const bool words = seen.wordsFrames - seen.creditsFrames > flightWords;
          Assert::IsTrue(!words || paused, L"a frame of words only while paused");
          pausedFrames += words ? 1 : 0;
        }
      }
    }
    Assert::IsTrue(pausedFrames > 0 && seen.wordPixels > 0, L"the pause screen's words");
    Logger::WriteMessage(("paused frames " + std::to_string(pausedFrames) + ", words " + std::to_string(seen.wordPixels)).c_str());
  }
};

} // namespace GameLogicTests
