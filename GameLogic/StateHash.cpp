#include "pch.h"

#include "StateHash.h"

#include "Colours.h"
#include "HeapOffset.h"
#include "ShipBlueprint.h"
#include "Universe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Elite
{
  namespace
  {
    /// The running FNV-1a hash and the four ways a field folds into it.
    class Folder
    {
    public:
      void Byte(std::uint8_t _byte) noexcept
      {
        m_hash ^= _byte;
        m_hash *= PRIME;
      }

      void Bytes(std::span<const std::uint8_t> _bytes) noexcept
      {
        for (const std::uint8_t byte : _bytes)
        {
          Byte(byte);
        }
      }

      /// Low byte first, which is the 6502's order and the codecs'.
      void Word(std::uint16_t _word) noexcept
      {
        Byte(static_cast<std::uint8_t>(_word & 0xFFu));
        Byte(static_cast<std::uint8_t>(_word >> 8));
      }

      void Flag(bool _flag) noexcept
      {
        Byte(_flag ? 1u : 0u);
      }

      [[nodiscard]] std::uint64_t Value() const noexcept
      {
        return m_hash;
      }

    private:
      static constexpr std::uint64_t OFFSET_BASIS = 14695981039346656037ull;
      static constexpr std::uint64_t PRIME = 1099511628211ull;

      std::uint64_t m_hash = OFFSET_BASIS;
    };

    // ---- one overload per state struct, fields in declaration order. Named for the channel census,
    // ---- which lists every reader of a workspace field by function name and lists this one.

    void FoldIntoStateHash(Folder& _into, const Canvas& _canvas) noexcept
    {
      _into.Bytes(_canvas.Screen());
      for (int cell = 0; cell < Canvas::CELL_COLUMNS * Canvas::CELL_ROWS; ++cell)
      {
        _into.Byte(_canvas.CellColour(cell));
      }
      _into.Byte(ColourIndex(_canvas.Background()));
      _into.Byte(ColourIndex(_canvas.SpaceViewBackground()));
      _into.Flag(_canvas.SpaceViewMulticolour());
      _into.Bytes(_canvas.SpriteMulticolour());
      for (const Colour colour : _canvas.ExplosionColour())
      {
        _into.Byte(ColourIndex(colour));
      }
      _into.Flag(_canvas.DashboardShown());
    }

    void FoldIntoStateHash(Folder& _into, const DrawWorkspace& _draw) noexcept
    {
      _into.Word(_draw.sc);
    }

    void FoldIntoStateHash(Folder& _into, const MathWorkspace& _math) noexcept
    {
      _into.Byte(_math.q);
      _into.Byte(_math.k2Low);
    }

    void FoldIntoStateHash(Folder& _into, const GeometryWorkspace& _geometry) noexcept
    {
      _into.Bytes(_geometry.xx16);
      _into.Bytes(_geometry.xx12);
      _into.Bytes(_geometry.xx2);
      _into.Bytes(_geometry.xx3);
    }

    void FoldIntoStateHash(Folder& _into, const Stardust& _dust) noexcept
    {
      _into.Bytes(_dust.x);
      _into.Bytes(_dust.xLow);
      _into.Bytes(_dust.y);
      _into.Bytes(_dust.yLow);
      _into.Bytes(_dust.z);
      _into.Bytes(_dust.zLow);
      _into.Byte(_dust.count);
      _into.Byte(_dust.newzp);
    }

    void FoldIntoStateHash(Folder& _into, const PlanetSunState& _heaps) noexcept
    {
      _into.Bytes(_heaps.sun);
      _into.Bytes(_heaps.ball);
      _into.Byte(_heaps.lsp);
      _into.Byte(_heaps.sunX);
      _into.Byte(_heaps.sunXNext);
      _into.Byte(_heaps.yx2M1);
      _into.Byte(_heaps.pltog);
      _into.Byte(_heaps.v);
      _into.Byte(_heaps.vNext);
      _into.Bytes(_heaps.k5);
      _into.Bytes(_heaps.k6);
      _into.Byte(_heaps.stp);
      _into.Byte(_heaps.flag);
    }

    void FoldIntoStateHash(Folder& _into, const Ship& _ship) noexcept
    {
      _into.Bytes(_ship.ToBytes()); // 6502: the K% layout, which is the codec's whole job
    }

    void FoldIntoStateHash(Folder& _into, const Bubble& _bubble) noexcept
    {
      _into.Bytes(_bubble.slots);
      for (const Ship& block : _bubble.blocks)
      {
        FoldIntoStateHash(_into, block);
      }
      _into.Bytes(_bubble.counts);
      _into.Byte(_bubble.junk);
      _into.Byte(_bubble.missileTarget);
      _into.Word(_bubble.heapBottom.up);
      _into.Byte(static_cast<std::uint8_t>(_bubble.stationType));
    }

    void FoldIntoStateHash(Folder& _into, const LineHeap& _heap) noexcept
    {
      _into.Bytes(_heap.Bytes()); // the arena only: the lent sun heap is `PlanetSunState::sun`'s
    }

    void FoldIntoStateHash(Folder& _into, const ClipState& _clip) noexcept
    {
      _into.Byte(_clip.dontclip);
    }

    void FoldIntoStateHash(Folder& _into, const Projection& _projection) noexcept
    {
      _into.Byte(_projection.x);
      _into.Byte(_projection.x1);
      _into.Byte(_projection.y);
      _into.Byte(_projection.y1);
    }

    void FoldIntoStateHash(Folder& _into, const ScreenState& _screen) noexcept
    {
      _into.Byte(_screen.colourBank);
      _into.Byte(_screen.bitmapMode);
      _into.Byte(_screen.dashboardShown);
      _into.Byte(_screen.upperBitmapMode);
      _into.Byte(_screen.backgroundFlash);
      _into.Byte(_screen.hyperspaceEffect);
      _into.Byte(_screen.rasterCounter);
    }

    void FoldIntoStateHash(Folder& _into, const TextState& _text) noexcept
    {
      _into.Byte(_text.column);
      _into.Byte(_text.row);
      _into.Byte(_text.caseFlags);
      _into.Byte(_text.palette.Byte());
    }

    void FoldIntoStateHash(Folder& _into, const MessageState& _message) noexcept
    {
      _into.Byte(_message.delay);
      _into.Byte(_message.append);
      _into.Byte(_message.token);
      _into.Byte(_message.column);
    }

    void FoldIntoStateHash(Folder& _into, const ExtendedTextState& _sentences) noexcept
    {
      _into.Byte(_sentences.lowerCaseBits);
      _into.Byte(_sentences.sentenceStart);
      _into.Byte(_sentences.toLineBuffer);
      _into.Byte(_sentences.justify);
      _into.Byte(_sentences.bufferLength);
      _into.Byte(_sentences.alwaysLower);
      _into.Byte(_sentences.literal);
      _into.Byte(_sentences.caseMask);
    }

    void FoldIntoStateHash(Folder& _into, const VideoState& _video) noexcept
    {
      _into.Byte(_video.enabled);
      _into.Byte(_video.expanded);
      for (const std::uint16_t x : _video.x)
      {
        _into.Word(x);
      }
      _into.Bytes(_video.y);
      for (const Colour colour : _video.colour)
      {
        _into.Byte(ColourIndex(colour));
      }
    }

    void FoldIntoStateHash(Folder& _into, const MemoryMap& _map) noexcept
    {
      _into.Byte(_map.requested);
      _into.Byte(_map.port);
    }

    void FoldIntoStateHash(Folder& _into, const TrumbleSprites& _trumbles) noexcept
    {
      _into.Byte(_trumbles.count);
      _into.Bytes(_trumbles.velocityX);
      _into.Bytes(_trumbles.velocityXHigh);
      _into.Bytes(_trumbles.coordinateXHigh);
    }

    void FoldIntoStateHash(Folder& _into, const FlightState& _flight) noexcept
    {
      _into.Byte(_flight.alpha);
      _into.Byte(_flight.alp1);
      _into.Byte(_flight.alp2);
      _into.Byte(_flight.alp2Next);
      _into.Byte(_flight.beta);
      _into.Byte(_flight.bet1);
      _into.Byte(_flight.bet2);
      _into.Byte(_flight.bet2Next);
      _into.Byte(_flight.delta);
      _into.Byte(_flight.delt4);
      _into.Byte(_flight.delt4Next);
      _into.Byte(_flight.mainLoopCounter);
      _into.Byte(_flight.slot);
      _into.Byte(static_cast<std::uint8_t>(_flight.type));
      _into.Word(_flight.blueprint != nullptr ? _flight.blueprint->address : std::uint16_t{0}); // 6502: XX0
      _into.Byte(_flight.rat);
      _into.Byte(_flight.rat2);
      _into.Byte(_flight.steerCone);
    }

    void FoldIntoStateHash(Folder& _into, const FlightStatus& _status) noexcept
    {
      _into.Byte(_status.laserTemperature);
      _into.Byte(_status.titleReset);
      _into.Byte(_status.hyperspaceCounter);
      _into.Byte(_status.hyperspaceCountdown);
      _into.Byte(_status.forwardShield);
      _into.Byte(_status.aftShield);
      _into.Byte(_status.energy);
      _into.Byte(_status.cabinTemperature);
      _into.Byte(_status.altitude);
      _into.Byte(_status.ecmCountdown);
      _into.Byte(_status.ecmOurs);
      _into.Byte(_status.damageFlash);
      _into.Byte(_status.viewLaser);
      _into.Byte(_status.midJump);
      _into.Byte(_status.laserPower);
      _into.Byte(_status.laserCount);
      _into.Byte(_status.missileArmed);
    }

    void FoldIntoStateHash(Folder& _into, const Compass& _compass) noexcept
    {
      _into.Byte(_compass.x);
      _into.Byte(_compass.y);
      _into.Byte(PatternByte(_compass.pattern));
    }

    void FoldIntoStateHash(Folder& _into, const LaserBurst& _burst) noexcept
    {
      _into.Byte(_burst.x);
      _into.Byte(_burst.y);
    }

    void FoldIntoStateHash(Folder& _into, const ControlState& _control) noexcept
    {
      _into.Byte(_control.roll);
      _into.Byte(_control.pitch);
      _into.Byte(_control.dockingComputer);
    }

    void FoldIntoStateHash(Folder& _into, const ControlOptions& _options) noexcept
    {
      _into.Byte(_options.dampingDisabled);
      _into.Byte(_options.recentreDisabled);
      _into.Byte(_options.joystick);
      _into.Byte(_options.authorNames);
    }

    void FoldIntoStateHash(Folder& _into, const Commander& _commander) noexcept
    {
      _into.Bytes(_commander.ToBytes()); // 6502: TP to CHK, the codec's layout
    }

    void FoldIntoStateHash(Folder& _into, const SystemSeeds& _seeds) noexcept
    {
      _into.Bytes(_seeds.bytes);
    }

    void FoldIntoStateHash(Folder& _into, const MarketState& _market) noexcept
    {
      _into.Byte(_market.randomiser);
      _into.Bytes(_market.price);
      _into.Bytes(_market.availability);
    }

    void FoldIntoStateHash(Folder& _into, const CurrentSystem& _current) noexcept
    {
      FoldIntoStateHash(_into, _current.seeds);
      _into.Byte(_current.economy);
      _into.Byte(_current.techLevel);
      _into.Byte(_current.government);
    }

    void FoldIntoStateHash(Folder& _into, const SoundBuffer& _sound) noexcept
    {
      _into.Bytes(_sound.flag);
      _into.Bytes(_sound.counter);
      _into.Bytes(_sound.priority);
      _into.Byte(_sound.pulseWidth);
      _into.Bytes(_sound.frequencyChange);
      _into.Bytes(_sound.frequency);
      _into.Bytes(_sound.control);
      _into.Bytes(_sound.attack);
      _into.Bytes(_sound.sustain);
      _into.Bytes(_sound.volumeRate);
      _into.Byte(_sound.soundOff);
    }

    void FoldIntoStateHash(Folder& _into, const MusicOptions& _options) noexcept
    {
      _into.Byte(_options.dockingMusicOff);
      _into.Byte(_options.dockingMusicForced);
      _into.Byte(_options.dockingPlaysTheme);
      _into.Byte(_options.effectsDuringMusic);
    }

    void FoldIntoStateHash(Folder& _into, const MusicPlayer& _music) noexcept
    {
      _into.Byte(_music.playing);
      _into.Word(_music.tuneStart);
      _into.Byte(_music.buffer);
      _into.Byte(_music.counter);
      _into.Byte(_music.vibrato2);
      _into.Byte(_music.vibrato3);
      _into.Word(_music.pointer);
      _into.Word(_music.restart);
      _into.Byte(_music.value0);
      _into.Byte(_music.value1);
      _into.Byte(_music.value2);
      _into.Byte(_music.value3);
      _into.Byte(_music.value4);
      _into.Byte(_music.voice2lo1);
      _into.Byte(_music.voice2hi1);
      _into.Byte(_music.voice2lo2);
      _into.Byte(_music.voice2hi2);
      _into.Byte(_music.voice3lo1);
      _into.Byte(_music.voice3hi1);
      _into.Byte(_music.voice3lo2);
      _into.Byte(_music.voice3hi2);
      _into.Flag(_music.vibrato2Raised);
      _into.Flag(_music.vibrato3Raised);
      FoldIntoStateHash(_into, _music.options);
    }

    void FoldIntoStateHash(Folder& _into, const CrosshairStep& _step) noexcept
    {
      _into.Byte(_step.y);
    }

  } // namespace

  std::uint64_t HashState(const Universe& _universe) noexcept
  {
    Folder into;

    // ---- what the drawing works on
    FoldIntoStateHash(into, _universe.canvas);
    FoldIntoStateHash(into, _universe.draw);
    FoldIntoStateHash(into, _universe.math);
    FoldIntoStateHash(into, _universe.geometry);

    // ---- the arena
    FoldIntoStateHash(into, _universe.dust);
    FoldIntoStateHash(into, _universe.heaps);
    FoldIntoStateHash(into, _universe.bubble);
    FoldIntoStateHash(into, _universe.work);
    FoldIntoStateHash(into, _universe.heap);
    FoldIntoStateHash(into, _universe.clip);
    FoldIntoStateHash(into, _universe.projection);
    into.Bytes(_universe.axes);

    // ---- the screen
    FoldIntoStateHash(into, _universe.screen);
    FoldIntoStateHash(into, _universe.text);
    FoldIntoStateHash(into, _universe.message);
    FoldIntoStateHash(into, _universe.sentences);
    FoldIntoStateHash(into, _universe.video);
    FoldIntoStateHash(into, _universe.memoryMap);
    FoldIntoStateHash(into, _universe.trumbles);
    into.Byte(_universe.view);
    into.Byte(_universe.spaceView);
    into.Byte(_universe.shipSlot);

    // ---- the flight
    FoldIntoStateHash(into, _universe.flight);
    FoldIntoStateHash(into, _universe.status);
    FoldIntoStateHash(into, _universe.compass);
    FoldIntoStateHash(into, _universe.burst);
    into.Bytes(_universe.keys);
    FoldIntoStateHash(into, _universe.control);
    FoldIntoStateHash(into, _universe.options);
    into.Byte(_universe.explosions);
    into.Byte(_universe.dockedFlag);

    // ---- the player
    FoldIntoStateHash(into, _universe.commander);
    into.Bytes(_universe.rng.State());
    into.Byte(_universe.crosshairX);
    into.Byte(_universe.crosshairY);
    FoldIntoStateHash(into, _universe.selectedSeeds);
    FoldIntoStateHash(into, _universe.market);
    into.Bytes(_universe.commanderName);
    into.Bytes(_universe.commanderFile);
    into.Byte(_universe.useDisk);
    into.Bytes(_universe.lineBuffer);
    into.Byte(_universe.numberWidth);
    FoldIntoStateHash(into, _universe.current);
    FoldIntoStateHash(into, _universe.sound);
    FoldIntoStateHash(into, _universe.music);
    FoldIntoStateHash(into, _universe.crosshairStep);

    // ---- the five M5-a-6 added, in their declaration order
    FoldIntoStateHash(into, _universe.jumpTarget);
    into.Word(_universe.jumpDistance);
    into.Byte(_universe.joystickGeometry);
    into.Byte(_universe.joystickEnabled);
    into.Byte(_universe.musicSwitchWas);

    return into.Value();
  }
}
