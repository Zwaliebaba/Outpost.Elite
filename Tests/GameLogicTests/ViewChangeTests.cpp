#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "Canvas.h"
#include "Controls.h"
#include "Dashboard.h"
#include "LookupTables.h"
#include "ShipDraw.h"
#include "Charts.h"
#include "Commander.h"
#include "ExtendedTokens.h"
#include "PlanetDraw.h"
#include "Rng.h"
#include "ShipMove.h"
#include "Stardust.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "ShipSlot.h"
#include "ViewChange.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * Setting up a screen (slice 3d-d-iii-a).
 *
 * Every routine here writes the bitmap, so every comparison is a whole-canvas compare from a
 * screen full of a marker byte. That is what separates "wrote nothing" from "wrote a zero", and
 * three of these routines exist to write zeros.
 */
namespace GameLogicTests
{

namespace
{
bool OracleMissing()
{
  const OracleImage& oracle = OracleImage::Instance();
  if (oracle.Available())
  {
    return false;
  }
  Logger::WriteMessage(("SKIPPED -- oracle absent: " + oracle.Reason()).c_str());
  return true;
}

std::wstring Widen(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

std::uint16_t ScreenBase(const OracleImage& _oracle)
{
  const Cpu6502 image = _oracle.Fresh();
  return static_cast<std::uint16_t>(
    (image.memory[_oracle.Label("ylookupl")] | (image.memory[_oracle.Label("ylookuph")] << 8))
    - 0x20);
}

void FillScreens(Cpu6502& _cpu, Elite::Canvas& _canvas, std::uint16_t _base, std::uint8_t _marker)
{
  std::memset(&_cpu.memory[_base], _marker, Elite::Canvas::SCREEN_SIZE);
  for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
  {
    _canvas.Write(offset, _marker);
  }
}

std::uint32_t CompareScreens(const Cpu6502& _cpu, std::uint16_t _base, const Elite::Canvas& _canvas,
                             std::uint8_t _marker, const std::wstring& _context)
{
  const std::span<const std::uint8_t> ours = _canvas.Screen();
  std::uint32_t touched = 0;

  for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
  {
    const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
    if (expected != ours[offset])
    {
      Assert::Fail((_context + L": screen differs at offset " + std::to_wstring(offset)
                    + L" -- game has " + std::to_wstring(expected) + L", port has "
                    + std::to_wstring(ours[offset])).c_str());
    }
    touched += (ours[offset] != _marker) ? 1u : 0u;
  }

  return touched;
}
} // namespace



namespace
{
struct RecordingSight final : Elite::SightEffects
{
  std::vector<std::uint8_t> modes;
  std::vector<std::uint8_t> masks;
  std::vector<std::uint8_t> colours;

  void SetRasterMode(std::uint8_t _mode) override { modes.push_back(_mode); }
  void SetSightColour(std::uint8_t _colour) override { colours.push_back(_colour); }
  void SetSpritesEnabled(std::uint8_t _mask) override { masks.push_back(_mask); }
};

struct RecordingView final : Elite::ViewEffects
{
  std::vector<std::uint8_t> palettes;
  std::vector<std::uint8_t> sounds;

  void SetPalette(std::uint8_t _colour) override { palettes.push_back(_colour); }
  void PlaySound(std::uint8_t _effect) override { sounds.push_back(_effect); }
};

/*
 * The port's whole flight world, and the `FlightScreen` over it.
 *
 * One object because `TT66` genuinely reaches all of it -- the line heaps, the token printer, the
 * message counters, the laser, the stardust and the dashboard -- and building it twice per test
 * method would be the same eighteen arguments in a different disguise.
 */
struct World
{
  Elite::Canvas canvas;
  Elite::DrawWorkspace draw;
  Elite::MathWorkspace math;
  Elite::GeometryWorkspace geometry;

  Elite::Stardust dust;
  Elite::PlanetSunState heaps;
  Elite::Bubble bubble;
  Elite::ShipBlock work{};

  Elite::ScreenState screen;
  Elite::TextState text;

  /*
   * The real character printer, drawing into the canvas -- `CHPR` is NOT trapped on the oracle's
   * side for this slice. `TT66` prints the view's name and `ee3` prints the countdown, and both
   * belong on the screen the routine has just cleared; trapping them would compare a character
   * stream and leave the pixels they produce out of the whole-canvas compare that everything else
   * here is checked by.
   */
  Elite::TextPrinter glyphs{ canvas, text };
  Elite::CharacterPrinter characters{ glyphs };
  Elite::TokenPrinter printer{ characters };
  Elite::MessageState message;

  Elite::FlightState flight;
  Elite::FlightStatus status;
  Elite::Compass compass{ 0xC3u, 0x9Cu, Elite::COMPASS_AHEAD };
  Elite::Rng rng;

  Elite::CommanderBlock commander;
  std::uint8_t trumbles = 0;

  RecordingSight sight;
  RecordingView effects;

  std::uint8_t view = 0;
  std::uint8_t spaceView = 0;
  std::uint8_t explosions = 0;
  std::uint8_t fuel = 0;

  World() { printer.SetCursor(&text); }

  [[nodiscard]] Elite::FlightScreen Screen() noexcept
  {
    return Elite::FlightScreen{ canvas, draw,  math,      geometry, dust,      heaps,
                                bubble, work,  screen,    text,     characters.state,
                                printer, characters, message, flight, status, compass, rng,
                                commander, trumbles, sight, effects,
                                view, spaceView, explosions, fuel };
  }
};

/// A world that is not all zeroes, so "cleared" and "left alone" are different answers everywhere.
void Seed(World& _world, std::uint32_t _seed)
{
  std::uint32_t state = _seed * 0x9E3779B9u + 0x85EBCA6Bu;
  auto next = [&]() {
    state = state * 1103515245u + 12345u;
    return static_cast<std::uint8_t>(state >> 17);
  };

  const std::uint8_t TYPES[] = { 3u, 5u, 2u };
  for (std::size_t slot = 0; slot < 3u; ++slot)
  {
    _world.bubble.slots[slot] = TYPES[slot];
  }
  _world.bubble.counts[Elite::SHIP_TYPE_STATION] = 1u;
  _world.bubble.junk = 1u;

  for (std::size_t slot = 0; slot < _world.bubble.blocks.size(); ++slot)
  {
    for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
    {
      _world.bubble.blocks[slot][byte] = (byte == 31u) ? 0xFFu : next();
    }
  }
  for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
  {
    _world.work[byte] = next();
  }

  for (std::size_t index = 0; index < _world.dust.x.size(); ++index)
  {
    _world.dust.x[index] = next();
    _world.dust.xLow[index] = next();
    _world.dust.y[index] = next();
    _world.dust.yLow[index] = next();
    _world.dust.z[index] = next();
    _world.dust.zLow[index] = next();
  }
  _world.dust.count = 12u; // 6502: NOST

  for (std::size_t index = 0; index < _world.heaps.sun.size(); ++index)
  {
    _world.heaps.sun[index] = next();
  }
  for (std::size_t index = 0; index < _world.heaps.ball.size(); ++index)
  {
    _world.heaps.ball[index] = next();
  }
  _world.heaps.lsp = 0x37u;

  _world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers)] = Elite::LASER_PULSE;
  _world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers) + 1u] = 0u;
  _world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers) + 2u] = Elite::LASER_BEAM;
  _world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers) + 3u] = Elite::LASER_MILITARY;
  _world.commander.bytes[static_cast<std::size_t>(Elite::Field::Tribbles)] = 0x40u;
  _world.commander.bytes[static_cast<std::size_t>(Elite::Field::Tribbles) + 1u] = 0x21u;
  _world.trumbles = 0x5Au;

  _world.rng.SetState({ 0x11u, 0x22u, 0x33u, 0x44u });

  _world.text.column = 0x1Fu;
  _world.text.row = 0x0Bu;
  _world.text.cellColour = Elite::TEXT_COLOUR_WHITE;
  _world.printer.SetCaseFlags(0x40u);
  _world.characters.state.lowerCaseBits = 0u;
  _world.characters.state.sentenceStart = 0u;
  _world.characters.state.alwaysLower = 0xFFu;

  _world.message.delay = 0x2Au;
  _world.message.append = 0x3Bu;
  _world.status.viewLaser = 0x4Cu;
  _world.status.energy = 180u;
  _world.status.forwardShield = 90u;
  _world.status.aftShield = 60u;
  _world.status.cabinTemperature = 100u;
  _world.status.laserTemperature = 70u;
  _world.status.altitude = 120u;
  _world.status.damageFlash = 0u;
  _world.status.ecmCountdown = 0u;
  _world.fuel = 40u;

  _world.flight.delta = 14u;
  _world.flight.alp1 = 5u;
  _world.flight.alp2 = 128u;
  _world.flight.beta = 200u;
  _world.flight.bet1 = 3u;
  _world.flight.mainLoopCounter = 0u;

  _world.screen.colourBank = 0x33u;
  _world.screen.bitmapMode = 0x44u;
  _world.screen.dashboardShown = 0u;
  _world.draw.t2 = 0x88u;
  _world.explosions = 0x66u;
}

