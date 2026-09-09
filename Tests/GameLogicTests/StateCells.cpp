#include "pch.h"

#include "StateCells.h"

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
    Cell Direct(const wchar_t* _name, std::uint8_t& _byte, CellScope _scope)
    {
      Cell cell;
      cell.name = _name;
      cell.scope = _scope;
      std::uint8_t* at = &_byte;
      cell.get = [at]() { return *at; };
      cell.set = [at](std::uint8_t _value) { *at = _value; };
      return cell;
    }

    /// A byte the port holds as a scoped enumeration with a fixed underlying type -- `PixelPattern`,
    /// `Colour` -- which is the same byte to the image and a different type to the compiler.
    template <typename Enum> Cell Enumerated(const wchar_t* _name, Enum& _field, CellScope _scope)
    {
      static_assert(std::is_enum_v<Enum> && sizeof(Enum) == 1, "one byte, one cell");
      Cell cell;
      cell.name = _name;
      cell.scope = _scope;
      Enum* at = &_field;
      cell.get = [at]() { return static_cast<std::uint8_t>(*at); };
      cell.set = [at](std::uint8_t _value) { *at = static_cast<Enum>(_value); };
      return cell;
    }

    /// A screen RAM palette the port holds as a `CellPalette` -- one byte to the image, two colours to the compiler.
    Cell Palette(const wchar_t* _name, Elite::CellPalette& _field, CellScope _scope)
    {
      Cell cell;
      cell.name = _name;
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
    void AddressPair(std::vector<Cell>& _cells, const wchar_t* _lowName, const wchar_t* _highName, std::function<std::uint16_t()> _get, std::function<void(std::uint16_t)> _set, CellScope _scope)
    {
      const auto pending = std::make_shared<std::uint16_t>(_get());
      for (std::size_t half = 0; half < 2u; ++half)
      {
        Cell cell;
        cell.name = (half == 0u) ? _lowName : _highName;
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
    void CodecCells(std::vector<Cell>& _cells, const std::wstring& _name, Coded& _object, CellScope _scope)
    {
      constexpr std::size_t SIZE = std::tuple_size_v<decltype(_object.ToBytes())>;
      for (std::size_t byte = 0; byte < SIZE; ++byte)
      {
        Cell cell;
        cell.name = _name + L" byte " + std::to_wstring(byte);
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

    /// One directly held byte under a name built at run time, which an indexed field needs: the
    /// address used to tell two `TRIBVX` cells apart and there is no address any more.
    void Named(std::vector<Cell>& _cells, const std::wstring& _name, std::uint8_t& _byte)
    {
      Cell cell = Direct(L"", _byte, CellScope::Compared);
      cell.name = _name;
      _cells.push_back(std::move(cell));
    }

    void Run(std::vector<Cell>& _cells, const wchar_t* _name, std::uint8_t* _bytes, std::size_t _count, CellScope _scope)
    {
      for (std::size_t index = 0; index < _count; ++index)
      {
        Cell cell = Direct(_name, _bytes[index], _scope);
        cell.name += L"[" + std::to_wstring(index) + L"]";
        _cells.push_back(std::move(cell));
      }
    }

  } // namespace

  /*
   * `Universe::picture` HAS NO CELLS HERE, and that is the whole of its treatment (Resolution.md
   * section 3.4).
   *
   * Every cell below mirrors a 6502 address, and the 640x400 picture has none: the original had no
   * such surface, so there is nothing in the interpreter's memory to write it to or read it back
   * from. It is not an omission to be closed later -- a fixture that recorded it would be recording
   * this port's own rendering against nothing (Modernize.md section 4.10), and after M6-b there
   * would be no original left to ask.
   *
   * `Elite::HashState` walks past it too, deliberately, and `StateHash.cpp` carries the reason;
   * `ThePicture::TheStateHashDeliberatelyDoesNotSeeIt` is what fails if somebody folds it in.
   */
  std::vector<Cell> StateCells(Elite::Universe& _universe)
  {
    std::vector<Cell> cells;
    cells.reserve(1200);
    Elite::Universe* universe = &_universe;

    // ---- compared, in the order `CompareState` checked them -----------------------------------------

    cells.push_back(Direct(L"XC", _universe.text.column, CellScope::Compared));
    cells.push_back(Direct(L"YC", _universe.text.row, CellScope::Compared));
    cells.push_back(Direct(L"QQ17", _universe.text.caseFlags, CellScope::Compared));
    cells.push_back(Direct(L"DTW1", _universe.sentences.lowerCaseBits, CellScope::Compared));
    cells.push_back(Direct(L"DTW2", _universe.sentences.sentenceStart, CellScope::Compared));
    cells.push_back(Direct(L"DTW6", _universe.sentences.alwaysLower, CellScope::Compared));
    cells.push_back(Direct(L"LSP", _universe.heaps.ballHeapTop, CellScope::Compared));

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
    cells.push_back(Direct(L"SUNX", _universe.heaps.sunX, CellScope::Compared));
    cells.push_back(Direct(L"SUNX+1", _universe.heaps.sunXNext, CellScope::Compared));
    cells.push_back(Direct(L"Yx2M1", _universe.heaps.lowestVisibleRow, CellScope::Compared));
    Run(cells, L"K5", _universe.heaps.segmentStart.data(), _universe.heaps.segmentStart.size(), CellScope::Image);
    Run(cells, L"K6", _universe.heaps.segmentEnd.data(), _universe.heaps.segmentEnd.size(), CellScope::Image);
    cells.push_back(Direct(L"STP", _universe.heaps.circleStep, CellScope::Compared));
    cells.push_back(Direct(L"FLAG", _universe.heaps.flag, CellScope::Compared));
    cells.push_back(Direct(L"PLTOG", _universe.heaps.planetDetail, CellScope::Compared));
    cells.push_back(Direct(L"V", _universe.heaps.v, CellScope::Image));
    cells.push_back(Direct(L"V+1", _universe.heaps.vNext, CellScope::Image));
    cells.push_back(Direct(L"DLY", _universe.message.delay, CellScope::Compared));
    cells.push_back(Direct(L"de", _universe.message.append, CellScope::Compared));
    cells.push_back(Direct(L"LAS2", _universe.status.viewLaser, CellScope::Compared));
    cells.push_back(Direct(L"VIEW", _universe.spaceView, CellScope::Compared));
    cells.push_back(Direct(L"QQ11", _universe.view, CellScope::Compared));
    cells.push_back(Direct(L"EV", _universe.explosions, CellScope::Compared));
    cells.push_back(Direct(L"MCNT", _universe.flight.mainLoopCounter, CellScope::Compared));
    AddressPair(cells, L"XX0", L"XX0+1", [&_universe]() { return _universe.flight.blueprint->address; },
      [&_universe](std::uint16_t _address)
      {
        if (const Elite::Blueprint* found = Elite::BlueprintAt(_address))
        {
          _universe.flight.blueprint = found;
        }
      },
      CellScope::Compared);
    cells.push_back(Direct(L"abraxas", _universe.screen.colourBank, CellScope::Compared));
    cells.push_back(Direct(L"caravanserai", _universe.screen.bitmapMode, CellScope::Compared));
    cells.push_back(Direct(L"DFLAG", _universe.screen.dashboardShown, CellScope::Compared));
    cells.push_back(Enumerated(L"COMC", _universe.compass.pattern, CellScope::Compared));
    cells.push_back(Direct(L"TRIBCT", _universe.trumbles.count, CellScope::Compared));
    for (std::size_t index = 0; index < _universe.trumbles.velocityX.size(); ++index)
    {
      const std::wstring which = L"[" + std::to_wstring(index) + L"]";
      Named(cells, L"TRIBVX" + which, _universe.trumbles.velocityX[index]);
      Named(cells, L"TRIBVXH" + which, _universe.trumbles.velocityXHigh[index]);
      Named(cells, L"TRIBXH" + which, _universe.trumbles.coordinateXHigh[index]);
    }

    /*
     * The VIC-II sprite coordinates -- registers on the I/O page, which the interpreter banks
     * since M6-0-a. Until then they were `XX21` in a flat image (§6.108): the same addresses held
     * the blueprint pointers for ship types 3 to 9 on one side and sprite registers on the other,
     * so a fixture had to CLAIM them (`spriteRegistersAreOurs`) and promise to draw no ship after.
     * The nine-bit x is one value on the port's side and a low byte per sprite plus one shared
     * high-bit byte on the game's.
     */
    {
      for (std::size_t sprite = Elite::FIRST_TRUMBLE_SPRITE; sprite < Elite::SPRITE_COUNT; ++sprite)
      {
        Cell low;
        low.name = L"sprite " + std::to_wstring(sprite) + L" x";
        low.scope = CellScope::Compared;
        low.get = [universe, sprite]() { return static_cast<std::uint8_t>(universe->video.x[sprite] & 0xFFu); };
        low.set = [universe, sprite](std::uint8_t _byte)
        { universe->video.x[sprite] = static_cast<std::uint16_t>((universe->video.x[sprite] & 0x100u) | _byte); };
        cells.push_back(std::move(low));

        Cell y = Direct(L"sprite y", _universe.video.y[sprite], CellScope::Compared);
        cells.push_back(std::move(y));
      }

      Cell shared;
      shared.name = L"sprite x high bits";
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

    cells.push_back(Direct(L"NOSTM", _universe.dust.count, CellScope::Compared));
    cells.push_back(Direct(L"tek", _universe.current.techLevel, CellScope::Compared));

    // 6502: XX21+2*SST-2 -- the self-modified table entry, compared as state because `NWSPS` is
    // the only writer, so an unexpected change is a defect.
    AddressPair(cells, L"XX21+2*SST-2", L"XX21+2*SST-1", [&_universe]() { return Elite::BlueprintOf(_universe.bubble.stationType)->address; },
      [&_universe](std::uint16_t _address)
      {
        if (const Elite::Blueprint* found = Elite::BlueprintAt(_address); found != nullptr && found->address != 0u)
        {
          _universe.bubble.stationType = found->type;
        }
      },
      CellScope::Compared);

    Run(cells, L"LSO", _universe.heaps.sun.data(), _universe.heaps.sun.size(), CellScope::Compared);

    /*
     * 6502: sound_variables -- what `NOISE`, `NOISE2` and `NOISEOFF` write (M3-b-2a).
     *
     * They were seams and the fixtures counted the calls; they are routines over `Universe::sound`
     * now, so the buffer is mirrored in and compared out. `PULSEW` is one byte rather than three
     * and `DNOIZ` is the pause screen's toggle rather than sound state, which is why both sit
     * beside the ten runs instead of inside them.
     */
    Run(cells, L"SOFLG", _universe.sound.flag.data(), _universe.sound.flag.size(), CellScope::Compared);
    Run(cells, L"SOCNT", _universe.sound.counter.data(), _universe.sound.counter.size(), CellScope::Compared);
    Run(cells, L"SOPR", _universe.sound.priority.data(), _universe.sound.priority.size(), CellScope::Compared);
    Run(cells, L"SOFRCH", _universe.sound.frequencyChange.data(), _universe.sound.frequencyChange.size(), CellScope::Compared);
    Run(cells, L"SOFRQ", _universe.sound.frequency.data(), _universe.sound.frequency.size(), CellScope::Compared);
    Run(cells, L"SOCR", _universe.sound.control.data(), _universe.sound.control.size(), CellScope::Compared);
    Run(cells, L"SOATK", _universe.sound.attack.data(), _universe.sound.attack.size(), CellScope::Compared);
    Run(cells, L"SOSUS", _universe.sound.sustain.data(), _universe.sound.sustain.size(), CellScope::Compared);
    Run(cells, L"SOVCH", _universe.sound.volumeRate.data(), _universe.sound.volumeRate.size(), CellScope::Compared);
    cells.push_back(Direct(L"PULSEW", _universe.sound.pulseWidth, CellScope::Compared));
    cells.push_back(Direct(L"DNOIZ", _universe.sound.soundOff, CellScope::Compared));

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
    Run(cells, L"safehouse", _universe.jumpTarget.bytes.data(), _universe.jumpTarget.bytes.size(), CellScope::Compared);
    cells.push_back(Direct(L"JSTGY", _universe.joystickGeometry, CellScope::Compared));
    cells.push_back(Direct(L"JSTE", _universe.joystickEnabled, CellScope::Compared));
    cells.push_back(Direct(L"MUTOKOLD", _universe.musicSwitchWas, CellScope::Compared));

    // 6502: QQ8 -- two bytes, and the port keeps it as one `std::uint16_t`, so it goes through the
    // same pair helper `XX0` uses.
    AddressPair(cells, L"QQ8", L"QQ8+1", [&_universe]() { return _universe.jumpDistance; }, [&_universe](std::uint16_t _value) { _universe.jumpDistance = _value; }, CellScope::Compared);

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
    cells.push_back(Direct(L"MUPLA", _universe.music.playing, CellScope::Compared));
    cells.push_back(Direct(L"MULIE", _universe.status.titleReset, CellScope::Compared));

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
    cells.push_back(Direct(L"L1M", _universe.memoryMap.requested, CellScope::Seeded));
    cells.push_back(Direct(L"l1", _universe.memoryMap.port, CellScope::Seeded));
    for (std::size_t slot = 0; slot < _universe.bubble.blocks.size(); ++slot)
    {
      CodecCells(cells, L"K% slot " + std::to_wstring(slot), _universe.bubble.blocks[slot], CellScope::Compared);
    }
    for (std::size_t index = 0; index < _universe.dust.x.size(); ++index)
    {
      const std::wstring which = L"[" + std::to_wstring(index) + L"]";
      Named(cells, L"SX" + which, _universe.dust.x[index]);
      Named(cells, L"SY" + which, _universe.dust.y[index]);
      Named(cells, L"SZ" + which, _universe.dust.z[index]);
    }

    // 6502: RAND -- behind `State`/`SetState`, and compared unless the caller asks otherwise.
    for (std::size_t index = 0; index < 4u; ++index)
    {
      Cell cell;
      cell.name = L"RAND";
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

    Run(cells, L"FRIN", _universe.bubble.slots.data(), _universe.bubble.slots.size(), CellScope::Image);
    Run(cells, L"MANY", _universe.bubble.counts.data(), _universe.bubble.counts.size(), CellScope::Image);
    CodecCells(cells, L"INWK", _universe.work, CellScope::Image);
    Run(cells, L"SXL", _universe.dust.xLow.data(), _universe.dust.xLow.size(), CellScope::Image);
    Run(cells, L"SYL", _universe.dust.yLow.data(), _universe.dust.yLow.size(), CellScope::Image);
    Run(cells, L"SZL", _universe.dust.zLow.data(), _universe.dust.zLow.size(), CellScope::Image);
    Run(cells, L"LSX2", _universe.heaps.ball.data(), _universe.heaps.ball.size(), CellScope::Image);

    /*
     * The WHOLE commander block, because `LASER` and `TRIBBLE` are two fields of one structure and
     * the routines that read the others -- `OUCH` empties a hold slot, `EXNO2` adds to the tally,
     * the flight loop reads `ESCP`, `ECM` and `NOMSL` -- would otherwise be comparing the port's
     * zeroes against whatever the shipped block happens to hold. `QQ14` comes with it; `Seed`
     * keeps `Universe::fuel` equal to it.
     */
    CodecCells(cells, L"TP", _universe.commander, CellScope::Image);

    cells.push_back(Palette(L"COL2", _universe.text.palette, CellScope::Image));
    /*
     * The rest of the extended printer's state, which no screen routine reads but `MESS` does: it
     * turns the justifier into a measuring device (`DTW4` = %11000000, print, read `DTW5`) and a
     * stale byte in either would centre the message in the wrong column. `DTW7` is not in this
     * build -- the Master's literal-character byte has no C64 label.
     */
    cells.push_back(Direct(L"DTW3", _universe.sentences.toLineBuffer, CellScope::Image));
    cells.push_back(Direct(L"DTW4", _universe.sentences.justify, CellScope::Image));
    cells.push_back(Direct(L"DTW5", _universe.sentences.bufferLength, CellScope::Image));
    cells.push_back(Direct(L"DTW8", _universe.sentences.caseMask, CellScope::Image));
    cells.push_back(Direct(L"MCH", _universe.message.token, CellScope::Image));
    cells.push_back(Direct(L"messXC", _universe.message.column, CellScope::Image));
    cells.push_back(Direct(L"QQ22+1", _universe.status.hyperspaceCountdown, CellScope::Image));
    cells.push_back(Direct(L"MJ", _universe.status.midJump, CellScope::Image));
    cells.push_back(Direct(L"JUNK", _universe.bubble.junk, CellScope::Image));
    cells.push_back(Direct(L"COMX", _universe.compass.x, CellScope::Image));
    cells.push_back(Direct(L"COMY", _universe.compass.y, CellScope::Image));
    /*
     * `T2` WAS AN IMAGE CELL AND HASHED A BYTE THE GAME DOES NOT WRITE (§8, M2-c).
     *
     * The port's `HLOIN` and `BOX2` parked their scratch in `T2` and `R2`, which is the BBC
     * commentary's naming; the C64's use `T` and `R`. Nothing read either byte on either side, so
     * the screens always agreed -- but the digest hashed the port's invention, so it moves with the
     * fix, under Modernize.md rule 1's second case.
     */
    cells.push_back(Direct(L"DELTA", _universe.flight.speed, CellScope::Image));
    cells.push_back(Direct(L"ALP1", _universe.flight.rollMagnitude, CellScope::Image));
    cells.push_back(Direct(L"ALP2", _universe.flight.rollSign, CellScope::Image));
    cells.push_back(Direct(L"BETA", _universe.flight.pitchRate, CellScope::Image));
    cells.push_back(Direct(L"BET1", _universe.flight.pitchMagnitude, CellScope::Image));
    cells.push_back(Direct(L"ENERGY", _universe.status.energy, CellScope::Image));
    cells.push_back(Direct(L"FSH", _universe.status.forwardShield, CellScope::Image));
    cells.push_back(Direct(L"ASH", _universe.status.aftShield, CellScope::Image));
    cells.push_back(Direct(L"CABTMP", _universe.status.cabinTemperature, CellScope::Image));
    cells.push_back(Direct(L"GNTMP", _universe.status.laserTemperature, CellScope::Image));
    cells.push_back(Direct(L"ALTIT", _universe.status.altitude, CellScope::Image));
    cells.push_back(Direct(L"FLH", _universe.status.damageFlash, CellScope::Image));
    cells.push_back(Direct(L"ECMA", _universe.status.ecmCountdown, CellScope::Image));

    // ---- seeded: a byte the game holds and the port has no field for --------------------------------

    /*
     * 6502: BEGIN's `LDA XX21+SST*2-2 / STA spasto`.
     *
     * `spasto` is `EQUW &8888` in the source and `BEGIN` overwrites it at boot with the Coriolis's
     * table entry, so a comparison against an image that had not booted used to spawn a station
     * whose blueprint was &8888 -- not a state the machine is ever in (§6.95's rule reaching a
     * third byte). The port needs no field: nothing writes `spasto` after `BEGIN`, and
     * `BlueprintOf(ShipType::Station)->address` IS `spasto` for ever.
     */
    {
      const std::uint16_t coriolis = Elite::BlueprintOf(Elite::ShipType::Station)->address;
      Cell low;
      low.name = L"spasto";
      low.scope = CellScope::Seeded;
      low.get = [coriolis]() { return static_cast<std::uint8_t>(coriolis & 0xFFu); };
      low.set = [](std::uint8_t) {};
      cells.push_back(std::move(low));

      Cell high;
      high.name = L"spasto+1";
      high.scope = CellScope::Seeded;
      high.get = [coriolis]() { return static_cast<std::uint8_t>(coriolis >> 8); };
      high.set = [](std::uint8_t) {};
      cells.push_back(std::move(high));
    }

    return cells;
  }


} // namespace GameLogicTests
