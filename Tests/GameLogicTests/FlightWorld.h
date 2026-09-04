#pragma once

#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "Canvas.h"
#include "Commander.h"
#include "Controls.h"
#include "Dashboard.h"
#include "ExtendedTokens.h"
#include "LookupTables.h"
#include "PlanetDraw.h"
#include "Rng.h"
#include "ShipDraw.h"
#include "ShipMove.h"
#include "ShipSlot.h"
#include "Stardust.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "ViewChange.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

/*
 * The port's whole flight world, and the oracle's memory beside it.
 *
 * Shared by `ViewChangeTests` and `FlightLoopTests` because both compare routines that reach all
 * of it: `TT66` clears everything drawn on the screen, and the flight loop drives everything that
 * draws. Building it twice would be the same eighteen arguments in two disguises.
 */
namespace GameLogicTests
{

using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

inline bool OracleMissing()
{
  const OracleImage& oracle = OracleImage::Instance();
  if (oracle.Available())
  {
    return false;
  }
  Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(
    ("SKIPPED -- oracle absent: " + oracle.Reason()).c_str());
  return true;
}

inline std::wstring WidenText(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

inline std::uint16_t ScreenBase(const OracleImage& _oracle)
{
  const Cpu6502 image = _oracle.Fresh();
  return static_cast<std::uint16_t>(
    (image.memory[_oracle.Label("ylookupl")] | (image.memory[_oracle.Label("ylookuph")] << 8))
    - 0x20);
}

inline void FillScreens(Cpu6502& _cpu, Elite::Canvas& _canvas, std::uint16_t _base,
                        std::uint8_t _marker)
{
  std::memset(&_cpu.memory[_base], _marker, Elite::Canvas::SCREEN_SIZE);
  for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
  {
    _canvas.Write(offset, _marker);
  }
}

inline std::uint32_t CompareScreens(const Cpu6502& _cpu, std::uint16_t _base,
                                    const Elite::Canvas& _canvas, std::uint8_t _marker,
                                    const std::wstring& _context)
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
  bool PlaySound(std::uint8_t _effect) override { sounds.push_back(_effect); return true; }
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
inline void Seed(World& _world, std::uint32_t _seed)
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
  std::uint16_t dtw3, dtw4, dtw5, dtw8;
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
    dtw2 = _oracle.Label("DTW2");        dtw6 = _oracle.Label("DTW6");        dtw3 = _oracle.Label("DTW3");
    dtw4 = _oracle.Label("DTW4");        dtw5 = _oracle.Label("DTW5");
    dtw8 = _oracle.Label("DTW8");
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
inline void Mirror(const World& _world, Cpu6502& _cpu, const Where& _at)
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

  /*
   * The rest of the extended printer's state, which no screen routine reads but `MESS` does:
   * it turns the justifier into a measuring device (`DTW4` = %11000000, print, read `DTW5`) and
   * a stale byte in either would centre the message in the wrong column. `DTW7` is not in this
   * build -- the Master's literal-character byte has no C64 label.
   */
  _cpu.memory[_at.dtw3] = _world.characters.state.toLineBuffer;
  _cpu.memory[_at.dtw4] = _world.characters.state.justify;
  _cpu.memory[_at.dtw5] = _world.characters.state.bufferLength;
  _cpu.memory[_at.dtw8] = _world.characters.state.caseMask;

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
inline void CompareState(const Cpu6502& _cpu, const World& _world, const Where& _at,
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

} // namespace GameLogicTests