/// Every label the screen routines touch, looked up once.
struct Where
{
  std::uint16_t frin, kPercent, many, inwk, sx, sxl, sy, syl, sz, szl, nostm;
  std::uint16_t lso, lsx2, lsp, xc, yc, qq17, dtw1, dtw2, dtw6, col2;
  std::uint16_t dly, de, las2, qq22, viewByte, qq11, mj, junk, ev, rand;
  std::uint16_t abraxas, caravanserai, dflag, comx, comy, comc, t2;
  std::uint16_t delta, alp1, alp2, beta, bet1, energy, fsh, ash, qq14;
  std::uint16_t cabtmp, gntmp, altit, mcnt, flh, ecma, laser, tribble, tribct;
  std::uint16_t screen;

  explicit Where(const OracleImage& _oracle)
  {
    frin = _oracle.Label("FRIN");        kPercent = _oracle.Label("K%");
    many = _oracle.Label("MANY");        inwk = _oracle.Label("INWK");
    sx = _oracle.Label("SX");            sxl = _oracle.Label("SXL");
    sy = _oracle.Label("SY");            syl = _oracle.Label("SYL");
    sz = _oracle.Label("SZ");            szl = _oracle.Label("SZL");
    nostm = _oracle.Label("NOSTM");      lso = _oracle.Label("LSO");
    lsx2 = _oracle.Label("LSX2");        lsp = _oracle.Label("LSP");
    xc = _oracle.Label("XC");            yc = _oracle.Label("YC");
    qq17 = _oracle.Label("QQ17");        dtw1 = _oracle.Label("DTW1");
    dtw2 = _oracle.Label("DTW2");        dtw6 = _oracle.Label("DTW6");
    col2 = _oracle.Label("COL2");        dly = _oracle.Label("DLY");
    de = _oracle.Label("de");            las2 = _oracle.Label("LAS2");
    qq22 = _oracle.Label("QQ22");        viewByte = _oracle.Label("VIEW");
    qq11 = _oracle.Label("QQ11");        mj = _oracle.Label("MJ");
    junk = _oracle.Label("JUNK");        ev = _oracle.Label("EV");
    rand = _oracle.Label("RAND");        abraxas = _oracle.Label("abraxas");
    caravanserai = _oracle.Label("caravanserai");
    dflag = _oracle.Label("DFLAG");      comx = _oracle.Label("COMX");
    comy = _oracle.Label("COMY");        comc = _oracle.Label("COMC");
    t2 = _oracle.Label("T2");            delta = _oracle.Label("DELTA");
    alp1 = _oracle.Label("ALP1");        alp2 = _oracle.Label("ALP2");
    beta = _oracle.Label("BETA");        bet1 = _oracle.Label("BET1");
    energy = _oracle.Label("ENERGY");    fsh = _oracle.Label("FSH");
    ash = _oracle.Label("ASH");          qq14 = _oracle.Label("QQ14");
    cabtmp = _oracle.Label("CABTMP");    gntmp = _oracle.Label("GNTMP");
    altit = _oracle.Label("ALTIT");      mcnt = _oracle.Label("MCNT");
    flh = _oracle.Label("FLH");          ecma = _oracle.Label("ECMA");
    laser = _oracle.Label("LASER");      tribble = _oracle.Label("TRIBBLE");
    tribct = _oracle.Label("TRIBCT");    screen = ScreenBase(_oracle);
  }
};

/// Copy the port's world into the oracle's memory, so both start identical.
void Mirror(const World& _world, Cpu6502& _cpu, const Where& _at)
{
  auto block = [&](std::uint16_t _base, const std::uint8_t* _from, std::size_t _count) {
    for (std::size_t index = 0; index < _count; ++index)
    {
      _cpu.memory[static_cast<std::uint16_t>(_base + index)] = _from[index];
    }
  };

  block(_at.frin, _world.bubble.slots.data(), _world.bubble.slots.size());
  block(_at.many, _world.bubble.counts.data(), _world.bubble.counts.size());
  block(_at.inwk, _world.work.bytes.data(), Elite::SHIP_BLOCK_SIZE);
  for (std::size_t slot = 0; slot < _world.bubble.blocks.size(); ++slot)
  {
    block(static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE),
          _world.bubble.blocks[slot].bytes.data(), Elite::SHIP_BLOCK_SIZE);
  }

  block(_at.sx, _world.dust.x.data(), _world.dust.x.size());
  block(_at.sxl, _world.dust.xLow.data(), _world.dust.xLow.size());
  block(_at.sy, _world.dust.y.data(), _world.dust.y.size());
  block(_at.syl, _world.dust.yLow.data(), _world.dust.yLow.size());
  block(_at.sz, _world.dust.z.data(), _world.dust.z.size());
  block(_at.szl, _world.dust.zLow.data(), _world.dust.zLow.size());
  _cpu.memory[_at.nostm] = _world.dust.count;

  block(_at.lso, _world.heaps.sun.data(), _world.heaps.sun.size());
  block(_at.lsx2, _world.heaps.ball.data(), _world.heaps.ball.size());
  _cpu.memory[_at.lsp] = _world.heaps.lsp;

  block(_at.laser, &_world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers)], 4u);
  block(_at.tribble, &_world.commander.bytes[static_cast<std::size_t>(Elite::Field::Tribbles)], 2u);
  _cpu.memory[_at.tribct] = _world.trumbles;

  const std::array<std::uint8_t, 4> seed = _world.rng.State();
  block(_at.rand, seed.data(), seed.size());

  _cpu.memory[_at.xc] = _world.text.column;
  _cpu.memory[_at.yc] = _world.text.row;
  _cpu.memory[_at.qq17] = _world.printer.CaseFlags();
  _cpu.memory[_at.col2] = _world.text.cellColour;
  _cpu.memory[_at.dtw1] = _world.characters.state.lowerCaseBits;
  _cpu.memory[_at.dtw2] = _world.characters.state.sentenceStart;
  _cpu.memory[_at.dtw6] = _world.characters.state.alwaysLower;

  _cpu.memory[_at.dly] = _world.message.delay;
  _cpu.memory[_at.de] = _world.message.append;
  _cpu.memory[_at.las2] = _world.status.viewLaser;
  _cpu.memory[static_cast<std::uint16_t>(_at.qq22 + 1)] = _world.status.hyperspaceCountdown;
  _cpu.memory[_at.mj] = _world.status.midJump;
  _cpu.memory[_at.junk] = _world.bubble.junk;
  _cpu.memory[_at.ev] = _world.explosions;
  _cpu.memory[_at.viewByte] = _world.spaceView;
  _cpu.memory[_at.qq11] = _world.view;

  _cpu.memory[_at.abraxas] = _world.screen.colourBank;
  _cpu.memory[_at.caravanserai] = _world.screen.bitmapMode;
  _cpu.memory[_at.dflag] = _world.screen.dashboardShown;
  _cpu.memory[_at.comx] = _world.compass.x;
  _cpu.memory[_at.comy] = _world.compass.y;
  _cpu.memory[_at.comc] = _world.compass.colour;
  _cpu.memory[_at.t2] = _world.draw.t2;

  _cpu.memory[_at.delta] = _world.flight.delta;
  _cpu.memory[_at.alp1] = _world.flight.alp1;
  _cpu.memory[_at.alp2] = _world.flight.alp2;
  _cpu.memory[_at.beta] = _world.flight.beta;
  _cpu.memory[_at.bet1] = _world.flight.bet1;
  _cpu.memory[_at.mcnt] = _world.flight.mainLoopCounter;

  _cpu.memory[_at.energy] = _world.status.energy;
  _cpu.memory[_at.fsh] = _world.status.forwardShield;
  _cpu.memory[_at.ash] = _world.status.aftShield;
  _cpu.memory[_at.cabtmp] = _world.status.cabinTemperature;
  _cpu.memory[_at.gntmp] = _world.status.laserTemperature;
  _cpu.memory[_at.altit] = _world.status.altitude;
  _cpu.memory[_at.flh] = _world.status.damageFlash;
  _cpu.memory[_at.ecma] = _world.status.ecmCountdown;
  _cpu.memory[_at.qq14] = _world.fuel;
}

