#pragma once

#include <cstdint>

#include "Arith.h"
#include "Canvas.h"
#include "Controls.h"
#include "Dashboard.h"
#include "ExtendedTokens.h"
#include "Lasers.h"
#include "Market.h"
#include "MemoryMap.h"
#include "Music.h"
#include "LineHeap.h"
#include "Rng.h"
#include "SoundEffects.h"
#include "Scanner.h"
#include "Picture.h"
#include "ShipDraw2x.h"
#include "ShipDraw.h"
#include "ShipMove.h"
#include "ShipSlot.h"
#include "Stardust.h"
#include "StartUp.h"
#include "TextPrint.h"
#include "Trumbles.h"

namespace Elite
{

  /*
   * 6502: abraxas, caravanserai and DFLAG -- what the screen is currently set up as.
   *
   * The first two READ LIKE REGISTERS AND ARE NOT. `abraxas` is the value the raster interrupt
   * pokes into `VIC+&18` on its next pass and `caravanserai` the one for `VIC+&11`, so both are
   * ordinary bytes here and only the handler that reads them is hardware. §6.73 made the opposite
   * mistake about `SIGHT`; this is the same question with the answer the other way round.
   *
   * `abraxas` says which block of screen RAM colours the bottom of the screen -- &81 for the one at
   * &6000 and &91 for the one at &6400, which is the dashboard's. `caravanserai` chooses standard
   * or multicolour bitmap for the same half. `DFLAG` is the cheap half of it: non-zero means the
   * dashboard is already on screen, so `wantdials` can skip copying it in again.
   */
  struct ScreenState
  {
    std::uint8_t colourBank = 0x81;  ///< 6502: abraxas
    std::uint8_t bitmapMode = 0xC0;  ///< 6502: caravanserai -- the LOWER half of the screen
    std::uint8_t dashboardShown = 0; ///< 6502: DFLAG

    /// 6502: moonflower -- `caravanserai`'s twin for the upper half, and the energy bomb's whole
    /// effect: flight loop part 3 drops it to %11010000 and the space view goes to standard bitmap
    /// mode for as long as the bomb burns.
    std::uint8_t upperBitmapMode = 0xC0;

    /*
     * 6502: welcome -- the border colour the raster handler cycles while the bomb burns.
     *
     * A table the interrupt indexes rather than a flag: `COMIRQ1` does `LDA welcome,X` and writes
     * VIC register &21, so a non-zero first byte is what makes the background flash. `BOMBOFF`
     * puts it back to zero and `COMIRQ1` increments it, which is the only place it grows.
     */
    std::uint8_t backgroundFlash = 0;

    /*
     * 6502: HFX -- AND IT DOES NOTHING IN THIS VERSION, which took a slice to establish (§6.155).
     *
     * On the BBC and the 6502 Second Processor a non-zero `HFX` makes the hyperspace rings
     * multicoloured, and `IRQ1` is the handler that reads it. This build has neither: upstream's
     * `hfx.asm` is `SKIP 1` and says in as many words that the flag is unused here; `DOHFX` exists
     * as a label with both of its instructions commented out in the original source; the C64's
     * `LL164` is four instructions and does not write it; and the C64's `COMIRQ1` does not read
     * it. Nothing in the assembled game touches this byte except `ZERO`, which clears `FRIN` to
     * `de` and catches it in passing at 1161.
     *
     * So the field is here because the memory is, and the port clears it where the original does.
     * ADR-005 §1 scheduled a per-row shift of the space view for it and there is no such effect to
     * build.
     */
    std::uint8_t hyperspaceEffect = 0;

    /*
     * 6502: RASTCT -- which half of the raster split the interrupt is setting up next.
     *
     * Zero is the space view and one is the dashboard, and `COMIRQ1` reads it as the index into
     * all seven of its tables before writing `innersec,X` back over it. It is the whole of the
     * handler's state: everything else it reads is either a constant table or one of the four
     * bytes above.
     */
    std::uint8_t rasterCounter = 0;
  };

