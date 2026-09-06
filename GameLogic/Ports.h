#pragma once

namespace Elite
{

  /*
   * Everything the library needs that is not a byte of `Universe` (Modernize.md §4.4, slice M3-a).
   *
   * When the state came out of the six argument-list structs this is what was left: the text
   * machinery, which cannot live in a universe that has to copy because two of its objects take a
   * seam, and the eleven interfaces the platform answers. Fourteen references where the six structs
   * held sixty-six.
   *
   * IT IS A STRUCT OF REFERENCES FOR ONE SLICE. §4.5's four ports -- `Presenter`, `Keyboard`,
   * `SoundSink`, `SaveStore` -- are M3-b's, and most of what the seven below carry is not a port at
   * all but a call into a routine that now exists. Collapsing them here would be two patterns in
   * one slice (rule 8), so this is the shape that lets M3-a change every signature once and M3-b
   * change what is behind them without touching a signature again.
   *
   * THE DECLARATIONS BELOW ARE FORWARD ONES ON PURPOSE. A reference member needs no complete type,
   * and this header including `ViewChange.h` while `ViewChange.h`'s routines take a `Ports&` is a
   * cycle. Every translation unit that reaches through one of these includes the real header
   * anyway, because it has to call something.
   */
  class TextSink;
  class TokenPrinter;
  class CharacterPrinter;
  class SightEffects;
  class ViewEffects;
  class ShipEffects;
  class ShipDrawEffects;
  class FlightLoopEffects;
  class ExtendedTokenPrinter;
  class StartUpEffects;
  class KeySource;
  class TradeScreenEffects;
  class LineEntryEffects;
  class CommanderStore;

  struct Ports
  {
    // ---- the text machinery, bound to the universe's own bytes -------------------------------
    TokenPrinter& printer;       ///< 6502: TT27 and the routines it falls into
    CharacterPrinter& characters; ///< 6502: CHPR, and `DTW1` to `DTW8` are its own state
    TextSink& sink;              ///< what `printer` and `characters` put characters through

    // ---- the seams the platform answers ------------------------------------------------------
    SightEffects& sight;      ///< 6502: SIGHT's sprite pokes
    ViewEffects& view;        ///< 6502: what a screen change reaches outside the library
    ShipEffects& tactics;     ///< 6502: JSR TACTICS, from inside `MVEIT`
    ShipDrawEffects& drawing; ///< 6502: `LL9`'s planet and explosion seams
    FlightLoopEffects& loop;  ///< 6502: the frame's sounds, spawns and music

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
     * The four the DOCKED screens need and a flight frame does not (M3-a-3).
     *
     * `keys` is `TT217`, which blocks until a key is pressed -- the docked half's whole input, where
     * a flight reads the matrix through `ControlEffects`. `trade` is `TRADEMODE`, the screen change
     * every trading screen opens with, and `entry` is the two the line editor waits and flushes
     * through. `store` is §4.5's `SaveStore` under its old name, and the one of the four that is a
     * port rather than a call into a routine that now exists.
     */
    KeySource& keys;
    TradeScreenEffects& trade;
    LineEntryEffects& entry;
    CommanderStore& store;
  };

} // namespace Elite
