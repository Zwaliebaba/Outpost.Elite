#include "pch.h"

#include "UniverseImage.h"

#include "Commander.h"
#include "ShipBlueprint.h"
#include "ShipSlot.h"
#include "VideoState.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

  namespace
  {

    /// A cell over one byte the port holds directly.
    Cell Direct(const wchar_t* _name, std::uint16_t _address, std::uint8_t& _byte, CellScope _scope)
    {
      Cell cell;
      cell.name = _name;
      cell.address = _address;
      cell.scope = _scope;
      std::uint8_t* at = &_byte;
      cell.get = [at]() { return *at; };
      cell.set = [at](std::uint8_t _value) { *at = _value; };
      return cell;
    }

    /// A run of directly held bytes, one cell each, named with their index.
    /// A ship's thirty-seven bytes, through the codec: each cell reads `ToBytes()` and writes back
    /// through `FromBytes`, so the image is the K% layout whatever the struct's own layout is.
    void ShipCells(std::vector<Cell>& _cells, const std::wstring& _name, std::uint16_t _base, Elite::Ship& _ship, CellScope _scope)
    {
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        Cell cell;
        cell.name = _name + L" byte " + std::to_wstring(byte);
        cell.address = static_cast<std::uint16_t>(_base + byte);
        cell.scope = _scope;
        Elite::Ship* ship = &_ship;
        cell.get = [ship, byte]() { return ship->ToBytes()[byte]; };
        cell.set = [ship, byte](std::uint8_t _value)
        {
          std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> bytes = ship->ToBytes();
          bytes[byte] = _value;
          *ship = Elite::Ship::FromBytes(bytes);
        };
        _cells.push_back(std::move(cell));
      }
    }

    void Run(std::vector<Cell>& _cells, const wchar_t* _name, std::uint16_t _base, std::uint8_t* _bytes, std::size_t _count,
             CellScope _scope)
    {
      for (std::size_t index = 0; index < _count; ++index)
      {
        Cell cell = Direct(_name, static_cast<std::uint16_t>(_base + index), _bytes[index], _scope);
        cell.name += L"[" + std::to_wstring(index) + L"]";
        _cells.push_back(std::move(cell));
      }
    }

    /// The two bytes of a sixteen-bit value the port holds whole and the game holds low byte first.
    void Pair(std::vector<Cell>& _cells, const wchar_t* _low, const wchar_t* _high, std::uint16_t _address, std::uint16_t& _value,
              CellScope _scope)
    {
      std::uint16_t* at = &_value;

      Cell low;
      low.name = _low;
      low.address = _address;
      low.scope = _scope;
      low.get = [at]() { return static_cast<std::uint8_t>(*at & 0xFFu); };
      low.set = [at](std::uint8_t _byte) { *at = static_cast<std::uint16_t>((*at & 0xFF00u) | _byte); };
      _cells.push_back(std::move(low));

      Cell high;
      high.name = _high;
      high.address = static_cast<std::uint16_t>(_address + 1u);
      high.scope = _scope;
      high.get = [at]() { return static_cast<std::uint8_t>(*at >> 8); };
      high.set = [at](std::uint8_t _byte) { *at = static_cast<std::uint16_t>((*at & 0x00FFu) | (_byte << 8)); };
      _cells.push_back(std::move(high));
    }

  } // namespace

  std::vector<Cell> ImageCells(Universe& _universe, const Where& _at)
  {
    std::vector<Cell> cells;
    cells.reserve(1200);
    Universe* universe = &_universe;

    // ---- compared, in the order `CompareState` checked them -----------------------------------------

    cells.push_back(Direct(L"XC", _at.xc, _universe.text.column, CellScope::Compared));
    cells.push_back(Direct(L"YC", _at.yc, _universe.text.row, CellScope::Compared));
    {
      // 6502: QQ17 -- behind a getter and a setter on the token printer.
      Cell cell;
      cell.name = L"QQ17";
      cell.address = _at.qq17;
      cell.scope = CellScope::Compared;
      cell.get = [universe]() { return universe->printer.CaseFlags(); };
      cell.set = [universe](std::uint8_t _flags) { universe->printer.SetCaseFlags(_flags); };
      cells.push_back(std::move(cell));
    }
    cells.push_back(Direct(L"DTW1", _at.dtw1, _universe.characters.state.lowerCaseBits, CellScope::Compared));
    cells.push_back(Direct(L"DTW2", _at.dtw2, _universe.characters.state.sentenceStart, CellScope::Compared));
    cells.push_back(Direct(L"DTW6", _at.dtw6, _universe.characters.state.alwaysLower, CellScope::Compared));
    cells.push_back(Direct(L"LSP", _at.lsp, _universe.heaps.lsp, CellScope::Compared));
    cells.push_back(Direct(L"DLY", _at.dly, _universe.message.delay, CellScope::Compared));
    cells.push_back(Direct(L"de", _at.de, _universe.message.append, CellScope::Compared));
    cells.push_back(Direct(L"LAS2", _at.las2, _universe.status.viewLaser, CellScope::Compared));
    cells.push_back(Direct(L"VIEW", _at.viewByte, _universe.spaceView, CellScope::Compared));
    cells.push_back(Direct(L"QQ11", _at.qq11, _universe.view, CellScope::Compared));
    cells.push_back(Direct(L"EV", _at.ev, _universe.explosions, CellScope::Compared));
    cells.push_back(Direct(L"MCNT", _at.mcnt, _universe.flight.mainLoopCounter, CellScope::Compared));
    Pair(cells, L"XX0", L"XX0+1", _at.xx0, _universe.flight.blueprint, CellScope::Compared);
    cells.push_back(Direct(L"abraxas", _at.abraxas, _universe.screen.colourBank, CellScope::Compared));
    cells.push_back(Direct(L"caravanserai", _at.caravanserai, _universe.screen.bitmapMode, CellScope::Compared));
    cells.push_back(Direct(L"DFLAG", _at.dflag, _universe.screen.dashboardShown, CellScope::Compared));
    cells.push_back(Direct(L"COMC", _at.comc, _universe.compass.colour, CellScope::Compared));
    cells.push_back(Direct(L"TRIBCT", _at.tribct, _universe.trumbles.count, CellScope::Compared));
    for (std::size_t index = 0; index < _universe.trumbles.velocityX.size(); ++index)
    {
      const std::uint16_t offset = static_cast<std::uint16_t>(index);
      cells.push_back(Direct(L"TRIBVX", static_cast<std::uint16_t>(_at.tribvx + offset), _universe.trumbles.velocityX[index],
                             CellScope::Compared));
      cells.push_back(Direct(L"TRIBVXH", static_cast<std::uint16_t>(_at.tribvxh + offset), _universe.trumbles.velocityXHigh[index],
                             CellScope::Compared));
      cells.push_back(Direct(L"TRIBXH", static_cast<std::uint16_t>(_at.tribxh + offset), _universe.trumbles.coordinateXHigh[index],
                             CellScope::Compared));
    }

    /*
     * The VIC-II sprite coordinates, and ONLY when the fixture has claimed them (slice 4d-a).
     *
     * In the flat image the VIC-II is `XX21` (§6.108), so these addresses hold the blueprint
     * pointers for ship types 3 to 9 on one side and sprite registers on the other. A fixture sets
     * `spriteRegistersAreOurs` when it intends `MVTRIBS` to run and promises to draw no ship after;
     * every other fixture leaves the bytes alone, and so does this table. The nine-bit x is one
     * value on the port's side and a low byte per sprite plus one shared high-bit byte on the game's.
     */
    if (_universe.spriteRegistersAreOurs)
    {
      for (std::size_t sprite = Elite::FIRST_TRUMBLE_SPRITE; sprite < Elite::SPRITE_COUNT; ++sprite)
      {
        const std::uint16_t at = static_cast<std::uint16_t>(_at.vic + 2u * sprite);

        Cell low;
        low.name = L"sprite " + std::to_wstring(sprite) + L" x";
        low.address = at;
        low.scope = CellScope::Compared;
        low.get = [universe, sprite]() { return static_cast<std::uint8_t>(universe->video.x[sprite] & 0xFFu); };
        low.set = [universe, sprite](std::uint8_t _byte)
        { universe->video.x[sprite] = static_cast<std::uint16_t>((universe->video.x[sprite] & 0x100u) | _byte); };
        cells.push_back(std::move(low));

        cells.push_back(Direct(L"sprite y", static_cast<std::uint16_t>(at + 1u), _universe.video.y[sprite], CellScope::Compared));
      }

      Cell shared;
      shared.name = L"sprite x high bits";
      shared.address = static_cast<std::uint16_t>(_at.vic + 0x10u);
      shared.scope = CellScope::Compared;
      shared.mask = static_cast<std::uint8_t>(0xFFu << Elite::FIRST_TRUMBLE_SPRITE);
      shared.get = [universe]()
      {
        std::uint8_t bits = 0;
        for (std::size_t sprite = Elite::FIRST_TRUMBLE_SPRITE; sprite < Elite::SPRITE_COUNT; ++sprite)
        {
          if ((universe->video.x[sprite] & 0x100u) != 0u)
          {
            bits = static_cast<std::uint8_t>(bits | (1u << sprite));
          }
        }
        return bits;
      };
      shared.set = [universe](std::uint8_t _bits)
      {
        for (std::size_t sprite = Elite::FIRST_TRUMBLE_SPRITE; sprite < Elite::SPRITE_COUNT; ++sprite)
        {
          const std::uint16_t high = ((_bits >> sprite) & 1u) != 0u ? 0x100u : 0u;
          universe->video.x[sprite] = static_cast<std::uint16_t>((universe->video.x[sprite] & 0xFFu) | high);
        }
      };
      cells.push_back(std::move(shared));
    }

    cells.push_back(Direct(L"NOSTM", _at.nostm, _universe.dust.count, CellScope::Compared));
    cells.push_back(Direct(L"tek", _at.tek, _universe.techLevel, CellScope::Compared));

    // 6502: XX21+2*SST-2 -- the self-modified table entry, compared as state because `NWSPS` is
    // the only writer, so an unexpected change is a defect.
    Pair(cells, L"XX21+2*SST-2", L"XX21+2*SST-1", _at.xx21Station, _universe.bubble.stationBlueprint, CellScope::Compared);

    Run(cells, L"LSO", _at.lso, _universe.heaps.sun.data(), _universe.heaps.sun.size(), CellScope::Compared);
    for (std::size_t slot = 0; slot < _universe.bubble.blocks.size(); ++slot)
    {
      ShipCells(cells, L"K% slot " + std::to_wstring(slot), static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE),
                _universe.bubble.blocks[slot], CellScope::Compared);
    }
    for (std::size_t index = 0; index < _universe.dust.x.size(); ++index)
    {
      const std::uint16_t offset = static_cast<std::uint16_t>(index);
      cells.push_back(Direct(L"SX", static_cast<std::uint16_t>(_at.sx + offset), _universe.dust.x[index], CellScope::Compared));
      cells.push_back(Direct(L"SY", static_cast<std::uint16_t>(_at.sy + offset), _universe.dust.y[index], CellScope::Compared));
      cells.push_back(Direct(L"SZ", static_cast<std::uint16_t>(_at.sz + offset), _universe.dust.z[index], CellScope::Compared));
    }

    // 6502: RAND -- behind `State`/`SetState`, and compared unless the caller asks otherwise.
    for (std::size_t index = 0; index < 4u; ++index)
    {
      Cell cell;
      cell.name = L"RAND";
      cell.address = static_cast<std::uint16_t>(_at.rand + index);
      cell.scope = CellScope::Compared;
      cell.generator = true;
      cell.get = [universe, index]() { return universe->rng.State()[index]; };
      cell.set = [universe, index](std::uint8_t _byte)
      {
        std::array<std::uint8_t, 4> state = universe->rng.State();
        state[index] = _byte;
        universe->rng.SetState(state);
      };
      cells.push_back(std::move(cell));
    }

    // ---- image only: what `Mirror` wrote and `CompareState` did not check ---------------------------

    Run(cells, L"FRIN", _at.frin, _universe.bubble.slots.data(), _universe.bubble.slots.size(), CellScope::Image);
    Run(cells, L"MANY", _at.many, _universe.bubble.counts.data(), _universe.bubble.counts.size(), CellScope::Image);
    ShipCells(cells, L"INWK", _at.inwk, _universe.work, CellScope::Image);
    Run(cells, L"SXL", _at.sxl, _universe.dust.xLow.data(), _universe.dust.xLow.size(), CellScope::Image);
    Run(cells, L"SYL", _at.syl, _universe.dust.yLow.data(), _universe.dust.yLow.size(), CellScope::Image);
    Run(cells, L"SZL", _at.szl, _universe.dust.zLow.data(), _universe.dust.zLow.size(), CellScope::Image);
    Run(cells, L"LSX2", _at.lsx2, _universe.heaps.ball.data(), _universe.heaps.ball.size(), CellScope::Image);

    /*
     * The WHOLE commander block, because `LASER` and `TRIBBLE` are two fields of one structure and
     * the routines that read the others -- `OUCH` empties a hold slot, `EXNO2` adds to the tally,
     * the flight loop reads `ESCP`, `ECM` and `NOMSL` -- would otherwise be comparing the port's
     * zeroes against whatever the shipped block happens to hold. `QQ14` comes with it; `Seed`
     * keeps `Universe::fuel` equal to it.
     */
    Run(cells, L"TP", _at.tp, _universe.commander.bytes.data(), Elite::COMMANDER_BLOCK_SIZE, CellScope::Image);

    cells.push_back(Direct(L"COL2", _at.col2, _universe.text.cellColour, CellScope::Image));
    /*
     * The rest of the extended printer's state, which no screen routine reads but `MESS` does: it
     * turns the justifier into a measuring device (`DTW4` = %11000000, print, read `DTW5`) and a
     * stale byte in either would centre the message in the wrong column. `DTW7` is not in this
     * build -- the Master's literal-character byte has no C64 label.
     */
    cells.push_back(Direct(L"DTW3", _at.dtw3, _universe.characters.state.toLineBuffer, CellScope::Image));
    cells.push_back(Direct(L"DTW4", _at.dtw4, _universe.characters.state.justify, CellScope::Image));
    cells.push_back(Direct(L"DTW5", _at.dtw5, _universe.characters.state.bufferLength, CellScope::Image));
    cells.push_back(Direct(L"DTW8", _at.dtw8, _universe.characters.state.caseMask, CellScope::Image));
    cells.push_back(Direct(L"MCH", _at.mch, _universe.message.token, CellScope::Image));
    cells.push_back(Direct(L"messXC", _at.messxc, _universe.message.column, CellScope::Image));
    cells.push_back(Direct(L"QQ22+1", static_cast<std::uint16_t>(_at.qq22 + 1u), _universe.status.hyperspaceCountdown, CellScope::Image));
    cells.push_back(Direct(L"MJ", _at.mj, _universe.status.midJump, CellScope::Image));
    cells.push_back(Direct(L"JUNK", _at.junk, _universe.bubble.junk, CellScope::Image));
    cells.push_back(Direct(L"COMX", _at.comx, _universe.compass.x, CellScope::Image));
    cells.push_back(Direct(L"COMY", _at.comy, _universe.compass.y, CellScope::Image));
    cells.push_back(Direct(L"T2", _at.t2, _universe.draw.t2, CellScope::Image));
    cells.push_back(Direct(L"DELTA", _at.delta, _universe.flight.delta, CellScope::Image));
    cells.push_back(Direct(L"ALP1", _at.alp1, _universe.flight.alp1, CellScope::Image));
    cells.push_back(Direct(L"ALP2", _at.alp2, _universe.flight.alp2, CellScope::Image));
    cells.push_back(Direct(L"BETA", _at.beta, _universe.flight.beta, CellScope::Image));
    cells.push_back(Direct(L"BET1", _at.bet1, _universe.flight.bet1, CellScope::Image));
    cells.push_back(Direct(L"ENERGY", _at.energy, _universe.status.energy, CellScope::Image));
    cells.push_back(Direct(L"FSH", _at.fsh, _universe.status.forwardShield, CellScope::Image));
    cells.push_back(Direct(L"ASH", _at.ash, _universe.status.aftShield, CellScope::Image));
    cells.push_back(Direct(L"CABTMP", _at.cabtmp, _universe.status.cabinTemperature, CellScope::Image));
    cells.push_back(Direct(L"GNTMP", _at.gntmp, _universe.status.laserTemperature, CellScope::Image));
    cells.push_back(Direct(L"ALTIT", _at.altit, _universe.status.altitude, CellScope::Image));
    cells.push_back(Direct(L"FLH", _at.flh, _universe.status.damageFlash, CellScope::Image));
    cells.push_back(Direct(L"ECMA", _at.ecma, _universe.status.ecmCountdown, CellScope::Image));

    // ---- seeded: what the oracle needs and the port has no field for ---------------------------------

    /*
     * 6502: BEGIN's `LDA XX21+SST*2-2 / STA spasto`, which the fixture has to do itself.
     *
     * `spasto` is `EQUW &8888` in the source and `BEGIN` overwrites it at boot with the Coriolis's
     * table entry. `BEGIN` is startup code and `OracleImage::Fresh()` does not run it, so the
     * assembled image still holds the placeholder -- and `NWSPS` copies `spasto` INTO the table, so
     * a comparison against an image that has not booted spawns a station whose blueprint is &8888.
     * That is not a state the machine is ever in (§6.95's rule reaching a third byte). The port
     * needs no field: nothing writes `spasto` after `BEGIN`, and `BlueprintAddress(ShipType::Station)`
     * IS `spasto` for ever.
     */
    {
      const std::uint16_t coriolis = Elite::BlueprintAddress(Elite::ShipType::Station);
      Cell low;
      low.name = L"spasto";
      low.address = _at.spasto;
      low.scope = CellScope::Seeded;
      low.get = [coriolis]() { return static_cast<std::uint8_t>(coriolis & 0xFFu); };
      low.set = [](std::uint8_t) {};
      cells.push_back(std::move(low));

      Cell high;
      high.name = L"spasto+1";
      high.address = static_cast<std::uint16_t>(_at.spasto + 1u);
      high.scope = CellScope::Seeded;
      high.get = [coriolis]() { return static_cast<std::uint8_t>(coriolis >> 8); };
      high.set = [](std::uint8_t) {};
      cells.push_back(std::move(high));
    }

    return cells;
  }

  void Materialise(const Universe& _universe, Cpu6502& _cpu, const Where& _at)
  {
    // The getters are all that runs; see the note on `ImageCells`.
    for (const Cell& cell : ImageCells(const_cast<Universe&>(_universe), _at))
    {
      _cpu.memory[cell.address] = cell.get();
    }
  }

  void Absorb(const Cpu6502& _cpu, Universe& _universe, const Where& _at)
  {
    for (const Cell& cell : ImageCells(_universe, _at))
    {
      if (cell.scope != CellScope::Seeded)
      {
        cell.set(static_cast<std::uint8_t>(_cpu.memory[cell.address] & cell.mask));
      }
    }
  }

  std::vector<Difference> Compare(const Cpu6502& _cpu, const Universe& _universe, const Where& _at, bool _compareRng)
  {
    std::vector<Difference> differences;
    for (const Cell& cell : ImageCells(const_cast<Universe&>(_universe), _at))
    {
      if (cell.scope != CellScope::Compared || (cell.generator && !_compareRng))
      {
        continue;
      }
      const std::uint8_t theirs = static_cast<std::uint8_t>(_cpu.memory[cell.address] & cell.mask);
      const std::uint8_t ours = static_cast<std::uint8_t>(cell.get() & cell.mask);
      if (theirs != ours)
      {
        differences.push_back(Difference{cell.name, cell.address, theirs, ours});
      }
    }
    return differences;
  }

  std::uint64_t Hash(const Universe& _universe, const Where& _at)
  {
    std::uint64_t hash = FNV_OFFSET;
    for (const Cell& cell : ImageCells(const_cast<Universe&>(_universe), _at))
    {
      if (cell.scope == CellScope::Seeded)
      {
        continue;
      }
      hash ^= static_cast<std::uint8_t>(cell.get() & cell.mask);
      hash *= FNV_PRIME;
    }
    return hash;
  }

  std::uint64_t Hash(const Universe& _universe)
  {
    const Where unresolved{};
    return Hash(_universe, unresolved);
  }

  // ---- the two names the suites already use --------------------------------------------------------

  void Mirror(const Universe& _universe, Cpu6502& _cpu, const Where& _at)
  {
    Materialise(_universe, _cpu, _at);
  }

  void CompareState(const Cpu6502& _cpu, const Universe& _universe, const Where& _at, const std::wstring& _context, bool _compareRng)
  {
    const std::vector<Difference> differences = Compare(_cpu, _universe, _at, _compareRng);
    if (!differences.empty())
    {
      const Difference& first = differences.front();
      Assert::Fail((_context + L": " + first.name + L" -- game has " + std::to_wstring(first.theirs) + L", port has " +
                    std::to_wstring(first.ours) + L" (" + std::to_wstring(differences.size()) + L" cell(s) differ)")
                     .c_str());
    }
  }

} // namespace GameLogicTests