  /*
   * Every byte of game state, owned in one place (Modernize.md §4.4, slice M3-a).
   *
   * Until M3 this was seven structs of references -- `FlightScreen`, `FlightLoop`, `TradeScreen`,
   * `SaveScreen`, `GameStart`, `MissionScreen`, `TitleScreen` -- each an argument list written out
   * because the alternative was a function with twenty parameters, and each a different subset of
   * the same bytes. The bytes themselves lived in two places that had grown apart: `Outpost::Game`
   * in `Main.cpp` held the docked half and `Outpost::FlightSession` the flight half, so which of
   * them owned a byte depended on which screen had needed it first.
   *
   * THIS STRUCT HAS NO REFERENCE MEMBER, NO VIRTUAL AND NO PRINTER, and that is the whole of its
   * design. It copies, so a test can take a snapshot; it has no vtable, so its layout is its
   * fields; and `UniverseImage` can hash it without knowing what else is in the program, which is
   * what `Game::StateHash` (M3-c) and the M0-c replay are built on.
   *
   * The two things that are NOT here are the text machinery and the seams, because both need what
   * this deliberately excludes: `ExtendedTokenPrinter` takes the control codes as a `ControlCodes*`,
   * so a universe that owned it would own a pointer to the platform. They travel beside it in `Ports` (M3-a-2), which is the
   * struct M3-b collapses to §4.5's four.
   *
   * The order below is §4.4's, which is the order the game's own memory map runs in as nearly as
   * the port's types allow: the drawing scratch, the arena, the screen, the flight, the player.
   */
  struct Universe
  {
    // ---- what the drawing works on ---------------------------------------------------------
    Canvas canvas;

    /*
     * The 640x400 picture, drawn beside the canvas and presented in its place (Resolution.md §3.4).
     *
     * IT IS THE ONE FIELD `HashState` DELIBERATELY WALKS PAST, and that is a decision rather than an
     * oversight -- `StateHash.cpp` says so where the fold would be. The replay digest exists to
     * notice a change in what the game DOES; this surface is a function of what the game did,
     * computed by code the resolution slices go on changing, and folding it would turn every
     * improvement to the rendering into a re-recording of five replay tables. R10's failure mode at
     * the scale of the whole game.
     *
     * What the digest must still catch is this surface LEAKING into the game -- a twin that rolled
     * the generator, moved a heap pointer or wrote a canvas byte -- and that is caught twice over:
     * by construction, because the twins take the surface and const references to their inputs, and
     * by the replay run with them present and absent for the same digest (Resolution.md §8.4).
     *
     * IT IS 107,682 BYTES, which takes a `Universe` from 15 KB to 123 KB -- eight times, measured
     * rather than estimated. Nothing hot copies one; the oracle's image round trips put two on the
     * stack beside a 64 KB interpreter, which is 310 KB of a Windows thread's megabyte. Stated
     * here rather than mitigated, and the number is what a later slice would have to argue with:
     * the dashboard's index plane is 71,680 of it and would halve at four bits a pixel.
     */
    Picture picture;
    DrawWorkspace draw;         ///< 6502: SC(1 0) -- the dashboard's cursor, all M2-c left of it
    MathWorkspace math;         ///< 6502: Q and K2's bottom byte, the two that outlive a call
    GeometryWorkspace geometry; ///< 6502: XX16, XX12, XX2 and XX3 -- `LL9`'s stage results

    // ---- the arena ---------------------------------------------------------------------------
    Stardust dust;         ///< 6502: SX, SY, SZ and their fractions, and NOSTM
    PlanetSunState heaps;  ///< 6502: LSO, LSX2, LSY2, LSP, K5, K6, FLAG, STP, SUNX and Yx2M1
    Bubble bubble;         ///< 6502: FRIN, K%, MANY, JUNK and SLSP
    Ship work{};           ///< 6502: INWK -- the ship the loop is working on
    LineHeap heap;         ///< 6502: the `LS%` region, and `SLSP` inside it

    ClipState clip;        ///< 6502: dontclip -- the short-range chart's, which the clipper reads
    Projection projection; ///< 6502: K3 and K4 -- where the last ship landed on screen
    K3Block axes{};        ///< 6502: K3, which `SPS1` fills for the docking check

    // ---- the screen --------------------------------------------------------------------------
    ScreenState screen;   ///< 6502: the border, the bank and the mode bytes
    TextState text;       ///< 6502: XC, YC, QQ17 and COL
    MessageState message; ///< 6502: DLY, de, MCH and messXC

    /// 6502: DTW1 to DTW8 -- the sentence machinery's bytes. They were `CharacterPrinter`'s member
    /// until M5-e-2b found the replay digest reading a printer the flight never drove (§8); the
    /// printer binds to these now, as `TextPrinter` binds to `text`, and the universe still copies.
    ExtendedTextState sentences;
    VideoState video{};         ///< 6502: the VIC-II registers `MVTRIBS` reads back

    /*
     * 6502: L1M and `l1` -- the 6510's input/output port, which decides what the address space
     * holds (M3-b-3a).
     *
     * It is here and not in `VideoState` because it is not a video register: the same routine banks
     * the SID in for `stopat`, the CIA in for `RDKEY` and the KERNAL in for `SVE`. `MemoryMap.h`
     * has the finding that put it in the library at all -- `SETL1` is neither self-modifying nor
     * inside an interrupt handler, which is what the seam it used to sit behind was justified by.
     */
    MemoryMap memoryMap;
    TrumbleSprites trumbles;    ///< 6502: TRIBCT, TRIBVX, TRIBVXH, TRIBXH

