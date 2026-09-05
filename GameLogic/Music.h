#pragma once

#include "SoundEffects.h"

#include <cstdint>

namespace Elite
{

  /*
   * The music (slice 5b).
   *
   * A SECOND INTERRUPT-TIME PLAYER, older than the game it is in. The header of `BDirqhere` says
   * "Music driver by Dave Dunn ... BBC source code converted from Commodore disassembly extremely
   * badly", and the "BD" on every label is the Blue Danube, which was the only tune the first
   * release had. It reads a stream of NIBBLES -- a byte holds two commands, low nibble first -- and
   * each command is a routine in a jump table: set a voice's frequency and gate it, set the
   * envelopes, set the pulse widths, rest for so many interrupts, restart the tune. Between
   * commands it runs a two-frequency vibrato on voices 2 and 3.
   *
   * IT SELF-MODIFIES IN TWO PLACES AND THE PORT KEEPS BOTH AS STATE. The vibrato alternates between
   * a note's frequency and one a little above it by rewriting the operand of a `BEQ` so that the
   * same test lands in a different half of the routine next time. That is a bit of state stored as
   * code, and `vibrato2Raised` / `vibrato3Raised` are that bit. Neither is reset when a tune starts,
   * because the code is not.
   *
   * The player's data pointer is a real sixteen-bit address in the original, pre-incremented before
   * every read. `MUSIC_DATA` is extracted from the byte that pointer STARTS on, so the port's
   * `pointer` is the original's address minus `musicstart`, and the oracle test compares them by
   * adding it back.
   */

  /*
   * 6502: MUTOK, MUFOR, MUDOCK and MUSILLY -- the four music options the pause screen toggles.
   *
   * All four are bit 7 flags (`BIT` and `BMI`), and two read backwards: `MUTOK` set means the
   * docking music is OFF, `MUFOR` set means it is FORCED on and cannot be stopped -- `stopbd` jumps
   * to `startbd` when it is set. `MUDOCK` set plays the theme in place of the Blue Danube when the
   * docking computer engages, and `MUSILLY` set lets sound effects play while music plays; clear,
   * which is how the game boots, the effect player is skipped while the music runs.
   *
   * The pause screen that toggles them is slice 4e's; these are the bytes it will toggle.
   */
  struct MusicOptions
  {
    std::uint8_t dockingMusicOff = 0;    ///< 6502: MUTOK
    std::uint8_t dockingMusicForced = 0; ///< 6502: MUFOR
    std::uint8_t dockingPlaysTheme = 0;  ///< 6502: MUDOCK
    std::uint8_t effectsDuringMusic = 0; ///< 6502: MUSILLY
  };

  /*
   * 6502: music_variables, MUPLA -- everything the player owns.
   *
   * `playing` is `MUPLA`, bit 7 set while a tune runs; `COMIRQ1` tests it to decide whether to call
   * the player at all. `tuneStart` is `value5`, the address a tune's pointer starts on, which
   * `startbd` writes and `BDENTRY` reads. `pointer` and `restart` are `BDdataptr1`/`2` and
   * `BDdataptr3`/`4`: the read position and the position command 9 rewinds to.
   *
   * THE VIBRATO NAMES ARE THE ORIGINAL'S AND THEY ARE BACKWARDS. `voice2lo1` holds the byte written
   * to SID+&8, which is the frequency's HIGH register, and `voice2hi1` the one written to SID+&7,
   * the LOW. The 32 the vibrato adds goes into `voice2hi2` with a carry into `voice2lo2`, which is
   * a sixteen-bit add of 32 to the frequency once the names are read the other way round. They are
   * kept as named because the ledger and the oracle test name them.
   */
  struct MusicPlayer
  {
    std::uint8_t playing = 0;    ///< 6502: MUPLA
    std::uint16_t tuneStart = 0; ///< 6502: value5(1 0), as an offset into MUSIC_DATA

    std::uint8_t buffer = 0;   ///< 6502: BDBUFF -- the nibbles not yet processed
    std::uint8_t counter = 0;  ///< 6502: counter -- interrupts of rest left
    std::uint8_t vibrato2 = 0; ///< 6502: vibrato2
    std::uint8_t vibrato3 = 0; ///< 6502: vibrato3

    std::uint16_t pointer = 0; ///< 6502: BDdataptr1(1 0), as an offset into MUSIC_DATA
    std::uint16_t restart = 0; ///< 6502: BDdataptr3(1 0), the same

    std::uint8_t value0 = 0; ///< 6502: value0 -- command 6 counts it and nothing reads it
    std::uint8_t value1 = 0; ///< 6502: value1 -- voice 1's control register, from command 13
    std::uint8_t value2 = 0; ///< 6502: value2 -- voice 2's
    std::uint8_t value3 = 0; ///< 6502: value3 -- voice 3's
    std::uint8_t value4 = 0; ///< 6502: value4 -- the rest length, from command 12

