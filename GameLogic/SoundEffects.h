#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Elite
{

  /*
   * The sound effects (slice 5a).
   *
   * THE SID IS WRITTEN FROM AN INTERRUPT AND NOT FROM THE GAME. `NOISE` and its relatives put an
   * effect into a BUFFER -- ten bytes a voice, three voices -- and set a flag; the raster interrupt
   * handler `COMIRQ1` runs `SOINT` once a frame, and `SOINT` is the only thing that touches the chip.
   * So the game side of the sound is a set of pure functions over a struct, and the chip side is a
   * tick that turns the struct into register writes. The port keeps that split exactly: the
   * functions here take a `SoundBuffer`, the tick emits a `SidWriteLog`, and what the executable
   * does with the log -- a synthesiser, a sample player, nothing -- is its own business.
   *
   * What makes this comparable is that the log IS the observable. `SOINT` writes the chip in a
   * particular order, and the order matters: a new effect zeroes all seven of a voice's registers
   * and then sets four of them, which a real SID hears as a gate going down and up again. A port
   * that compared the registers' final state would be blind to that, so the oracle test compares
   * the sequence of writes and not the picture they leave.
   */

  /// One register write, as the interrupt handler makes it. `reg` is the offset from
  /// SID (&D400), 0 to &18.
  struct SidWrite
  {
    std::uint8_t reg = 0;
    std::uint8_t value = 0;
  };

  /*
   * The writes one interrupt makes, in order.
   *
   * Fixed capacity rather than a vector because the tick runs once per frame for the life of the
   * program and should not allocate. An effect tick writes at most fourteen bytes a voice; the music
   * player can write more in a tick that processes a long command chain, and 512 is above anything
   * the shipped tunes reach. What does not fit is dropped and counted rather than silently lost.
   */
  struct SidWriteLog
  {
    static constexpr std::size_t CAPACITY = 512;

    std::array<SidWrite, CAPACITY> writes{};
    std::size_t count = 0;
    std::size_t dropped = 0;

    void Add(std::uint8_t _reg, std::uint8_t _value) noexcept
    {
      if (count < CAPACITY)
      {
        writes[count] = SidWrite{_reg, _value};
        ++count;
      }
      else
      {
        ++dropped;
      }
    }

    void Clear() noexcept
    {
      count = 0;
      dropped = 0;
    }
  };

  /// The SID's register map, as `SOINT` and the music player address it.
  inline constexpr std::uint8_t SID_FREQUENCY_LOW = 0;
  inline constexpr std::uint8_t SID_FREQUENCY_HIGH = 1;
  inline constexpr std::uint8_t SID_PULSE_WIDTH_LOW = 2;
  inline constexpr std::uint8_t SID_PULSE_WIDTH_HIGH = 3;
  inline constexpr std::uint8_t SID_CONTROL = 4;
  inline constexpr std::uint8_t SID_ATTACK_DECAY = 5;
  inline constexpr std::uint8_t SID_SUSTAIN_RELEASE = 6;
  inline constexpr std::uint8_t SID_VOICE_REGISTERS = 7;
  inline constexpr std::uint8_t SID_FILTER_CUTOFF_LOW = 0x15;
  inline constexpr std::uint8_t SID_FILTER_CUTOFF_HIGH = 0x16;
  inline constexpr std::uint8_t SID_FILTER_CONTROL = 0x17;
  inline constexpr std::uint8_t SID_VOLUME = 0x18;
  inline constexpr std::uint8_t SID_REGISTER_COUNT = 0x19;

  /// The three voices, and the sixteen effects `NOISE` can be asked for.
  inline constexpr std::size_t SID_VOICE_COUNT = 3;
  inline constexpr std::uint8_t SOUND_EFFECT_COUNT = 16;

  /*
   * The sixteen sounds of `sfxatk` and its five sibling tables (M5-a-4).
   *
   * THEY WERE DECLARED IN EIGHT HEADERS, one beside whichever routine first played each: `sfxbeep`
   * and two more in `Combat.h`, `sfxboop` in `ViewChange.h`, `sfxecm` in `Dashboard.h`, `sfxhyp1`
   * in `Flight.h`, five in `FlightLoop.h`, two in `Tactics.h` and `sfxtrib` here. They are ONE
   * table, indexed 0 to 15 by every one of the six `sfx*` arrays, and `SOUND_EFFECT_COUNT` was
   * already here on its own. §6.121 said it for the ship types -- a number is a property of the
   * table, not of the routine that first happened to want one -- and this is the same finding a
   * second time.
   *
   * SLOT 8 IS NAMED FOR THE FIRST TIME. The port had fifteen constants for sixteen entries; the
   * original calls it `sfxeng` and its own comment says "This sound is not used".
   */
  enum class SoundEffect : std::uint8_t
  {
    PulseLaser = 0,     ///< Pulse lasers fired by us
    HitByLaser = 1,     ///< sfxelas
    ShipExploding = 2,  ///< sfxhit
    Explosion = 3,      ///< We died, or collided
    Missile = 4,        ///< A missile launched, and a ship launching
    Beep = 5,           ///< Short and high
    Boop = 6,           ///< Long and low
    Hyperspace = 7,     ///< sfxhyp1
    Engine = 8,         ///< sfxeng -- and the original's own comment says it is not used
    Ecm = 9,            ///< sfxecm
    BeamLaser = 10,     ///< sfxblas
    MilitaryLaser = 11, ///< sfxalas
    MiningLaser = 12,   ///< sfxmlas
    EnergyBomb = 13,    ///< sfxbomb
    Trumbles = 14,      ///< The Trumbles dying
    HitByLaser2 = 15,   ///< sfxelas2

    /*
     * sfxhyp1 with its top bit SET -- and it is the ONE place in the game that sets it.
     *
     * Not a seventeenth sound: it is sound 7 again with an index that falls PAST the end of
     * `SFXPR`, so the priority byte reads as zero and the routine looks for a voice already playing
     * it rather than taking a new one. `HYPNOISE` plays 7 pitched, then 4, then this. It is an
     * enumerator rather than a `+ 128u` at the call site because the trick is the point.
     */
    HyperspaceAgain = 135,
  };

  /*
   * The buffer between the game and the interrupt.
   *
   * Ten arrays of three, one entry a voice, and one byte on its own. Every one is a label in the
   * original and the names are kept: `flag` is `SOFLG`, whose low six bits hold the effect number
   * PLUS ONE (so that zero means "nothing playing") and whose top bit says "new, not yet started";
   * `priority` is `SOPR`, which `NOISE` compares against to decide whether a new effect may take
   * the voice, and which counts DOWN as the effect plays so that an old sound loses to a new one.
   *
   * `pulseWidth` is `PULSEW`, which flips between 2 and 6 every frame that voice 1 is active and
   * is written to every voice's pulse-width register -- so every pulse effect in the game has the
   * same slow duty-cycle wobble. It starts at 2 because that is the byte the binary loads with.
   *
   * `soundOff` is `DNOIZ`, which is a configuration byte the pause screen toggled -- the settings
   * file sets it since InputTimer.md S-1 -- rather than sound state, and it is here because `NOISE`
   * is its only reader in this library. It is also the ONLY `DNOIZ`: `Universe` carried a second
   * one that the pause screen wrote and nothing read, until M5-a-5 found it.
   */
  struct SoundBuffer
  {
    std::array<std::uint8_t, SID_VOICE_COUNT> flag{};
    std::array<std::uint8_t, SID_VOICE_COUNT> counter{};
    std::array<std::uint8_t, SID_VOICE_COUNT> priority{};
    std::uint8_t pulseWidth = 2;
    std::array<std::uint8_t, SID_VOICE_COUNT> frequencyChange{};
    std::array<std::uint8_t, SID_VOICE_COUNT> frequency{};
    std::array<std::uint8_t, SID_VOICE_COUNT> control{};
    std::array<std::uint8_t, SID_VOICE_COUNT> attack{};
    std::array<std::uint8_t, SID_VOICE_COUNT> sustain{};
    std::array<std::uint8_t, SID_VOICE_COUNT> volumeRate{};

    std::uint8_t soundOff = 0;
  };

  /*
   * What `NOISE` LEAVES, and A is half of it (M3-b-2a).
   *
   * The carry was the whole answer while `DashboardEffects` was a seam, because an interface method
   * can return one thing and the seam chose the flag. It is not the whole answer: `.MA14 STA
   * INWK+35` stores what `NOISE2` left in A into the dead ship's ENERGY byte, so the sound system
   * decides a game value and a port that answered only the carry had to invent one. It invented the
   * sustain, which is what `EXNO2` had in A on the way IN; the routine overwrites it.
   *
   * Three exits and three different accumulators. The path that takes a voice ends by writing the
   * flag byte, so A is that byte. The priority refusal branches out of a comparison, so A is the
   * effect's priority. And with sound switched off the test on `DNOIZ` leaves A holding `DNOIZ` and
   * the carry untouched.
   */
  struct NoiseResult
  {
    bool carry = false; ///< the C flag on return -- set when the effect took a voice
    std::uint8_t a = 0; ///< A on return, which `.MA14` stores as a ship's energy
  };

  /*
   * Put effect `_effect` into the buffer, if it is allowed a voice.
   *
   * THREE ANSWERS, and the caller's carry is one of them (§6.99). The routine SETS the carry when
   * it takes a voice; it reaches `SOUR1`, a bare `RTS`, by two different branches, and they leave
   * different flags. The test on `DNOIZ` touches no flag, so with sound switched off the carry that
   * comes back is the one that went in. The priority branch is taken out of a comparison that
   * FAILED, so an effect refused for priority comes back with the carry clear. `OUCH` opens its
   * `DORND` on this carry, which is why the difference is worth modelling (§6.88).
   *
   * BIT 7 OF THE EFFECT NUMBER IS A FLAG and it is read late. `HYPNOISE` passes sfxhyp1 + 128 to
   * layer the drive's sound on top of itself, and the routine reads `SFXPR,Y` with the 128 still
   * in Y -- index 135, which is a byte of `COLD` -- before masking it off. Its bit 0 decides whether
   * the "already playing on some voice" scan runs, and the scan compares against the UNMASKED
   * number plus one, so it can never match. The table is extracted to 136 bytes for this one read.
   *
   * WHICH VOICE: the one already playing this effect, else the lowest priority of the three -- a
   * comparison chain that prefers voice 2 on a tie with voice 1 and voice 3 on a tie with either.
   */
  [[nodiscard]] NoiseResult PlaySoundEffect(SoundBuffer& _buffer, SoundEffect _effect, bool _carryIn) noexcept;

  /*
   * NOISE with the sustain byte and the frequency supplied instead of looked up.
   *
   * It sets the OVERFLOW flag by testing a byte that holds &60, stashes the two supplied bytes, and
   * enters `NOISE` past its own `CLV`. The two overflow branches inside `NOISE` then take the
   * supplied bytes instead of the table's, and an `EQUB &50` -- a branch-if-overflow-clear that
   * cannot branch -- swallows the `CLV` (§6.79's idiom). So this is one routine with a flag, and
   * the port writes it that way.
   */
  [[nodiscard]] NoiseResult PlaySoundEffectPitched(SoundBuffer& _buffer, SoundEffect _effect, std::uint8_t _sustain,
                                                   std::uint8_t _frequency, bool _carryIn) noexcept;

  /// BEEP, BELL -- a tail call into `NOISE`, so the carry it returns is `NOISE`'s. `BELL`
  /// prints character 7, and character 7 in `CHPR` is `R5`, which calls `BEEP`: the
  /// text printer rings it over a `SoundBuffer` since M3-b-2b, so the bell has no routine of its own.
  [[nodiscard]] NoiseResult Beep(SoundBuffer& _buffer, bool _carryIn) noexcept;

  /*
   * Find the voice playing `_effect` and run its counter down.
   *
   * It sets `SOCNT` to 1 rather than clearing the flag, so the interrupt handler ends the sound on
   * its next pass the ordinary way -- gate off, flag and priority cleared -- and nothing here writes
   * the chip. A voice not playing the effect is left alone, and so is everything if none is.
   */
  void StopSoundEffect(SoundBuffer& _buffer, SoundEffect _effect) noexcept;

  /// The same for all three voices at once: every counter to 1, so every sound
  /// ends on the next interrupt. `stopbd` calls it before silencing the chip.
  void FlushSoundEffects(SoundBuffer& _buffer) noexcept;

  /*
   * SOINT, SOUL3b -- one interrupt's worth of the effect player.
   *
   * Voice 3 down to voice 1, and for each: nothing to do if the flag is zero; if the flag's top bit
   * is set the effect is NEW, so zero the voice's seven registers and write control, attack and
   * sustain, then the frequency; otherwise apply the frequency change if there is one. Then the
   * housekeeping: the priority steps down but never below one, the counter steps down and ends the
   * sound at zero (gate off, flag and priority cleared), and every time the counter lands on a
   * multiple the volume-rate mask picks out, the sustain volume drops a step.
   *
   * THE FREQUENCY IS ONE BYTE SPREAD ACROSS TWO REGISTERS: `f` becomes `00ffffff ff000000`, so the
   * chip's sixteen-bit frequency is `f * 64`. And the sustain step SUBTRACTS SIXTEEN from a byte
   * whose high nibble is the volume, which WRAPS when the volume is already zero -- a nibble of
   * zero minus one is fifteen, so a quiet effect that keeps stepping comes back at full volume.
   * The port keeps the byte arithmetic and the oracle confirms the wrap is what the game does.
   *
   * THE PULSE WIDTH FLIPS ON ONE EXIT AND NOT THE OTHER. A voice with nothing playing branches to
   * `SOUL3b`, which steps to the next voice and, after voice 1, returns from the interrupt
   * directly; a voice that was processed reaches `SOUL3`, which after voice 1 falls into the
   * width flip on `PULSEW`. So the width alternates only on frames where voice 1 is active.
   */
  void RunSoundEffects(SoundBuffer& _buffer, SidWriteLog& _log) noexcept;

} // namespace Elite