    std::uint8_t view = 0;      ///< 6502: QQ11 -- which screen is up
    std::uint8_t spaceView = 0; ///< 6502: VIEW -- which way the player is looking, 0 to 3

    /*
     * 6502: INF -- the ship block `LL9` part 1 writes two bytes of directly, as a slot.
     *
     * Only the briefing needs it to outlive a call, which is why it was `MissionScreen::shipSlot`
     * until M3-a: `PAUSE` runs INSIDE the token `BRIEF` is printing and has to find the ship that
     * `BRIEF` built several hundred instructions earlier. It is a real zero-page pointer in the
     * original and a real byte here, rather than a constant zero, because `NWSHP` chooses the slot.
     */
    std::uint8_t shipSlot = 0;

    // ---- the flight --------------------------------------------------------------------------
    FlightState flight;
    FlightStatus status;
    Compass compass{0xC3u, 0x9Cu, COMPASS_AHEAD};
    LaserBurst burst{};      ///< 6502: LASX and LASY
    KeyLogger keys{};        ///< 6502: KLO
    ControlState control;    ///< 6502: JSTX, JSTY and `auto`
    ControlOptions options;  ///< 6502: DAMP, DJD and JSTK
    std::uint8_t explosions = 0; ///< 6502: EV

    /// 6502: QQ12 -- non-zero while docked. State the docked half writes and the title screen, the
    /// briefings and `RESET` read; a reference into `Main.cpp` until M3-a.
    std::uint8_t dockedFlag = 0;

    // ---- the player --------------------------------------------------------------------------
    Commander commander;
    Rng rng;

    /*
     * 6502: QQ9 and QQ10 -- where the crosshairs are on whichever chart is up.
     *
     * The player's, not the chart's: `ping` copies the commander's own position into them and `jmp`
     * copies them back, so they outlive every screen that draws them. `ChartView` is still the
     * argument the chart routines take, because it is a VIEW over four owners; these two are the
     * pair of them it borrows.
     */
    std::uint8_t crosshairX = 0;
    std::uint8_t crosshairY = 0;

    /// 6502: QQ15 -- the seeds of the system under the crosshairs, which `TT111` writes and the
    /// status, system-data and hyperspace screens all read back.
    SystemSeeds selectedSeeds{};

    /// 6502: QQ26, QQ19 and AVL -- the market this station is offering, which lasts as long as the
    /// docking does: `GenerateMarket` writes it on arrival and every trading screen reads it.
    MarketState market;

    /*
     * 6502: NAME, NA% and DISK -- the commander's FILE, which is not the commander.
     *
     * `NAME` is the eight bytes the save and load prompts read and write; `NA%` is the save image,
     * which `JAMESON` overwrites and `DFAULT` loads back, so it is what a reset actually resets;
     * and `DISK` is one of the pause screen's thirteen toggles, a byte rather than a flag. All
     * three are memory in the original and were `GameStart`'s spans until M3-a-3.
     */
    std::array<std::uint8_t, COMMANDER_NAME_SIZE> commanderName{};
    std::array<std::uint8_t, COMMANDER_FILE_SIZE> commanderFile{};
    std::uint8_t useDisk = 0;

    /// 6502: INWK+5 -- the line editor's buffer, which the original carves out of the ship
    /// workspace and the port keeps beside it. Sixteen bytes, which is what `MT26`'s limits allow.
    std::array<std::uint8_t, 16> lineBuffer{};

    /*
     * 6502: U -- the field width `BPRNT` was last given, and it is state because SV1 does not set it.
     *
     * The competition number is printed with `CLC / JSR BPRNT` and no store to `U` first, so it
     * comes out at whatever width the last caller left behind. `U` is a scratch byte in zero page
     * that `ZERO` does not clear, and the upstream source says so in as many words. Harmless -- the
     * number always has ten digits, so all that varies is a leading space -- but a port that chose
     * a width here would be inventing one. It was `SaveScreen::numberWidth` until M3-a-3.
     */
    std::uint8_t numberWidth = 0;

    /*
     * 6502: QQ2, QQ28, tek and gov -- the system you are IN, as opposed to the one under the
     * crosshairs.
     *
     * The whole struct rather than `tek` alone, and that is the point: M3-a-1 copied the tech level
     * into the universe as a byte while `CurrentSystem` stayed in the composition root, which made
     * two bytes out of the one the original has. The flight loop reads it (part 14 spawns the
     * station and `NWSPS` picks a Coriolis or a Dodo by `tek`) and the docked half writes it on
     * arrival, so it belongs to neither half and therefore to the universe.
     */
    CurrentSystem current;

