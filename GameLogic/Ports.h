#pragma once

namespace Elite
{

  /*
   * Everything the library needs that is not a byte of `Universe` (Modernize.md §4.4, slice M3-a).
   *
   * When the state came out of the six argument-list structs this is what was left: the text
   * machinery, which cannot live in a universe that has to copy because two of its objects take a
   * seam, and the interfaces the platform answers. Thirteen references where the six structs held
   * sixty-six.
   *
   * IT IS A STRUCT OF REFERENCES FOR ONE SLICE. §4.5's four ports -- `Presenter`, `Keyboard`,
   * `SoundSink`, `SaveStore` -- are M3-b's, and most of what the rest below carry is not a port at
   * all but a call into a routine that now exists. Collapsing them here would be two patterns in
   * one slice (rule 8), so this is the shape that lets M3-a change every signature once and M3-b
   * change what is behind them without touching a signature again.
   *
   * THE COUNT DOES NOT FALL WHILE THE PHASE RUNS AND IT CANNOT. Each of the four ports has to be
   * here before the seams it replaces can go, so a slice that lands one and removes none would put
   * `aggregate-refs` ABOVE the ceiling M3-a-3 recorded -- which rule 5 forbids outright, and rightly:
   * a ratchet that can be argued past is not one. So each of M3-b's remaining slices lands its port
   * in the same commit as at least one removal: thirteen before M3-b-2b and thirteen after, TWELVE
   * after M3-b-3a (which removed `SightEffects` and landed no port), and twelve after M3-b-3b,
   * where `Presenter` spent that credit and `TradeScreenEffects` went (§8, 2026-09-06).
   *
   * THE DECLARATIONS BELOW ARE FORWARD ONES ON PURPOSE. A reference member needs no complete type,
   * and this header including `ViewChange.h` while `ViewChange.h`'s routines take a `Ports&` is a
   * cycle. Every translation unit that reaches through one of these includes the real header
   * anyway, because it has to call something.
   */
  class TextSink;
  class TokenPrinter;
  class CharacterPrinter;
  class ShipDrawEffects;
  class SpawnChildEffects;
  class ExtendedTokenPrinter;
  class StartUpEffects;
  class KeySource;
  class LineEntryEffects;
  class Presenter;
  class CommanderStore;

  struct SidWriteLog; // SoundEffects.h -- a plain aggregate, so this cannot be a class declaration

  struct Ports
  {
    // ---- the text machinery, bound to the universe's own bytes -------------------------------
    TokenPrinter& printer;       ///< 6502: TT27 and the routines it falls into
    CharacterPrinter& characters; ///< 6502: CHPR, and `DTW1` to `DTW8` are its own state
    TextSink& sink;              ///< what `printer` and `characters` put characters through

    // ---- the seams the platform answers ------------------------------------------------------
    ShipDrawEffects& drawing; ///< 6502: `LL9`'s planet and explosion seams
    SpawnChildEffects& loop;  ///< 6502: SFS1, which M4-a's typed stage result is what it waits on

    /*
     * 6502: SID -- the chip, as the game side of the code writes it (M3-b-2b).
     *
     * THE FIRST OF SECTION 4.5's FOUR TO ARRIVE, and it is a `SidWriteLog` rather than an interface
     * because that is what the port has meant by a sound sink since slice 5a: the library runs
     * `NOISE` and the music player itself and emits REGISTER WRITES IN ORDER, and the order is the
     * observable (`SoundEffects.h`). A method per register would be the same thing with a vtable.
     *
     * It is the GAME side's log and not the interrupt's. `SOINT` and the music player's own tick
     * are called by the executable once a frame with a log of its own; what comes through here is
     * the handful of writes the game makes between interrupts -- `stopat` running the chip down and
     * `BDENTRY` zeroing it -- which the executable applies ahead of the next interrupt's, because
     * that is the order they happen in.
     *
     * It is here and not in `Universe` because it is not state: nothing in the library reads it
     * back, it is drained and cleared every frame, and the M0-c replay hashes the universe.
     */
    SidWriteLog& sid;

    /*
     * The two the title screen and the briefings need and a flight frame does not.
     *
     * `ExtendedTokenPrinter` is text machinery like the three above -- it is here rather than in
     * `Universe` because it takes the control codes as a `ControlCodes*` -- and `StartUpEffects`
     * is the seam `TITLE` and `BRIEF` wait and scan through. They joined when `TitleScreen` and
     * `MissionScreen` went, because both of those held a `FlightLoop&` and could not outlive it.
     */
    ExtendedTokenPrinter& tokens;
    StartUpEffects& start;

    /*
     * 6502: DELAY -- §4.5's `Presenter`, and the second of the four to arrive (M3-b-3b).
     *
     * It is here rather than beside `SidWriteLog` because both halves of the loop wait: the docked
     * screens pause after a beep and the title sequence between its frames. `Presenter.h` has why
     * this one is a port where `TT66`, `CLYNS`, `TRADEMODE` and `dn2` were not.
     */
    Presenter& present;

    /*
     * The four the DOCKED screens need and a flight frame does not (M3-a-3).
     *
     * `keys` is `TT217`, which blocks until a key is pressed -- the docked half's whole input, where
     * a flight reads the matrix through `ControlEffects`; `entry` is what the line editor flushes
     * the keyboard through, and both are §4.5's `Keyboard` waiting for M3-b-3c. `store` is
     * `SaveStore` under its old name.
     *
     * `trade` WAS HERE UNTIL M3-b-3b. `TRADEMODE` is `TT66` and a keyboard flush, `ClearToView` is
     * `TT66`, `ClearBottomRows` is `CLYNS` and `BeepAndPause` is `BEEP` and `DELAY` -- four seams
     * in front of routines this library has had for slices, which is §6.73 for the ninth time.
     */
    KeySource& keys;
    LineEntryEffects& entry;
    CommanderStore& store;
  };

} // namespace Elite