/// Compare every byte of state the screen routines can touch.
void CompareState(const Cpu6502& _cpu, const World& _world, const Where& _at,
                  const std::wstring& _context)
{
  auto same = [&](std::uint16_t _address, std::uint8_t _ours, const wchar_t* _name) {
    Assert::AreEqual(_cpu.memory[_address], _ours, (_context + L": " + _name).c_str());
  };

  same(_at.xc, _world.text.column, L"XC");
  same(_at.yc, _world.text.row, L"YC");
  same(_at.qq17, _world.printer.CaseFlags(), L"QQ17");
  same(_at.dtw1, _world.characters.state.lowerCaseBits, L"DTW1");
  same(_at.dtw2, _world.characters.state.sentenceStart, L"DTW2");
  same(_at.dtw6, _world.characters.state.alwaysLower, L"DTW6");
  same(_at.lsp, _world.heaps.lsp, L"LSP");
  same(_at.dly, _world.message.delay, L"DLY");
  same(_at.de, _world.message.append, L"de");
  same(_at.las2, _world.status.viewLaser, L"LAS2");
  same(_at.viewByte, _world.spaceView, L"VIEW");
  same(_at.qq11, _world.view, L"QQ11");
  same(_at.ev, _world.explosions, L"EV");
  same(_at.mcnt, _world.flight.mainLoopCounter, L"MCNT");
  same(_at.abraxas, _world.screen.colourBank, L"abraxas");
  same(_at.caravanserai, _world.screen.bitmapMode, L"caravanserai");
  same(_at.dflag, _world.screen.dashboardShown, L"DFLAG");
  same(_at.comc, _world.compass.colour, L"COMC");
  same(_at.tribct, _world.trumbles, L"TRIBCT");
  same(_at.nostm, _world.dust.count, L"NOSTM");

  for (std::size_t index = 0; index < _world.heaps.sun.size(); ++index)
  {
    same(static_cast<std::uint16_t>(_at.lso + index), _world.heaps.sun[index], L"LSO");
  }
  for (std::size_t slot = 0; slot < _world.bubble.blocks.size(); ++slot)
  {
    for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
    {
      same(static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte),
           _world.bubble.blocks[slot][byte], L"K%");
    }
  }
  for (std::size_t index = 0; index < _world.dust.x.size(); ++index)
  {
    same(static_cast<std::uint16_t>(_at.sx + index), _world.dust.x[index], L"SX");
    same(static_cast<std::uint16_t>(_at.sy + index), _world.dust.y[index], L"SY");
    same(static_cast<std::uint16_t>(_at.sz + index), _world.dust.z[index], L"SZ");
  }
  for (std::size_t index = 0; index < 4u; ++index)
  {
    same(static_cast<std::uint16_t>(_at.rand + index), _world.rng.State()[index], L"RAND");
  }
}
} // namespace