    /*
     * 6502: sound_variables -- the buffer between the game and the raster interrupt (M3-b-2a).
     *
     * `NOISE`, `NOISE2` and `NOISEOFF` write ten arrays of three and one byte on their own, and
     * nothing in the game reads the SID back: `SOINT` runs once a frame from `COMIRQ1` and is the
     * only thing that touches the chip. So the buffer is MEMORY the routines own, not a seam --
     * `DashboardEffects` existed only because the port had nowhere to put it while sound was
     * phase 5's, and `SoundEffects.cpp` has had the routines since slice 5a.
     *
     * IT IS A PLAIN STRUCT, which is why it can live here at all: no reference, no vtable, and it
     * copies with the rest of the universe.
     */
    SoundBuffer sound;

    /*
     * 6502: music_variables and MUPLA -- the docking music and the title theme (M3-b-2b).
     *
     * The same argument as the buffer above and one step further out: `startbd`, `stopbd`, `startat`
     * and `stopat` are `Music.cpp`'s routines over this struct, and what makes them a PORT rather
     * than pure memory is that they write the SID directly -- `BDENTRY` zeroes the chip and `stopat`
     * runs its twenty-five registers down -- where `NOISE` only fills a buffer. So the state is
     * here and the register writes go to `Ports::sid`, which is §4.5's `SoundSink`.
     *
     * `MusicOptions` travels inside it because the pause screen's four toggles are what `startbd`
     * reads to decide whether to play at all, and they are the player's rather than the screen's.
     */
    MusicPlayer music;

    /*
     * The seven that were loose in the executable's composition struct until M3-c and on
     * `Elite::Game` until this slice, and every one of them has a 6502 name (ADR-007 §3).
     *
     * They are here because §4.4's rule is "every byte of game state, and nothing else" and a byte
     * with a label in the original is game state by definition. They were on `Game` for one reason
     * and it was not a design one: they were loose members of `Main.cpp`'s struct when M3-c moved
     * the dispatch, and carrying them across with it was the smallest change that compiled.
     *
     * `Game::m_paused` is the one that did NOT come with them and stays where it is: the original
     * has no such byte, `FREEZE` is a loop that reads the keyboard and does not return, and the
     * state a windowed program is in instead is the port's own (ADR-005 §3's trade).
     */

    /*
     * 6502: what `TT17` leaves in X and Y -- the crosshair steps, held between the scan and the
     * dispatch that uses them.
     *
     * On the 6502 they are registers and the two routines are consecutive; here `TT102`'s work is
     * a function call away, so they have to live somewhere. Both halves of the loop write it.
     */
    CrosshairStep crosshairStep;

    /*
     * 6502: safehouse and QQ8 -- the system the countdown is running towards, and its distance.
     *
     * Separate from `selectedSeeds` (`QQ15`) because the player keeps moving the crosshairs while
     * the countdown runs, and `TT18` arrives at what was chosen when the key was pressed rather
     * than at whatever is under the crosshairs when it expires. `QQ8` is here for the same reason:
     * `hyp` measures the distance once and `TT18` spends that much fuel.
     */
    SystemSeeds jumpTarget{};
    std::uint16_t jumpDistance = 0;

    /*
     * 6502: JSTGY and JSTE -- two of the thirteen that NOTHING ELSE IN THE PORT READS.
     *
     * They are the joystick's y-inversion and its enable, and the flight controls read `JSTK` for
     * both. They are here because `DKS3` walks a contiguous run and the run is thirteen long: a
     * port that left them out would shift every option after them by two, and the "D" key would
     * switch the music instead of the disk.
     */
    std::uint8_t joystickGeometry = 0;
    std::uint8_t joystickEnabled = 0;

    /// 6502: MUTOKOLD -- what `MUTOKCH` saw last, which is how it notices the switch moving.
    std::uint8_t musicSwitchWas = 0;

    /*
     * `soundDisabled` WAS HERE AND IT WAS A SECOND `DNOIZ` (M5-a-5).
     *
     * The original has ONE: `DK4` writes it (`STX DNOIZ`, the key code itself) and `NOISE` reads it
     * (`LDA DNOIZ / BNE SOUR1`). This port had two -- this one, which the pause screen wrote, and
     * `SoundBuffer::soundOff`, which `MakeNoise` reads -- so the byte was written twice and read
     * NEVER, and pressing "2" on the pause screen did not switch the sound off. The same shape as
     * the duplicate `QQ12` the replay slice found in `FlightPort`, and found the same way: by
     * asking which bytes the digest was not watching.
     */

    /*
     * 6502: LSO -- the sun's heap, which `NWSPS` hands to the SPACE STATION (§6.112).
     *
     * The line heap and the sun heap are separate arrays here and one region in the original, and
     * a station's lines are written through the sun's window. Called once when a universe starts
     * running rather than on every frame, because it is a wiring step and not a per-frame one.
     */
    void LendSunHeap() noexcept
    {
      heap.AttachSunHeap(heaps.sun);
    }
  };

} // namespace Elite
