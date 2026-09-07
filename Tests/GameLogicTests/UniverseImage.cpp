#include "pch.h"

#include "UniverseImage.h"

#include "Commander.h"
#include "ShipBlueprint.h"
#include "ShipSlot.h"
#include "VideoState.h"

#include <array>
#include <cstdint>
#include <type_traits>
#include <string>
#include <utility>
#include <functional>
#include <memory>
#include <tuple>
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

    /// A byte the port holds as a scoped enumeration with a fixed underlying type -- `PixelPattern`,
    /// `Colour` -- which is the same byte to the image and a different type to the compiler.
    template <typename Enum> Cell Enumerated(const wchar_t* _name, std::uint16_t _address, Enum& _field, CellScope _scope)
    {
      static_assert(std::is_enum_v<Enum> && sizeof(Enum) == 1, "one byte, one cell");
      Cell cell;
      cell.name = _name;
      cell.address = _address;
      cell.scope = _scope;
      Enum* at = &_field;
      cell.get = [at]() { return static_cast<std::uint8_t>(*at); };
      cell.set = [at](std::uint8_t _value) { *at = static_cast<Enum>(_value); };
      return cell;
    }

    /// A screen RAM palette the port holds as a `CellPalette` -- one byte to the image, two colours to the compiler.
    Cell Palette(const wchar_t* _name, std::uint16_t _address, Elite::CellPalette& _field, CellScope _scope)
    {
      Cell cell;
      cell.name = _name;
      cell.address = _address;
      cell.scope = _scope;
      Elite::CellPalette* at = &_field;
      cell.get = [at]() { return at->Byte(); };
      cell.set = [at](std::uint8_t _value) { *at = Elite::CellPalette::Of(_value); };
      return cell;
    }

    /// A run of directly held bytes, one cell each, named with their index.
    /// A sixteen-bit address the port holds as a POINTER -- `XX0`, the station's `XX21` entry --
    /// as two cells: reads give the pointee's address, and a write reassembles the address from
    /// both bytes before it is looked up, so a half-written pair changes nothing.
    void AddressPair(std::vector<Cell>& _cells, const wchar_t* _lowName, const wchar_t* _highName, std::uint16_t _address,
                     std::function<std::uint16_t()> _get, std::function<void(std::uint16_t)> _set, CellScope _scope)
    {
      const auto pending = std::make_shared<std::uint16_t>(_get());
      for (std::size_t half = 0; half < 2u; ++half)
      {
        Cell cell;
        cell.name = (half == 0u) ? _lowName : _highName;
        cell.address = static_cast<std::uint16_t>(_address + half);
        cell.scope = _scope;
        cell.get = [_get, half]() { return static_cast<std::uint8_t>(_get() >> (8u * half)); };
        cell.set = [_set, pending, half](std::uint8_t _value)
        {
          const std::uint16_t mask = static_cast<std::uint16_t>(0xFFu << (8u * half));
          *pending = static_cast<std::uint16_t>((*pending & ~mask) | (static_cast<std::uint16_t>(_value) << (8u * half)));
          if (half == 1u)
          {
            _set(*pending);
          }
        };
        _cells.push_back(std::move(cell));
      }
    }

    /// A struct with a codec -- a `Ship`'s thirty-seven bytes, a `Commander`'s seventy-seven --
    /// as cells: each reads `ToBytes()` and writes back through `FromBytes`, so the image is the
    /// original's layout whatever the struct's own layout is.
    template <class Coded>
    void CodecCells(std::vector<Cell>& _cells, const std::wstring& _name, std::uint16_t _base, Coded& _object, CellScope _scope)
    {
      constexpr std::size_t SIZE = std::tuple_size_v<decltype(_object.ToBytes())>;
      for (std::size_t byte = 0; byte < SIZE; ++byte)
      {
        Cell cell;
        cell.name = _name + L" byte " + std::to_wstring(byte);
        cell.address = static_cast<std::uint16_t>(_base + byte);
        cell.scope = _scope;
        Coded* object = &_object;
        cell.get = [object, byte]() { return object->ToBytes()[byte]; };
        cell.set = [object, byte](std::uint8_t _value)
        {
          auto bytes = object->ToBytes();
          bytes[byte] = _value;
          *object = Coded::FromBytes(bytes);
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

  } // namespace

  std::vector<Cell> ImageCells(Universe& _universe, const Where& _at)
  {
    return ImageCells(static_cast<Elite::Universe&>(_universe), _universe.spriteRegistersAreOurs, _at);
  }

  std::vector<Cell> ImageCells(Elite::Universe& _universe, bool _spriteRegistersAreOurs, const Where& _at)
  {
    std::vector<Cell> cells;
    cells.reserve(1200);
    Elite::Universe* universe = &_universe;

    // ---- compared, in the order `CompareState` checked them -----------------------------------------

    cells.push_back(Direct(L"XC", _at.xc, _universe.text.column, CellScope::Compared));
    cells.push_back(Direct(L"YC", _at.yc, _universe.text.row, CellScope::Compared));
    cells.push_back(Direct(L"QQ17", _at.qq17, _universe.text.caseFlags, CellScope::Compared));
    cells.push_back(Direct(L"DTW1", _at.dtw1, _universe.sentences.lowerCaseBits, CellScope::Compared));
    cells.push_back(Direct(L"DTW2", _at.dtw2, _universe.sentences.sentenceStart, CellScope::Compared));
    cells.push_back(Direct(L"DTW6", _at.dtw6, _universe.sentences.alwaysLower, CellScope::Compared));
    cells.push_back(Direct(L"LSP", _at.lsp, _universe.heaps.lsp, CellScope::Compared));

    /*
     * 6502: SUNX, Yx2M1, K5, K6, STP, FLAG and PLTOG -- the rest of the planet and sun state, cells
     * since M6-0-d. `Where` had no `SUNX` and no `LSY2` from M3-b on, so a fixture could not put a
     * DRAWN sun into both machines; these are what `SUN`, `CIRCLE` and `BLINE` leave behind. `V` is
     * an IMAGE cell only: the original uses the pair as a pointer in `LL9`, `TACTICS` and the
     * printers and as the sun's counter, and the port keeps the two apart (`PlanetSunState::v`),
     * so after a frame the game's byte is whichever user ran last and a comparison would be noise.
     * `K5` and `K6` are the same shape and were found so by making them `Compared` first: the ball
     * walk's segment ends here, and `LL9`'s line clipper and the escape pod's launch write the same
     * eight bytes as scratch the port keeps in its stage results (M2-c), so eight suites differed
     * on them after a frame that drew a ship and no planet.
     */
    cells.push_back(Direct(L"SUNX", _at.sunx, _universe.heaps.sunX, CellScope::Compared));
    cells.push_back(Direct(L"SUNX+1", static_cast<std::uint16_t>(_at.sunx + 1u), _universe.heaps.sunXNext, CellScope::Compared));
    cells.push_back(Direct(L"Yx2M1", _at.yx2m1, _universe.heaps.yx2M1, CellScope::Compared));
    Run(cells, L"K5", _at.k5, _universe.heaps.k5.data(), _universe.heaps.k5.size(), CellScope::Image);
    Run(cells, L"K6", _at.k6, _universe.heaps.k6.data(), _universe.heaps.k6.size(), CellScope::Image);
    cells.push_back(Direct(L"STP", _at.stp, _universe.heaps.stp, CellScope::Compared));
    cells.push_back(Direct(L"FLAG", _at.flag, _universe.heaps.flag, CellScope::Compared));
    cells.push_back(Direct(L"PLTOG", _at.pltog, _universe.heaps.pltog, CellScope::Compared));
    cells.push_back(Direct(L"V", _at.v, _universe.heaps.v, CellScope::Image));
    cells.push_back(Direct(L"V+1", static_cast<std::uint16_t>(_at.v + 1u), _universe.heaps.vNext, CellScope::Image));
    cells.push_back(Direct(L"DLY", _at.dly, _universe.message.delay, CellScope::Compared));
    cells.push_back(Direct(L"de", _at.de, _universe.message.append, CellScope::Compared));
    cells.push_back(Direct(L"LAS2", _at.las2, _universe.status.viewLaser, CellScope::Compared));
    cells.push_back(Direct(L"VIEW", _at.viewByte, _universe.spaceView, CellScope::Compared));
    cells.push_back(Direct(L"QQ11", _at.qq11, _universe.view, CellScope::Compared));
    cells.push_back(Direct(L"EV", _at.ev, _universe.explosions, CellScope::Compared));
    cells.push_back(Direct(L"MCNT", _at.mcnt, _universe.flight.mainLoopCounter, CellScope::Compared));
    AddressPair(
      cells, L"XX0", L"XX0+1", _at.xx0, [&_universe]() { return _universe.flight.blueprint->address; },
      [&_universe](std::uint16_t _address)
      {
        if (const Elite::Blueprint* found = Elite::BlueprintAt(_address))
        {
          _universe.flight.blueprint = found;
        }
      },
      CellScope::Compared);
    cells.push_back(Direct(L"abraxas", _at.abraxas, _universe.screen.colourBank, CellScope::Compared));
    cells.push_back(Direct(L"caravanserai", _at.caravanserai, _universe.screen.bitmapMode, CellScope::Compared));
    cells.push_back(Direct(L"DFLAG", _at.dflag, _universe.screen.dashboardShown, CellScope::Compared));
    cells.push_back(Enumerated(L"COMC", _at.comc, _universe.compass.pattern, CellScope::Compared));
    cells.push_back(Direct(L"TRIBCT", _at.tribct, _universe.trumbles.count, CellScope::Compared));
    for (std::size_t index = 0; index < _universe.trumbles.velocityX.size(); ++index)
    {
      const std::uint16_t offset = static_cast<std::uint16_t>(index);
      cells.push_back(
        Direct(L"TRIBVX", static_cast<std::uint16_t>(_at.tribvx + offset), _universe.trumbles.velocityX[index], CellScope::Compared));
      cells.push_back(
        Direct(L"TRIBVXH", static_cast<std::uint16_t>(_at.tribvxh + offset), _universe.trumbles.velocityXHigh[index], CellScope::Compared));
      cells.push_back(
        Direct(L"TRIBXH", static_cast<std::uint16_t>(_at.tribxh + offset), _universe.trumbles.coordinateXHigh[index], CellScope::Compared));
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
    if (_spriteRegistersAreOurs)
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
    cells.push_back(Direct(L"tek", _at.tek, _universe.current.techLevel, CellScope::Compared));

    // 6502: XX21+2*SST-2 -- the self-modified table entry, compared as state because `NWSPS` is
    // the only writer, so an unexpected change is a defect.
    AddressPair(
      cells, L"XX21+2*SST-2", L"XX21+2*SST-1", _at.xx21Station,
      [&_universe]() { return Elite::BlueprintOf(_universe.bubble.stationType)->address; },
      [&_universe](std::uint16_t _address)
      {
        if (const Elite::Blueprint* found = Elite::BlueprintAt(_address); found != nullptr && found->address != 0u)
        {
          _universe.bubble.stationType = found->type;
        }
      },
      CellScope::Compared);

    Run(cells, L"LSO", _at.lso, _universe.heaps.sun.data(), _universe.heaps.sun.size(), CellScope::Compared);

    /*
     * 6502: sound_variables -- what `NOISE`, `NOISE2` and `NOISEOFF` write (M3-b-2a).
     *
     * They were seams and the fixtures counted the calls; they are routines over `Universe::sound`
     * now, so the buffer is mirrored in and compared out. `PULSEW` is one byte rather than three
     * and `DNOIZ` is the pause screen's toggle rather than sound state, which is why both sit
     * beside the ten runs instead of inside them.
     */
    Run(cells, L"SOFLG", _at.soflg, _universe.sound.flag.data(), _universe.sound.flag.size(), CellScope::Compared);
    Run(cells, L"SOCNT", _at.socnt, _universe.sound.counter.data(), _universe.sound.counter.size(), CellScope::Compared);
    Run(cells, L"SOPR", _at.sopr, _universe.sound.priority.data(), _universe.sound.priority.size(), CellScope::Compared);
    Run(cells, L"SOFRCH", _at.sofrch, _universe.sound.frequencyChange.data(), _universe.sound.frequencyChange.size(), CellScope::Compared);
    Run(cells, L"SOFRQ", _at.sofrq, _universe.sound.frequency.data(), _universe.sound.frequency.size(), CellScope::Compared);
    Run(cells, L"SOCR", _at.socr, _universe.sound.control.data(), _universe.sound.control.size(), CellScope::Compared);
    Run(cells, L"SOATK", _at.soatk, _universe.sound.attack.data(), _universe.sound.attack.size(), CellScope::Compared);
    Run(cells, L"SOSUS", _at.sosus, _universe.sound.sustain.data(), _universe.sound.sustain.size(), CellScope::Compared);
    Run(cells, L"SOVCH", _at.sovch, _universe.sound.volumeRate.data(), _universe.sound.volumeRate.size(), CellScope::Compared);
    cells.push_back(Direct(L"PULSEW", _at.pulsew, _universe.sound.pulseWidth, CellScope::Compared));
    cells.push_back(Direct(L"DNOIZ", _at.dnoiz, _universe.sound.soundOff, CellScope::Compared));

    /*
     * 6502: safehouse, QQ8, JSTGY, JSTE and MUTOKOLD -- the widening ADR-007 §5 named, taken.
     *
     * M3's follow-on moved seven bytes out of `Elite::Game` and into `Universe` because every one
     * has a 6502 name, and §4.4's rule puts a named byte in the universe. Moving them did not put
     * them in the DIGEST: `Hash` walks this table, so a field is unhashed until a cell names it,
     * and a slice that broke `MUTOKOLD` or `QQ8` would not have moved the record. That is rule 1's
     * FIRST case -- a deliberately widened digest -- and taking it moves every checkpoint.
     *
     * FIVE OF THE SEVEN, and the arithmetic is the finding. One was a duplicate (`soundDisabled`,
     * a second `DNOIZ`, removed by M5-a-5 -- the cell above was already watching the real one). The
     * other is `crosshairStep`, and it CANNOT have a cell: it is what `TT17` leaves in X and Y,
     * which are REGISTERS. The original keeps them nowhere, so there is no address to compare
     * against; the port has to put them somewhere and that somewhere is not memory the game has.
     * ADR-007 §5 listed all seven as one kind of gap and they are three kinds.
     */
    Run(cells, L"safehouse", _at.safehouse, _universe.jumpTarget.bytes.data(), _universe.jumpTarget.bytes.size(), CellScope::Compared);
    cells.push_back(Direct(L"JSTGY", _at.jstgy, _universe.joystickGeometry, CellScope::Compared));
    cells.push_back(Direct(L"JSTE", _at.jste, _universe.joystickEnabled, CellScope::Compared));
    cells.push_back(Direct(L"MUTOKOLD", _at.mutokold, _universe.musicSwitchWas, CellScope::Compared));

    // 6502: QQ8 -- two bytes, and the port keeps it as one `std::uint16_t`, so it goes through the
    // same pair helper `XX0` uses.
    AddressPair(
      cells, L"QQ8", L"QQ8+1", _at.qq8, [&_universe]() { return _universe.jumpDistance; },
      [&_universe](std::uint16_t _value) { _universe.jumpDistance = _value; }, CellScope::Compared);

    /*
     * 6502: MUPLA and MULIE -- what `startbd`, `stopbd`, `startat` and `stopat` leave behind
     * (M3-b-2b).
     *
     * The same argument one paragraph up: they were seams on `FlightLoopEffects` and
     * `StartUpEffects` and the fixtures counted the calls; both machines run the player now, so
     * whether a tune is playing is a byte to be mirrored in and compared out. `MULIE` is the title
     * screen's bracket around its `RESET`, which `stopbd` READS -- so a fixture that changed it
     * without the port changing it too would be comparing two different decisions.
     */
    cells.push_back(Direct(L"MUPLA", _at.mupla, _universe.music.playing, CellScope::Compared));
    cells.push_back(Direct(L"MULIE", _at.mulie, _universe.status.titleReset, CellScope::Compared));

    /*
     * 6502: L1M and l1 -- the memory map, MIRRORED IN AND NOT COMPARED OUT (M3-b-3a).
     *
     * They were `SightEffects::SetRasterMode`, a write-only seam the fixtures counted, and both
     * machines run `SETL1` now -- so mirroring them matters: `SETL1` PRESERVES the top five bits,
     * and a port given a zero where the game has the datasette's would agree by accident.
     *
     * THEY CANNOT BE COMPARED HERE, and the reason is §6.108's and not this slice's. `NOSPRITES`
     * is trapped on the oracle in every fixture that changes a screen, because in flat memory its
     * `STA VIC+&15` lands on the blueprint pointer table; so the game does not reach the `SETL1`
     * inside it and the port does. Four suites DO run the routine on both sides and compare the
     * byte where it lands -- `ControlsTests` (`SIGHT`), `TrumbleTests` (`MVTRIBS`),
     * `ExplosionTests` (`PTCLS2`) and `SoundTests` (`startbd`, `stopbd`) -- and they are where the
     * bracket is pinned until the harness models the banking.
     */
    cells.push_back(Direct(L"L1M", _at.l1m, _universe.memoryMap.requested, CellScope::Seeded));
    cells.push_back(Direct(L"l1", Where::IO_PORT, _universe.memoryMap.port, CellScope::Seeded));
    for (std::size_t slot = 0; slot < _universe.bubble.blocks.size(); ++slot)
    {
      CodecCells(cells, L"K% slot " + std::to_wstring(slot), static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE),
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
    CodecCells(cells, L"INWK", _at.inwk, _universe.work, CellScope::Image);
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
    CodecCells(cells, L"TP", _at.tp, _universe.commander, CellScope::Image);

    cells.push_back(Palette(L"COL2", _at.col2, _universe.text.palette, CellScope::Image));
    /*
     * The rest of the extended printer's state, which no screen routine reads but `MESS` does: it
     * turns the justifier into a measuring device (`DTW4` = %11000000, print, read `DTW5`) and a
     * stale byte in either would centre the message in the wrong column. `DTW7` is not in this
     * build -- the Master's literal-character byte has no C64 label.
     */
    cells.push_back(Direct(L"DTW3", _at.dtw3, _universe.sentences.toLineBuffer, CellScope::Image));
    cells.push_back(Direct(L"DTW4", _at.dtw4, _universe.sentences.justify, CellScope::Image));
    cells.push_back(Direct(L"DTW5", _at.dtw5, _universe.sentences.bufferLength, CellScope::Image));
    cells.push_back(Direct(L"DTW8", _at.dtw8, _universe.sentences.caseMask, CellScope::Image));
    cells.push_back(Direct(L"MCH", _at.mch, _universe.message.token, CellScope::Image));
    cells.push_back(Direct(L"messXC", _at.messxc, _universe.message.column, CellScope::Image));
    cells.push_back(Direct(L"QQ22+1", static_cast<std::uint16_t>(_at.qq22 + 1u), _universe.status.hyperspaceCountdown, CellScope::Image));
    cells.push_back(Direct(L"MJ", _at.mj, _universe.status.midJump, CellScope::Image));
    cells.push_back(Direct(L"JUNK", _at.junk, _universe.bubble.junk, CellScope::Image));
    cells.push_back(Direct(L"COMX", _at.comx, _universe.compass.x, CellScope::Image));
    cells.push_back(Direct(L"COMY", _at.comy, _universe.compass.y, CellScope::Image));
    /*
     * `T2` WAS AN IMAGE CELL AND HASHED A BYTE THE GAME DOES NOT WRITE (§8, M2-c).
     *
     * The port's `HLOIN` and `BOX2` parked their scratch in `T2` and `R2`, which is the BBC
     * commentary's naming; the C64's use `T` and `R`. Nothing read either byte on either side, so
     * the screens always agreed -- but the digest hashed the port's invention, so it moves with the
     * fix, under Modernize.md rule 1's second case.
     */
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
     * needs no field: nothing writes `spasto` after `BEGIN`, and `BlueprintOf(ShipType::Station)->address`
     * IS `spasto` for ever.
     */
    {
      const std::uint16_t coriolis = Elite::BlueprintOf(Elite::ShipType::Station)->address;
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

  namespace
  {
    std::uint64_t HashCells(const std::vector<Cell>& _cells)
    {
      std::uint64_t hash = FNV_OFFSET;
      for (const Cell& cell : _cells)
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
  } // namespace

  std::uint64_t Hash(const Universe& _universe, const Where& _at)
  {
    return HashCells(ImageCells(const_cast<Universe&>(_universe), _at));
  }

  std::uint64_t Hash(const Elite::Universe& _universe)
  {
    const Where unresolved{};
    return HashCells(ImageCells(const_cast<Elite::Universe&>(_universe), false, unresolved));
  }

  // ---- the two names the suites already use --------------------------------------------------------

  void Mirror(const Universe& _universe, Cpu6502& _cpu, const Where& _at)
  {
    Materialise(_universe, _cpu, _at);
  }

  void CompareSound(const Cpu6502& _cpu, const Universe& _universe, const Where& _at, const std::wstring& _context)
  {
    struct Run
    {
      const wchar_t* name;
      std::uint16_t base;
      const std::uint8_t* bytes;
      std::size_t count;
    };
    const Elite::SoundBuffer& sound = _universe.sound;
    const Run RUNS[] = {
      {L"SOFLG", _at.soflg, sound.flag.data(), sound.flag.size()},
      {L"SOCNT", _at.socnt, sound.counter.data(), sound.counter.size()},
      {L"SOPR", _at.sopr, sound.priority.data(), sound.priority.size()},
      {L"SOFRCH", _at.sofrch, sound.frequencyChange.data(), sound.frequencyChange.size()},
      {L"SOFRQ", _at.sofrq, sound.frequency.data(), sound.frequency.size()},
      {L"SOCR", _at.socr, sound.control.data(), sound.control.size()},
      {L"SOATK", _at.soatk, sound.attack.data(), sound.attack.size()},
      {L"SOSUS", _at.sosus, sound.sustain.data(), sound.sustain.size()},
      {L"SOVCH", _at.sovch, sound.volumeRate.data(), sound.volumeRate.size()},
      {L"PULSEW", _at.pulsew, &sound.pulseWidth, 1u},
      {L"DNOIZ", _at.dnoiz, &sound.soundOff, 1u},

      // 6502: MUPLA and MULIE -- the music the game side starts and stops (M3-b-2b). `ImageCells`
      // carries them too, so a suite that calls `CompareState` has them already; this is for the
      // ones that ask about the sound alone.
      {L"MUPLA", _at.mupla, &_universe.music.playing, 1u},
      {L"MULIE", _at.mulie, &_universe.status.titleReset, 1u},
    };

    for (const Run& run : RUNS)
    {
      for (std::size_t index = 0; index < run.count; ++index)
      {
        const std::uint8_t theirs = _cpu.memory[static_cast<std::uint16_t>(run.base + index)];
        const std::uint8_t ours = run.bytes[index];
        if (theirs != ours)
        {
          Assert::Fail((_context + L": " + run.name + L"+" + std::to_wstring(index) + L" -- game has " + std::to_wstring(theirs) +
                        L", port has " + std::to_wstring(ours))
                         .c_str());
        }
      }
    }
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