    std::uint8_t voice2lo1 = 0; ///< 6502: voice2lo1
    std::uint8_t voice2hi1 = 0; ///< 6502: voice2hi1
    std::uint8_t voice2lo2 = 0; ///< 6502: voice2lo2
    std::uint8_t voice2hi2 = 0; ///< 6502: voice2hi2
    std::uint8_t voice3lo1 = 0; ///< 6502: voice3lo1
    std::uint8_t voice3hi1 = 0; ///< 6502: voice3hi1
    std::uint8_t voice3lo2 = 0; ///< 6502: voice3lo2
    std::uint8_t voice3hi2 = 0; ///< 6502: voice3hi2

    /*
     * 6502: the operand of the BEQ at BDbeqmod1 / BDbeqmod2 -- which half of the vibrato routine
     * the next trigger lands in.
     *
     * False is the assembled operand, which reaches the LABELLED half (`BDlab24`, `BDlab23`): the
     * one that writes the note's own frequency and rewrites the operand to reach the other. So the
     * first trigger after a note restates the note and the second raises it, and "raised" here
     * means "the next trigger writes the raised copy".
     */
    bool vibrato2Raised = false;
    bool vibrato3Raised = false;

    MusicOptions options;
  };

  /*
   * 6502: startbd, april16, startat2 -- start the docking music, if the options allow.
   *
   * `BIT MUDOCK / BMI startat` chooses the theme over the Blue Danube; then `startat2` records the
   * tune's start, and the checks run: already playing, do nothing; forced on, start regardless;
   * switched off, do nothing; else start. `BDENTRY` zeroes the chip and `MUPLA` goes to &FF.
   */
  void StartDockingMusic(MusicPlayer& _music, SidWriteLog& _log) noexcept;

  /// 6502: startat -- the title theme, through the same `startat2` and so the same checks.
  void StartTheme(MusicPlayer& _music, SidWriteLog& _log) noexcept;

  /*
   * 6502: stopbd -- stop the docking music, unless something says not to.
   *
   * `BIT MULIE / BMI itsoff`: the title screen sets `_titleReset` around its `RESET` so that the
   * reset does not silence the theme it has just started. `BIT MUFOR / BMI startbd`: forced music
   * cannot be stopped, and the routine goes to START it instead, which does nothing if it is
   * already playing. Otherwise `stopat`.
   */
  void StopDockingMusic(MusicPlayer& _music, std::uint8_t _titleReset, SoundBuffer& _buffer, SidWriteLog& _log) noexcept;

  /*
   * 6502: stopat -- stop whatever is playing.
   *
   * Nothing if nothing is. Otherwise `SOFLUSH` runs the effects down, `MUPLA` goes to zero, the
   * chip's twenty-five registers are zeroed from &18 down to 0, and the volume register is set to
   * fifteen -- so the effects, which `SOFLUSH` has just told to end, end on a chip that can be heard.
   */
  void StopMusic(MusicPlayer& _music, SoundBuffer& _buffer, SidWriteLog& _log) noexcept;

  /*
   * 6502: BDENTRY -- start the tune at `tuneStart`.
   *
   * Clears the nibble buffer, the rest counter and both vibrato counters; zeroes the chip's
   * registers from &18 down to ONE -- `DEX / BNE`, so register 0 is not touched; points both data
   * pointers at the start; and sets the volume to fifteen.
   */
  void BeginTune(MusicPlayer& _music, SidWriteLog& _log) noexcept;

  /*
   * 6502: BDirqhere -- one interrupt of the music player. The fifteen commands are BDRO1 to BDRO15
   * and the helpers BDlab1 to BDlab24, each marked where it lands in Music.cpp.
   *
   * If a rest is in progress the counter comes down and the vibrato runs. Otherwise commands are
   * taken from the nibble stream and performed, one after another, until one of them is a rest --
   * commands 8 and 15 -- with a length above zero. A rest of zero does not end the pass, and a tune
   * with no rests would never return, as the original would not.
   *
   * The nibble stream: `BDBUFF` holds what is left of the current byte. If it is non-zero its low
   * nibble is the next command and the byte shifts down; if it is zero the next byte is fetched, its
   * low nibble is the command and its high nibble stays in the buffer. A fetched byte whose low
   * nibble is zero indexes the jump table at minus one and jumps through two bytes of code, which
   * the shipped tunes never do; the port stops the pass there rather than model the crash.
   */
  void RunMusic(MusicPlayer& _music, SidWriteLog& _log) noexcept;

  /*
   * 6502: COMIRQ1, coffee, RASTCT, zebop -- the interrupt's SID half.
   *
   * The handler fires twice a frame, once at the top of the screen and once at the dashboard, and
   * `RASTCT` flips between them; only the dashboard pass reaches the sound. On that pass: music if
   * `MUPLA` says so, and then the effects unless music is playing and `MUSILLY` is clear -- in which
   * case `JMP coffee` returns from the interrupt without them. The screen half of the handler --
   * `zebop`, `abraxas` and the other VIC-II writes -- is the canvas's, and the port's
   * `SyncVideoRegisters` is where it lives.
   *
   * One call is one frame of sound. What the frame is worth in samples is the executable's business.
   */
  void RunSoundInterrupt(SoundBuffer& _buffer, MusicPlayer& _music, SidWriteLog& _log) noexcept;

} // namespace Elite