TEST_CLASS(TheScreenPrimitives)
{
public:
  /*
   * 6502: ZES1k and ZES2k -- and the byte at offset zero is the whole point.
   *
   * `ZES2k` stores at Y and then counts DOWN, stopping when Y reaches zero, so byte 0 of the page
   * is never written. Entered through `ZES1k` with Y = 0 the first `DEY` wraps to 255 and the
   * whole page goes. Both entries are swept, and the marker is non-zero so that "left alone" and
   * "written as zero" are different answers.
   */
  TEST_METHOD(TheScreenClearMatchesZES1kAndZES2k)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t zes1k = oracle.Label("ZES1k");
    const std::uint16_t zes2k = oracle.Label("ZES2k");
    const std::uint16_t sc = oracle.Label("SC");
    const std::uint16_t screen = ScreenBase(oracle);
    const std::uint8_t screenPage = static_cast<std::uint8_t>(screen >> 8);

    for (const std::uint8_t page : { std::uint8_t{ 0 }, std::uint8_t{ 3 }, std::uint8_t{ 0x1F } })
    {
      // ---- the whole page, through ZES1k ----------------------------------------------------
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        FillScreens(cpu, canvas, screen, 0x7Eu);

        cpu.x = static_cast<std::uint8_t>(screenPage + page);
        Assert::IsTrue(cpu.CallSubroutine(zes1k, 5'000).completed, L"ZES1k returned");

        Elite::ZeroWholePage(canvas, static_cast<std::uint16_t>(page * 256u));

        const std::wstring where = Widen("ZES1k(page " + std::to_string(page) + ")");
        Assert::AreEqual<std::uint32_t>(256u, CompareScreens(cpu, screen, canvas, 0x7Eu, where),
                                        (where + L": the whole page went").c_str());
      }

      // ---- a partial page, through ZES2k ------------------------------------------------------
      for (const std::uint8_t first : { std::uint8_t{ 1 }, std::uint8_t{ 0x3F }, std::uint8_t{ 0xFF } })
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        FillScreens(cpu, canvas, screen, 0x7Eu);

        cpu.memory[sc] = 0u;
        cpu.x = static_cast<std::uint8_t>(screenPage + page);
        cpu.y = first;
        Assert::IsTrue(cpu.CallSubroutine(zes2k, 5'000).completed, L"ZES2k returned");

        Elite::ZeroPageDown(canvas, static_cast<std::uint16_t>(page * 256u), first);

        const std::wstring where =
          Widen("ZES2k(page " + std::to_string(page) + ", from " + std::to_string(first) + ")");
        const std::uint32_t cleared = CompareScreens(cpu, screen, canvas, 0x7Eu, where);
        Assert::AreEqual<std::uint32_t>(first, cleared,
                                        (where + L": bytes 1 to `first`, and byte 0 untouched").c_str());
      }
    }
  }

  /*
   * 6502: mvblockK and mvbllop -- the dashboard, copied from `DSTORE%` into the bitmap.
   *
   * The real source and the real destination, because both now exist: `DSTORE%` carries the
   * dashboard image and `DASHBOARD_IMAGE` is the same 2,240 bytes (§6.78). `wantdials` makes
   * these two calls back to back -- eight whole pages and then `mvbllop` for the remaining &C0 --
   * and the second continues where the first stopped, which is what `V` and `SC` being left
   * advanced buys the original and what the second call's arguments buy the port.
   */
  TEST_METHOD(TheBlockCopyMatchesMvblockK)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t mvblockK = oracle.Label("mvblockK");
    const std::uint16_t mvbllop = oracle.Label("mvbllop");
    const std::uint16_t sc = oracle.Label("SC");
    const std::uint16_t v = oracle.Label("V");
    const std::uint16_t screen = ScreenBase(oracle);

    const std::uint16_t store = 0xEF90u;                     // 6502: DSTORE%
    const std::uint16_t dashboard = 18u * 8u * 40u;          // 6502: DLOC%, as a canvas offset
    const std::uint16_t destination = static_cast<std::uint16_t>(screen + dashboard);

    Cpu6502 cpu = oracle.Fresh();
    Elite::Canvas canvas;
    FillScreens(cpu, canvas, screen, 0x3Bu);

    // 6502: LDX #8 / V = DSTORE% / SC = DLOC% / JSR mvblockK.
    cpu.memory[v] = static_cast<std::uint8_t>(store & 0xFFu);
    cpu.memory[static_cast<std::uint16_t>(v + 1)] = static_cast<std::uint8_t>(store >> 8);
    cpu.memory[sc] = static_cast<std::uint8_t>(destination & 0xFFu);
    cpu.memory[static_cast<std::uint16_t>(sc + 1)] = static_cast<std::uint8_t>(destination >> 8);
    cpu.x = 8u;
    Assert::IsTrue(cpu.CallSubroutine(mvblockK, 60'000).completed, L"mvblockK returned");

    // 6502: LDY #&C0 / LDX #1 / JSR mvbllop -- and V and SC are where the last call left them.
    cpu.y = 0xC0u;
    cpu.x = 1u;
    Assert::IsTrue(cpu.CallSubroutine(mvbllop, 60'000).completed, L"mvbllop returned");

    Elite::CopyPagesDown(canvas, Elite::DASHBOARD_IMAGE.data(), dashboard, 8u, 0u);
    Elite::CopyPagesDown(canvas, Elite::DASHBOARD_IMAGE.data() + 8u * 256u,
                         static_cast<std::uint16_t>(dashboard + 8u * 256u), 1u, 0xC0u);

    CompareScreens(cpu, screen, canvas, 0x3Bu, L"mvblockK");

    /*
     * The hole and the overrun, asserted rather than described.
     *
     * 2,240 bytes are copied and they are not the first 2,240: `mvbllop` stores at Y and counts
     * DOWN to 1, so offset 2,048 is never written and offset 2,240 is. Both bytes of the image
     * are zero, so on screen this is invisible -- and the marker makes it visible here, which is
     * the whole reason a comparison starts from one.
     */
    Assert::AreEqual<std::uint32_t>(0x3Bu, canvas.Read(static_cast<std::uint16_t>(dashboard + 2048u)),
                                    L"offset 2048 is the hole and keeps the marker");
    Assert::AreEqual(Elite::DASHBOARD_IMAGE[2240],
                     canvas.Read(static_cast<std::uint16_t>(dashboard + 2240u)),
                     L"offset 2240 is copied, one past where seven rows end");
    Assert::AreEqual(Elite::DASHBOARD_IMAGE[2047],
                     canvas.Read(static_cast<std::uint16_t>(dashboard + 2047u)), L"the eighth page");
    Assert::AreEqual<std::uint32_t>(0x3Bu, canvas.Read(static_cast<std::uint16_t>(dashboard + 2241u)),
                                    L"and nothing past it");
  }

  /// 6502: BOXS -- the rule across the whole screen, at the two rows the game asks for and a
  /// spread of others.
  TEST_METHOD(TheScreenRuleMatchesBOXS)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t boxs = oracle.Label("BOXS");
    const std::uint16_t screen = ScreenBase(oracle);

    for (const std::uint8_t row : { std::uint8_t{ 0 }, std::uint8_t{ 1 }, std::uint8_t{ 7 },
                                    std::uint8_t{ 8 }, std::uint8_t{ 100 }, std::uint8_t{ 143 },
                                    std::uint8_t{ 199 } })
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      FillScreens(cpu, canvas, screen, 0x11u);

      cpu.x = row;
      Assert::IsTrue(cpu.CallSubroutine(boxs, 20'000).completed, L"BOXS returned");

      Elite::DrawWorkspace draw;
      Elite::DrawScreenRule(canvas, draw, row);

      const std::wstring where = Widen("BOXS(row " + std::to_string(row) + ")");
      Assert::IsTrue(CompareScreens(cpu, screen, canvas, 0x11u, where) > 0u,
                     (where + L": something was drawn").c_str());
    }
  }

  /*
   * 6502: BOXS2 -- the vertical edges, and it EORs.
   *
   * Swept from a screen full of a marker AND run twice on the same canvas, because an EOR that
   * put the screen back is the property the routine has and a STORE does not.
   */
  TEST_METHOD(TheVerticalEdgeMatchesBOXS2)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t boxs2 = oracle.Label("BOXS2");
    const std::uint16_t sc = oracle.Label("SC");
    const std::uint16_t screen = ScreenBase(oracle);

    struct Case
    {
      std::uint16_t cell;
      std::uint8_t pattern, rows;
    };

    const std::vector<Case> CASES = {
      { 3u * 8u, 0x03u, 18u },   // 6502: BOX2's left edge
      { 36u * 8u, 0xC0u, 18u },  // 6502: BOX2's right edge
      { 0u, 0xFFu, 1u },
      { 10u * 8u, 0x5Au, 7u },
    };

    for (const Case& item : CASES)
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      FillScreens(cpu, canvas, screen, 0x66u);

      const std::uint16_t address = static_cast<std::uint16_t>(screen + item.cell);
      cpu.memory[sc] = static_cast<std::uint8_t>(address & 0xFFu);
      cpu.a = item.pattern;
      cpu.y = static_cast<std::uint8_t>(address >> 8);
      cpu.x = item.rows;
      Assert::IsTrue(cpu.CallSubroutine(boxs2, 20'000).completed, L"BOXS2 returned");

      Elite::ToggleVerticalEdge(canvas, item.cell, item.pattern, item.rows);

      const std::wstring where =
        Widen("BOXS2(cell " + std::to_string(item.cell) + ", pattern "
              + std::to_string(item.pattern) + ", rows " + std::to_string(item.rows) + ")");
      CompareScreens(cpu, screen, canvas, 0x66u, where);

      // Twice puts it back, which is what an EOR is for.
      Elite::ToggleVerticalEdge(canvas, item.cell, item.pattern, item.rows);
      for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
      {
        Assert::AreEqual<std::uint32_t>(0x66u, canvas.Read(offset),
                                        (where + L": twice restores the screen").c_str());
      }
    }
  }

  /// 6502: BLUEBAND and BLUEBANDS -- the two coloured bands, which STORE rather than EOR.
  TEST_METHOD(TheColourBandsMatchBLUEBAND)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t blueband = oracle.Label("BLUEBAND");
    const std::uint16_t screen = ScreenBase(oracle);

    Cpu6502 cpu = oracle.Fresh();
    Elite::Canvas canvas;
    FillScreens(cpu, canvas, screen, 0x2Au);

    Assert::IsTrue(cpu.CallSubroutine(blueband, 40'000).completed, L"BLUEBAND returned");
    Elite::DrawColourBands(canvas);

    const std::uint32_t touched = CompareScreens(cpu, screen, canvas, 0x2Au, L"BLUEBAND");
    Assert::AreEqual<std::uint32_t>(2u * 18u * 24u, touched, L"two bands of eighteen by twenty-four");
  }


  /*
   * 6502: BOX2 -- the border, at both of its heights.
   *
   * THE HEIGHT IS A DATA BYTE. `BOX2` opens `LDX #18`, and `TTX66K` falls into it through
   * `LDX #25 / EQUB &2C` -- `BIT abs` swallowing that `LDX`, so the fall-through keeps 25 while a
   * `JSR BOX2` gets 18. This calls the routine both ways: at its label, and two bytes in, which
   * is exactly where the swallowed instruction ends and what the fall-through executes (§6.79).
   *
   * A port that took the height from a constant would agree with the game on one screen and draw
   * seven rows too few or too many on the other.
   */
  TEST_METHOD(TheBorderMatchesBOX2AtBothHeights)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t box2 = oracle.Label("BOX2");
    const std::uint16_t t2 = oracle.Label("T2");
    const std::uint16_t screen = ScreenBase(oracle);

    struct Case
    {
      const char* what;
      std::uint16_t entry;
      std::uint8_t rows;
      bool preset;
    };

    const std::vector<Case> CASES = {
      { "called at its label, which loads 18", box2, Elite::BORDER_ROWS_SPACE_VIEW, false },
      { "fallen into past the LDX, keeping 25", static_cast<std::uint16_t>(box2 + 2),
        Elite::BORDER_ROWS_TEXT_SCREEN, true },
      { "fallen into with something else entirely", static_cast<std::uint16_t>(box2 + 2), 7, true },
    };

    for (const Case& item : CASES)
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      FillScreens(cpu, canvas, screen, 0x4Du);

      cpu.memory[t2] = 0x99u;
      cpu.x = item.preset ? item.rows : std::uint8_t{ 0xA5u };
      Assert::IsTrue(cpu.CallSubroutine(item.entry, 60'000).completed, L"BOX2 returned");

      Elite::DrawWorkspace draw;
      draw.t2 = 0x99u;
      Elite::DrawBorder(canvas, draw, item.rows);

      const std::wstring where = Widen(std::string("BOX2 (") + item.what + ")");
      Assert::IsTrue(CompareScreens(cpu, screen, canvas, 0x4Du, where) > 0u,
                     (where + L": something was drawn").c_str());
      Assert::AreEqual(cpu.memory[t2], draw.t2, (where + L": T2").c_str());
    }
  }
  /*
   * 6502: zonkscanners -- clear "on the scanner" on every ship in the bubble.
   *
   * The sweep covers an empty bubble, a full one, a bubble whose first slot is empty (which stops
   * the walk before it starts), and one with the planet and the sun in it -- both negative types,
   * both skipped, and both with the bit set so that skipping them is visible.
   */
  TEST_METHOD(ForgettingTheBlipsMatchesZonkscanners)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t zonk = oracle.Label("zonkscanners");
    const std::uint16_t frin = oracle.Label("FRIN");
    const std::uint16_t kPercent = oracle.Label("K%");

    const std::vector<std::vector<std::uint8_t>> BUBBLES = {
      {},
      { 1, 2, 3 },
      { 129, 130, 5 },
      { 0, 7, 8 },
      { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 },
    };

    for (std::size_t index = 0; index < BUBBLES.size(); ++index)
    {
      const std::vector<std::uint8_t>& types = BUBBLES[index];

      Cpu6502 cpu = oracle.Fresh();
      Elite::Bubble bubble;

      for (std::size_t slot = 0; slot < bubble.slots.size(); ++slot)
      {
        const std::uint8_t type = (slot < types.size()) ? types[slot] : std::uint8_t{ 0 };
        bubble.slots[slot] = type;
        cpu.memory[static_cast<std::uint16_t>(frin + slot)] = type;
      }

      for (std::size_t slot = 0; slot < bubble.blocks.size(); ++slot)
      {
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          // Every byte marked, and byte 31 with bit 4 set, so clearing it is visible and
          // clearing anything else is a failure.
          const std::uint8_t value =
            (byte == 31u) ? 0xFFu : static_cast<std::uint8_t>(0x40u + byte + slot);
          bubble.blocks[slot][byte] = value;
          cpu.memory[static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = value;
        }
      }

      Assert::IsTrue(cpu.CallSubroutine(zonk, 40'000).completed, L"zonkscanners returned");
      Elite::ForgetScannerBlips(bubble);

      const std::wstring where = Widen("zonkscanners(case " + std::to_string(index) + ")");
      for (std::size_t slot = 0; slot < bubble.blocks.size(); ++slot)
      {
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          const std::uint16_t at =
            static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte);
          Assert::AreEqual(cpu.memory[at], bubble.blocks[slot][byte],
                           (where + L": K% slot " + std::to_wstring(slot) + L" byte "
                            + std::to_wstring(byte)).c_str());
        }
      }
    }
  }
};


TEST_CLASS(TheDashboardScreen)
{
public:
  /*
   * 6502: wantdials -- the dashboard arriving, and the dashboard already being there.
   *
   * The whole routine including `DIALS`, so this is the widest comparison in the slice: the
   * border, the 2,240-byte copy, every blip forgotten, seven dials, the compass, two colour
   * bands and the sprites switched off, against the game doing all of it.
   *
   * BOTH VALUES OF `DFLAG`. The flag skips the expensive half and not the cheap one -- with the
   * dashboard already up, the border, the two mode bytes, the bands and the sprites still happen
   * and only the copy, the blips and `DIALS` are skipped. A port that read the flag as "return
   * early" would agree on the second frame and differ on everything drawn on top of it.
   */
  TEST_METHOD(TheDashboardScreenMatchesWantdials)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t wantdials = oracle.Label("wantdials");
    const std::uint16_t screen = ScreenBase(oracle);

    struct At
    {
      std::uint16_t abraxas, caravanserai, dflag, delta, alp1, alp2, beta, bet1;
      std::uint16_t energy, fsh, ash, qq14, cabtmp, gntmp, altit, mcnt, flh, qq11;
      std::uint16_t comx, comy, comc, many, kPercent, frin, t2;
    } at{};

    at.abraxas = oracle.Label("abraxas");
    at.caravanserai = oracle.Label("caravanserai");
    at.dflag = oracle.Label("DFLAG");
    at.delta = oracle.Label("DELTA");
    at.alp1 = oracle.Label("ALP1");
    at.alp2 = oracle.Label("ALP2");
    at.beta = oracle.Label("BETA");
    at.bet1 = oracle.Label("BET1");
    at.energy = oracle.Label("ENERGY");
    at.fsh = oracle.Label("FSH");
    at.ash = oracle.Label("ASH");
    at.qq14 = oracle.Label("QQ14");
    at.cabtmp = oracle.Label("CABTMP");
    at.gntmp = oracle.Label("GNTMP");
    at.altit = oracle.Label("ALTIT");
    at.mcnt = oracle.Label("MCNT");
    at.flh = oracle.Label("FLH");
    at.qq11 = oracle.Label("QQ11");
    at.comx = oracle.Label("COMX");
    at.comy = oracle.Label("COMY");
    at.comc = oracle.Label("COMC");
    at.many = oracle.Label("MANY");
    at.kPercent = oracle.Label("K%");
    at.frin = oracle.Label("FRIN");
    at.t2 = oracle.Label("T2");

    struct Recorder final : Elite::SightEffects
    {
      std::vector<std::uint8_t> modes;
      std::vector<std::uint8_t> masks;
      std::uint32_t colours = 0;

      void SetRasterMode(std::uint8_t _mode) override { modes.push_back(_mode); }
      void SetSightColour(std::uint8_t) override { ++colours; }
      void SetSpritesEnabled(std::uint8_t _mask) override { masks.push_back(_mask); }
    };

    std::uint32_t compared = 0;
    std::uint32_t copies = 0;

    for (const std::uint8_t already : { std::uint8_t{ 0 }, std::uint8_t{ 0xFF }, std::uint8_t{ 1 } })
    {
      for (const std::uint8_t counter : { std::uint8_t{ 0 }, std::uint8_t{ 2 }, std::uint8_t{ 9 } })
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        cpu.AddTrap(oracle.Label("SETL1"));

        FillScreens(cpu, canvas, screen, 0x00u);

        const std::uint8_t READINGS[] = { 14u, 5u, 128u, 200u, 3u, 180u, 90u, 60u,
                                          40u, 100u, 70u, 120u, 0xFFu };
        const std::uint16_t WHERE[] = { at.delta, at.alp1, at.alp2, at.beta, at.bet1,
                                        at.energy, at.fsh, at.ash, at.qq14, at.cabtmp,
                                        at.gntmp, at.altit, at.flh };
        for (std::size_t index = 0; index < 13u; ++index)
        {
          cpu.memory[WHERE[index]] = READINGS[index];
        }
        cpu.memory[at.mcnt] = counter;
        cpu.memory[at.qq11] = 0u;
        cpu.memory[at.dflag] = already;
        cpu.memory[at.abraxas] = 0x81u;
        cpu.memory[at.caravanserai] = 0xC0u;
        cpu.memory[at.t2] = 0x77u;

        Elite::Bubble bubble;
        bubble.counts[Elite::SHIP_TYPE_STATION] = 1u;
        cpu.memory[static_cast<std::uint16_t>(at.many + Elite::SHIP_TYPE_STATION)] = 1u;

        // Two ships in the bubble, both with bit 4 of byte 31 set, so `zonkscanners` has
        // something to forget -- and the DFLAG case that skips it has to leave it alone.
        const std::uint8_t TYPES[] = { 3u, 5u };
        for (std::size_t slot = 0; slot < 2u; ++slot)
        {
          bubble.slots[slot] = TYPES[slot];
          cpu.memory[static_cast<std::uint16_t>(at.frin + slot)] = TYPES[slot];
        }

        std::uint32_t state = 0x77C1A305u ^ (counter * 0x9E3779B9u) ^ already;
        for (std::size_t slot = 0; slot < 2u; ++slot)
        {
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            state = state * 1103515245u + 12345u;
            const std::uint8_t value =
              (byte == 31u) ? 0xFFu : static_cast<std::uint8_t>(state >> 17);
            bubble.blocks[slot][byte] = value;
            cpu.memory[static_cast<std::uint16_t>(at.kPercent + slot * Elite::SHIP_BLOCK_SIZE
                                                  + byte)] = value;
          }
        }

        Elite::Compass compass{ 0xC3u, 0x9Cu, Elite::COMPASS_AHEAD };
        cpu.memory[at.comx] = compass.x;
        cpu.memory[at.comy] = compass.y;
        cpu.memory[at.comc] = compass.colour;

        const Elite::Testing::RunResult run = cpu.CallSubroutine(wantdials, 400'000);
        Assert::IsTrue(run.completed, L"wantdials returned");

        Elite::DrawWorkspace draw;
        Elite::MathWorkspace math;
        Elite::GeometryWorkspace geometry;
        Elite::ScreenState screenState;
        screenState.dashboardShown = already;
        draw.t2 = 0x77u;

        Elite::FlightState flight;
        flight.delta = READINGS[0];
        flight.alp1 = READINGS[1];
        flight.alp2 = READINGS[2];
        flight.beta = READINGS[3];
        flight.bet1 = READINGS[4];
        flight.mainLoopCounter = counter;

        Elite::FlightStatus status;
        status.energy = READINGS[5];
        status.forwardShield = READINGS[6];
        status.aftShield = READINGS[7];
        status.cabinTemperature = READINGS[9];
        status.laserTemperature = READINGS[10];
        status.altitude = READINGS[11];
        status.damageFlash = READINGS[12];

        Recorder effects;
        Elite::ShowDashboard(canvas, draw, math, geometry, screenState, bubble, flight, status,
                             READINGS[8], compass, effects);

        const std::wstring where =
          Widen("wantdials(DFLAG " + std::to_string(already) + ", MCNT " + std::to_string(counter) + ")");

        const std::uint32_t touched = CompareScreens(cpu, screen, canvas, 0x00u, where);

        Assert::AreEqual(cpu.memory[at.abraxas], screenState.colourBank, (where + L": abraxas").c_str());
        Assert::AreEqual(cpu.memory[at.caravanserai], screenState.bitmapMode,
                         (where + L": caravanserai").c_str());
        Assert::AreEqual(cpu.memory[at.dflag], screenState.dashboardShown, (where + L": DFLAG").c_str());
        Assert::AreEqual(cpu.memory[at.comx], compass.x, (where + L": COMX").c_str());
        Assert::AreEqual(cpu.memory[at.comy], compass.y, (where + L": COMY").c_str());
        Assert::AreEqual(cpu.memory[at.comc], compass.colour, (where + L": COMC").c_str());
        Assert::AreEqual(cpu.memory[at.t2], draw.t2, (where + L": T2").c_str());

        for (std::size_t slot = 0; slot < 2u; ++slot)
        {
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            const std::uint16_t address =
              static_cast<std::uint16_t>(at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte);
            Assert::AreEqual(cpu.memory[address], bubble.blocks[slot][byte],
                             (where + L": K% slot " + std::to_wstring(slot) + L" byte "
                              + std::to_wstring(byte)).c_str());
          }
        }

        // `NOSPRITES` runs either way, so the seam sees the same three calls whatever `DFLAG` is.
        Assert::AreEqual<std::size_t>(2u, effects.modes.size(), (where + L": two raster switches").c_str());
        Assert::AreEqual<std::size_t>(1u, effects.masks.size(), (where + L": one sprite mask").c_str());
        Assert::AreEqual<std::uint32_t>(0u, effects.masks[0], (where + L": and it is zero").c_str());
        Assert::AreEqual<std::size_t>(2u, cpu.trapHits.size(), (where + L": the game switched twice").c_str());

        Assert::IsTrue(touched > 0u, (where + L": something was drawn").c_str());
        copies += (already == 0u) ? 1u : 0u;
        ++compared;
      }
    }

    Assert::AreEqual<std::uint32_t>(9u, compared, L"the whole sweep ran");
    Assert::IsTrue(copies > 0u && copies < compared, L"both halves of the DFLAG test were taken");
  }


  /*
   * 6502: TTX66K -- the whole screen set up, for every shape of view it distinguishes.
   *
   * Views 0 and 13 tail-jump into `wantdials` and never reach anything below that test, so those
   * two cases are the dashboard again through a different door. Views 2, 64 and 128 get one band
   * of colour cells and everything else gets two. And every path ends by falling into `BOX2` past
   * its `LDX #18`, so the border is 25 rows rather than 18 (§6.79).
   *
   * Three separate clears in three different shapes, and the marker is what makes them
   * distinguishable: a byte the routine zeroed and a byte it never touched are the same thing on
   * a screen that started at zero.
   */
  TEST_METHOD(TheWholeScreenMatchesTTX66K)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t ttx66k = oracle.Label("TTX66K");
    const std::uint16_t screen = ScreenBase(oracle);
    const std::uint16_t abraxas = oracle.Label("abraxas");
    const std::uint16_t caravanserai = oracle.Label("caravanserai");
    const std::uint16_t dflag = oracle.Label("DFLAG");
    const std::uint16_t comc = oracle.Label("COMC");
    const std::uint16_t xc = oracle.Label("XC");
    const std::uint16_t yc = oracle.Label("YC");
    const std::uint16_t qq11 = oracle.Label("QQ11");
    const std::uint16_t frin = oracle.Label("FRIN");
    const std::uint16_t kPercent = oracle.Label("K%");
    const std::uint16_t many = oracle.Label("MANY");
    const std::uint16_t comx = oracle.Label("COMX");
    const std::uint16_t comy = oracle.Label("COMY");
    const std::uint16_t t2 = oracle.Label("T2");
    const std::uint16_t mcnt = oracle.Label("MCNT");

    struct Recorder final : Elite::SightEffects
    {
      std::vector<std::uint8_t> modes;
      std::vector<std::uint8_t> masks;
      void SetRasterMode(std::uint8_t _mode) override { modes.push_back(_mode); }
      void SetSightColour(std::uint8_t) override {}
      void SetSpritesEnabled(std::uint8_t _mask) override { masks.push_back(_mask); }
    };

    std::uint32_t compared = 0;
    std::uint32_t dashboards = 0;
    std::uint32_t oneBand = 0;

    for (const std::uint8_t view : { std::uint8_t{ 0 }, std::uint8_t{ 1 }, std::uint8_t{ 2 },
                                     std::uint8_t{ 6 }, std::uint8_t{ 13 }, std::uint8_t{ 64 },
                                     std::uint8_t{ 128 }, std::uint8_t{ 255 } })
    {
      for (const std::uint8_t already : { std::uint8_t{ 0 }, std::uint8_t{ 0xFF } })
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        cpu.AddTrap(oracle.Label("SETL1"));

        FillScreens(cpu, canvas, screen, 0x1Du);

        cpu.memory[qq11] = view;
        cpu.memory[dflag] = already;
        cpu.memory[abraxas] = 0x33u;
        cpu.memory[caravanserai] = 0x44u;
        cpu.memory[comc] = 0x55u;
        cpu.memory[xc] = 0x66u;
        cpu.memory[yc] = 0x77u;
        cpu.memory[t2] = 0x88u;
        cpu.memory[mcnt] = 0u;
        cpu.memory[comx] = 0xC3u;
        cpu.memory[comy] = 0x9Cu;

        Elite::Bubble bubble;
        bubble.counts[Elite::SHIP_TYPE_STATION] = 1u;
        cpu.memory[static_cast<std::uint16_t>(many + Elite::SHIP_TYPE_STATION)] = 1u;

        const std::uint8_t TYPES[] = { 3u, 5u };
        for (std::size_t slot = 0; slot < 2u; ++slot)
        {
          bubble.slots[slot] = TYPES[slot];
          cpu.memory[static_cast<std::uint16_t>(frin + slot)] = TYPES[slot];
        }

        std::uint32_t state = 0x2B91D6C5u ^ (view * 0x9E3779B9u) ^ already;
        for (std::size_t slot = 0; slot < 2u; ++slot)
        {
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            state = state * 1103515245u + 12345u;
            const std::uint8_t value =
              (byte == 31u) ? 0xFFu : static_cast<std::uint8_t>(state >> 17);
            bubble.blocks[slot][byte] = value;
            cpu.memory[static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE
                                                  + byte)] = value;
          }
        }

        const Elite::Testing::RunResult run = cpu.CallSubroutine(ttx66k, 400'000);
        Assert::IsTrue(run.completed, L"TTX66K returned");

        Elite::DrawWorkspace draw;
        Elite::MathWorkspace math;
        Elite::GeometryWorkspace geometry;
        Elite::TextState textState;
        Elite::ScreenState screenState;
        Elite::Compass compass{ 0xC3u, 0x9Cu, 0x55u };
        Elite::FlightState flight;
        Elite::FlightStatus status;
        Recorder effects;

        screenState.colourBank = 0x33u;
        screenState.bitmapMode = 0x44u;
        screenState.dashboardShown = already;
        textState.column = 0x66u;
        textState.row = 0x77u;
        draw.t2 = 0x88u;

        Elite::SetUpScreenPixels(canvas, draw, math, geometry, textState, screenState, bubble,
                                 flight, status, 0u, compass, effects, view);

        const std::wstring where =
          Widen("TTX66K(QQ11 " + std::to_string(view) + ", DFLAG " + std::to_string(already) + ")");

        Assert::IsTrue(CompareScreens(cpu, screen, canvas, 0x1Du, where) > 0u,
                       (where + L": something was drawn").c_str());
        Assert::AreEqual(cpu.memory[abraxas], screenState.colourBank, (where + L": abraxas").c_str());
        Assert::AreEqual(cpu.memory[caravanserai], screenState.bitmapMode,
                         (where + L": caravanserai").c_str());
        Assert::AreEqual(cpu.memory[dflag], screenState.dashboardShown, (where + L": DFLAG").c_str());
        Assert::AreEqual(cpu.memory[comc], compass.colour, (where + L": COMC").c_str());
        Assert::AreEqual(cpu.memory[xc], textState.column, (where + L": XC").c_str());
        Assert::AreEqual(cpu.memory[yc], textState.row, (where + L": YC").c_str());
        Assert::AreEqual(cpu.memory[t2], draw.t2, (where + L": T2").c_str());

        for (std::size_t slot = 0; slot < 2u; ++slot)
        {
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            const std::uint16_t address =
              static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte);
            Assert::AreEqual(cpu.memory[address], bubble.blocks[slot][byte],
                             (where + L": K% slot " + std::to_wstring(slot) + L" byte "
                              + std::to_wstring(byte)).c_str());
          }
        }

        Assert::AreEqual<std::size_t>(cpu.trapHits.size(), effects.modes.size(),
                                      (where + L": the same number of raster switches").c_str());

        dashboards += (view == 0u || view == 13u) ? 1u : 0u;
        oneBand += (view == 2u || view == 64u || view == 128u) ? 1u : 0u;
        ++compared;
      }
    }

    Assert::AreEqual<std::uint32_t>(8u * 2u, compared, L"the whole sweep ran");
    Assert::IsTrue(dashboards > 0u, L"the tail call into wantdials was taken");
    Assert::IsTrue(oneBand > 0u, L"and the one-band views were covered");
  }
};


TEST_CLASS(TheScreenChange)
{
public:
  /*
   * 6502: TT66, which is `STA QQ11` and falls into TTX66 -- the whole routine, at last.
   *
   * `CHPR` IS NOT TRAPPED. The view's name and the hyperspace countdown are printed onto the
   * screen the routine has just cleared, so they belong in the whole-canvas compare with
   * everything else; trapping them would turn the one part of this routine that produces TEXT
   * into a character stream compared separately from the pixels around it.
   *
   * The sweep covers the space view (where the name is printed and the dashboard comes back),
   * view 13 (which takes the same tail call), and four text screens; both settings of `DFLAG`;
   * every one of the four space views, because the name is `VIEW ORA #&60`; and a countdown of
   * zero, which skips `ee3` entirely, against two that do not.
   */
  TEST_METHOD(TheScreenChangeMatchesTT66)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Where at(oracle);
    const std::uint16_t tt66 = oracle.Label("TT66");

    std::uint32_t compared = 0;
    std::uint32_t named = 0;
    std::uint32_t counted = 0;

    for (const std::uint8_t view : { std::uint8_t{ 0 }, std::uint8_t{ 1 }, std::uint8_t{ 6 },
                                     std::uint8_t{ 13 }, std::uint8_t{ 64 }, std::uint8_t{ 128 } })
    {
      for (std::uint8_t spaceView = 0; spaceView < 4u; ++spaceView)
      {
        for (const std::uint8_t countdown : { std::uint8_t{ 0 }, std::uint8_t{ 3 },
                                              std::uint8_t{ 15 } })
        {
          World world;
          Seed(world, view * 97u + spaceView * 13u + countdown);
          world.spaceView = spaceView;
          world.view = 0xEEu; // whatever was up before, which `TT66` overwrites
          world.status.hyperspaceCountdown = countdown;

          Cpu6502 cpu = oracle.Fresh();
          cpu.AddTrap(oracle.Label("SETL1"));
          FillScreens(cpu, world.canvas, at.screen, 0x1Du);
          Mirror(world, cpu, at);

          cpu.a = view;
          const Elite::Testing::RunResult run = cpu.CallSubroutine(tt66, 900'000);
          Assert::IsTrue(run.completed, L"TT66 returned");

          Elite::FlightScreen screen = world.Screen();
          Elite::SetUpScreen(screen, view);

          const std::wstring where =
            Widen("TT66(QQ11 " + std::to_string(view) + ", VIEW " + std::to_string(spaceView)
                  + ", QQ22+1 " + std::to_string(countdown) + ")");

          CompareScreens(cpu, at.screen, world.canvas, 0x1Du, where);
          CompareState(cpu, world, at, where);

          named += (view == 0u) ? 1u : 0u;
          counted += (countdown != 0u) ? 1u : 0u;
          ++compared;
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(6u * 4u * 3u, compared, L"the whole sweep ran");
    Assert::IsTrue(named > 0u, L"the view's name was printed on some passes");
    Assert::IsTrue(counted > 0u, L"and the countdown on some");
  }

  /*
   * 6502: LOOK1, with LQ and LO2 -- all three exits.
   *
   * The one that is easy to get wrong is `LO2`: on the space view, asked for the view already
   * showing, the routine does the palette change and NOTHING else. A port that fell through to
   * the clear would redraw the screen on every press of a view key that changed nothing, which
   * looks almost right.
   */
  TEST_METHOD(TheViewChangeMatchesLOOK1)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Where at(oracle);
    const std::uint16_t look1 = oracle.Label("LOOK1");

    std::uint32_t compared = 0;
    std::uint32_t unchanged = 0;
    std::uint32_t reseeded = 0;

    for (const std::uint8_t view : { std::uint8_t{ 0 }, std::uint8_t{ 1 }, std::uint8_t{ 64 } })
    {
      for (std::uint8_t from = 0; from < 4u; ++from)
      {
        for (std::uint8_t to = 0; to < 4u; ++to)
        {
          World world;
          Seed(world, view * 31u + from * 7u + to);
          world.view = view;
          world.spaceView = from;

          Cpu6502 cpu = oracle.Fresh();
          cpu.AddTrap(oracle.Label("SETL1"));
          cpu.AddTrap(oracle.Label("DOVDU19"));
          FillScreens(cpu, world.canvas, at.screen, 0x1Du);
          Mirror(world, cpu, at);

          cpu.x = to;
          const Elite::Testing::RunResult run = cpu.CallSubroutine(look1, 900'000);
          Assert::IsTrue(run.completed, L"LOOK1 returned");

          Elite::FlightScreen screen = world.Screen();
          Elite::ChangeView(screen, to);

          const std::wstring where =
            Widen("LOOK1(QQ11 " + std::to_string(view) + ", VIEW " + std::to_string(from)
                  + " -> " + std::to_string(to) + ")");

          CompareScreens(cpu, at.screen, world.canvas, 0x1Du, where);
          CompareState(cpu, world, at, where);

          // The palette change happens on every path, including the one that does nothing else.
          Assert::AreEqual<std::size_t>(1u, world.effects.palettes.size(),
                                        (where + L": one palette change").c_str());
          Assert::AreEqual<std::uint32_t>(0u, world.effects.palettes[0],
                                          (where + L": and it asks for zero").c_str());

          if (view == 0u && to == from)
          {
            ++unchanged;
          }
          if (view != 0u)
          {
            ++reseeded;
          }
          ++compared;
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(3u * 4u * 4u, compared, L"the whole sweep ran");
    Assert::IsTrue(unchanged > 0u, L"LO2's do-nothing path was taken");
    Assert::IsTrue(reseeded > 0u, L"and LQ's reseeding path");
  }

  /*
   * 6502: WARP -- the "J" key, and it refuses more often than it agrees.
   *
   * Four refusals and one jump. The sweep covers each refusal on its own -- junk in the slot two
   * above the junk count, a station in the bubble, witchspace, and either body closer than two
   * units -- and the negative-sign case for each body, which skips its distance test because a
   * body behind you cannot be flown into.
   */
  TEST_METHOD(TheWarpMatchesWARP)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Where at(oracle);
    const std::uint16_t warp = oracle.Label("WARP");

    struct Case
    {
      const char* what;
      std::uint8_t junk, occupant, station, midJump;
      std::uint8_t planetSign, planetHigh, sunSign, sunHigh;
    };

    const std::vector<Case> CASES = {
      { "clear space, both bodies far", 1, 0, 0, 0, 0x30, 0x40, 0x30, 0x40 },
      { "a ship above the junk", 1, 7, 0, 0, 0x30, 0x40, 0x30, 0x40 },
      { "a station in the bubble", 1, 0, 1, 0, 0x30, 0x40, 0x30, 0x40 },
      { "witchspace", 1, 0, 0, 0xFF, 0x30, 0x40, 0x30, 0x40 },
      { "the planet too close", 1, 0, 0, 0, 0x01, 0x00, 0x30, 0x40 },
      { "the sun too close", 1, 0, 0, 0, 0x30, 0x40, 0x01, 0x00 },
      { "the planet behind you", 1, 0, 0, 0, 0x81, 0x00, 0x30, 0x40 },
      { "the sun behind you", 1, 0, 0, 0, 0x30, 0x40, 0x81, 0x00 },
      { "both behind you", 1, 0, 0, 0, 0x81, 0x00, 0x81, 0x00 },
      { "no junk at all", 0, 0, 0, 0, 0x30, 0x40, 0x30, 0x40 },
      { "junk right up to the slot", 3, 0, 0, 0, 0x30, 0x40, 0x30, 0x40 },

      /*
       * The `CMP #2 / BCC WA1` boundary, from both sides and on both bodies. `MAS2` ORs the three
       * sign bytes and drops bit 7, so a sign byte of 2 IS a largest axis of 2 -- which is the
       * smallest distance the routine will still warp from. Without these four the comparison
       * cannot tell `< 2` from `< 3`, which is what the mutation sweep found.
       */
      { "the planet at exactly two", 1, 0, 0, 0, 0x02, 0x00, 0x30, 0x40 },
      { "the planet at one below two", 1, 0, 0, 0, 0x01, 0x00, 0x30, 0x40 },
      { "the sun at exactly two", 1, 0, 0, 0, 0x30, 0x40, 0x02, 0x00 },
      { "the sun at one below two", 1, 0, 0, 0, 0x30, 0x40, 0x01, 0x00 },
      { "both at exactly two", 1, 0, 0, 0, 0x02, 0x00, 0x02, 0x00 },
    };

    std::uint32_t jumped = 0;
    std::uint32_t refused = 0;

    for (const Case& item : CASES)
    {
      World world;
      Seed(world, 0x5Au);
      world.view = 0u;
      world.spaceView = 1u;
      world.bubble.junk = item.junk;
      world.bubble.counts[Elite::SHIP_TYPE_STATION] = item.station;
      world.status.midJump = item.midJump;

      for (std::size_t slot = 0; slot < world.bubble.slots.size(); ++slot)
      {
        world.bubble.slots[slot] = 0u;
      }
      world.bubble.slots[0] = 128u; // the planet
      world.bubble.slots[1] = 129u; // the sun
      const std::size_t above = static_cast<std::size_t>(item.junk) + 2u;
      if (above < world.bubble.slots.size())
      {
        world.bubble.slots[above] = item.occupant;
      }

      world.bubble.blocks[0][2] = 0u;
      world.bubble.blocks[0][5] = 0u;
      world.bubble.blocks[0][8] = item.planetSign;
      world.bubble.blocks[0][7] = item.planetHigh;
      world.bubble.blocks[1][2] = 0u;
      world.bubble.blocks[1][5] = 0u;
      world.bubble.blocks[1][8] = item.sunSign;
      world.bubble.blocks[1][7] = item.sunHigh;

      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(oracle.Label("SETL1"));
      cpu.AddTrap(oracle.Label("DOVDU19"));
      cpu.AddTrap(oracle.Label("NOISE"));
      FillScreens(cpu, world.canvas, at.screen, 0x1Du);
      Mirror(world, cpu, at);

      const Elite::Testing::RunResult run = cpu.CallSubroutine(warp, 900'000);
      Assert::IsTrue(run.completed, L"WARP returned");

      Elite::FlightScreen screen = world.Screen();
      Elite::Warp(screen);

      const std::wstring where = Widen(std::string("WARP (") + item.what + ")");

      CompareScreens(cpu, at.screen, world.canvas, 0x1Du, where);
      CompareState(cpu, world, at, where);

      // The refusal noise is the seam, and the game asking for it is a trap hit at `NOISE`.
      std::size_t noises = 0;
      for (const Cpu6502::TrapHit& hit : cpu.trapHits)
      {
        noises += (hit.address == oracle.Label("NOISE")) ? 1u : 0u;
      }
      Assert::AreEqual<std::size_t>(noises, world.effects.sounds.size(),
                                    (where + L": the same number of refusals").c_str());
      for (const std::uint8_t effect : world.effects.sounds)
      {
        Assert::AreEqual<std::uint32_t>(Elite::SOUND_BOOP, effect, (where + L": sfxboop").c_str());
      }

      if (world.effects.sounds.empty())
      {
        ++jumped;
      }
      else
      {
        ++refused;
      }
    }

    Assert::IsTrue(jumped > 0u, L"some warps were allowed");
    Assert::IsTrue(refused > 0u, L"and some refused");
  }
};

} // namespace GameLogicTests
