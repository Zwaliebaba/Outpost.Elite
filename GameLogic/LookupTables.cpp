#include "pch.h"

#include "LookupTables.h"

#include "Canvas.h"
#include "Commander.h"
#include "Equipment.h"
#include "ShipBlueprint.h"
#include "SoundEffects.h"
#include "VideoState.h"

#include <tuple>

/*
 * The shapes of the generated tables, as assertions instead of prose (slice 5b).
 *
 * THIS FILE HOLDS NO CODE ON PURPOSE. `LookupTables.h` describes every table it declares, and a
 * good many of those descriptions are arithmetic: the font is "96 characters of eight rows", the
 * equipment prices are "two bytes an item" for fourteen items, the scanner colours are "sized by
 * what indexes it, which is a ship TYPE". §6.8's rule for the whole ledger says the same thing --
 * size a table from what can INDEX it, never from where the next label happens to sit. Each of
 * those sentences is a claim that a regenerated table could quietly break, and until this file
 * every one of them was checked by a person reading two files at once.
 *
 * WHY IT IS A TRANSLATION UNIT AND NOT PART OF THE HEADER. The claims tie a table's length to a
 * constant that belongs to the code that INDEXES it -- `SHIP_TYPE_COUNT`, `SPRITE_DEFINITION_COUNT`,
 * `Canvas::CELL_ROWS`, `SOUND_EFFECT_COUNT` -- and `LookupTables.h` is included by almost
 * everything. Reaching those constants from the header would make the tables depend on the port
 * rather than the other way round. Here it costs one translation unit that emits nothing.
 *
 * AND WHY THE ASSERTIONS ARE ABOUT SIZE. `std::tuple_size_v` reads the DECLARATION and never the
 * bytes, so these hold without the tables' values being visible -- which is what lets the big ones
 * stay in their own `.cpp` instead of putting 160 KB of initialiser into every translation unit
 * that wants a font. What the values are is the assembler's business and `tools/extract_tables.py`
 * takes them straight from the assembled game; what SHAPE they have to be is this port's, and that
 * is the half a compiler can hold.
 */
namespace Elite
{

  namespace
  {

    /// The declared length of one of the generated arrays, without reading a byte of it.
    template <typename Table> inline constexpr std::size_t EXTENT = std::tuple_size_v<Table>;

  } // namespace

  // ---- the pictures -----------------------------------------------------------------------------

  // 6502: FONT -- "96 characters of eight rows, starting at space".
  static_assert(EXTENT<decltype(FONT_DATA)> == 96u * 8u, "the font is 96 characters of eight rows");

  // 6502: spritp -- "the seven sprite definitions, 64 bytes each", and both numbers are `VideoState`'s.
  static_assert(EXTENT<decltype(SPRITE_DEFINITIONS)> == SPRITE_DEFINITION_COUNT * SPRITE_BYTES,
                "the sprite sheet is SPRITE_DEFINITION_COUNT definitions of SPRITE_BYTES");

  // ---- the screen -------------------------------------------------------------------------------

  // The two halves of one 16-bit table are the same length, or the address they form is nonsense.
  static_assert(EXTENT<decltype(ROW_ADDRESS_LOW)> == EXTENT<decltype(ROW_ADDRESS_HIGH)>, "ylookup is one table in two halves");
  static_assert(EXTENT<decltype(CELL_ADDRESS_LOW)> == EXTENT<decltype(CELL_ADDRESS_HIGH)>, "celllook is one table in two halves");

  // 6502: ylookup -- indexed by a screen row, and a row is a byte.
  static_assert(EXTENT<decltype(ROW_ADDRESS_LOW)> == 256u, "every y a byte can hold has a row address");

  // 6502: celllook -- indexed by a CHARACTER row, which is what `Canvas` counts.
  static_assert(EXTENT<decltype(CELL_ADDRESS_LOW)> == static_cast<std::size_t>(Canvas::CELL_ROWS),
                "celllook has one entry per character row");

  // The dashboard's two palettes cover the same cells: screen RAM and colour RAM, one byte each.
  static_assert(EXTENT<decltype(DASHBOARD_SCREEN_COLOURS)> == EXTENT<decltype(DASHBOARD_COLOUR_RAM)>,
                "the dashboard's screen RAM and colour RAM cover the same cells");
  static_assert(EXTENT<decltype(DASHBOARD_SCREEN_COLOURS)> ==
                  static_cast<std::size_t>(Canvas::CELL_COLUMNS) * (Canvas::CELL_ROWS - Canvas::DASHBOARD_CELL_ROW),
                "and they are exactly the cells below the raster split");

  /*
   * 6502: CTWOS2 -- "two identical entries per pixel so that a pair can be read at any offset",
   * plus the two WRAPPED cases `CPIX2` reads two entries along. Ten and not eight, and the header
   * says in as many words that the extra two are not padding.
   */
  static_assert(EXTENT<decltype(MULTICOLOUR_MASK_TABLE)> == EXTENT<decltype(PIXEL_MASK_TABLE)> + 2u,
                "CTWOS2 is the aligned masks plus the two wrapped cases");

  // 6502: TWFR and TWFL -- a horizontal line's two ends, one entry per x within a byte.
  static_assert(EXTENT<decltype(LINE_RIGHT_MASK_TABLE)> == EXTENT<decltype(LINE_LEFT_MASK_TABLE)>,
                "the two ends of a horizontal line are indexed the same way");
  static_assert(EXTENT<decltype(LINE_RIGHT_MASK_TABLE)> == EXTENT<decltype(PIXEL_MASK_TABLE)>,
                "and by the same x within the byte the pixel masks use");

  // 6502: the four tables `COMIRQ1` indexes by `RASTCT` -- one entry per half of the split.
  static_assert(EXTENT<decltype(RASTER_NEXT_LINE_TABLE)> == 2u, "the raster split has two halves");
  static_assert(EXTENT<decltype(RASTER_SPRITE_MULTICOLOUR_TABLE)> == EXTENT<decltype(RASTER_NEXT_LINE_TABLE)>, "and so does santana");
  static_assert(EXTENT<decltype(RASTER_SPRITE_COLOUR_TABLE)> == EXTENT<decltype(RASTER_NEXT_LINE_TABLE)>, "and lotus");
  static_assert(EXTENT<decltype(RASTER_NEXT_COUNTER_TABLE)> == EXTENT<decltype(RASTER_NEXT_LINE_TABLE)>, "and innersec");

  // ---- the ships --------------------------------------------------------------------------------

  /*
   * 6502: scacol -- "sized by what indexes it, which is a ship TYPE", and a type runs 0 to
   * `SHIP_TYPE_COUNT` inclusive because type 0 is the missing ship.
   */
  static_assert(EXTENT<decltype(SCANNER_COLOUR_TABLE)> == static_cast<std::size_t>(SHIP_TYPE_COUNT) + 1u,
                "the scanner has a colour for every ship type and for none");

  // ---- the trade ---------------------------------------------------------------------------------

  // 6502: PRXS -- "two bytes an item, low byte first", for FOURTEEN items on this build.
  static_assert(EXTENT<decltype(EQUIPMENT_PRICES)> == static_cast<std::size_t>(EQUIPMENT_ITEM_COUNT) * 2u,
                "every item of equipment has a two-byte price");

  // ---- the commander -----------------------------------------------------------------------------

  /*
   * 6502: NA2% -- "eight bytes of name and then the 77-byte data block, and thirteen more because
   * JAMESON copies ninety-eight". The slack is real and is copied by the game, so the assertion is
   * that the block FITS rather than that it fills.
   */
  static_assert(EXTENT<decltype(DEFAULT_COMMANDER)> >= COMMANDER_FILE_SIZE, "the default commander holds a name and a block");

  // ---- the sound ----------------------------------------------------------------------------------

  /*
   * 6502: the eight per-effect tables `NOISE` indexes at `LDX` the effect number. They are eight
   * parallel columns of one table with sixteen rows, and `SoundEffect` is the row.
   */
  static_assert(EXTENT<decltype(EFFECT_COUNT_TABLE)> == SOUND_EFFECT_COUNT, "one entry per sound effect");
  static_assert(EXTENT<decltype(EFFECT_FREQUENCY_TABLE)> == SOUND_EFFECT_COUNT, "one entry per sound effect");
  static_assert(EXTENT<decltype(EFFECT_CONTROL_TABLE)> == SOUND_EFFECT_COUNT, "one entry per sound effect");
  static_assert(EXTENT<decltype(EFFECT_ATTACK_TABLE)> == SOUND_EFFECT_COUNT, "one entry per sound effect");
  static_assert(EXTENT<decltype(EFFECT_SUSTAIN_TABLE)> == SOUND_EFFECT_COUNT, "one entry per sound effect");
  static_assert(EXTENT<decltype(EFFECT_FREQUENCY_CHANGE_TABLE)> == SOUND_EFFECT_COUNT, "one entry per sound effect");
  static_assert(EXTENT<decltype(EFFECT_VOLUME_RATE_TABLE)> == SOUND_EFFECT_COUNT, "one entry per sound effect");

  // ---- the Trumbles --------------------------------------------------------------------------------

  // 6502: TRIBTA and TRIBMA -- "how many Trumble sprites to show" and "which sprite", by population.
  static_assert(EXTENT<decltype(TRUMBLE_COUNT_TABLE)> == EXTENT<decltype(TRUMBLE_SPRITE_TABLE)>,
                "the two Trumble tables are indexed by the same population");

  // 6502: TRIBDIR and TRIBDIRH -- "four entries because two bits are what index them".
  static_assert(EXTENT<decltype(TRUMBLE_DIRECTION_TABLE)> == 4u, "two bits are what index the directions");
  static_assert(EXTENT<decltype(TRUMBLE_DIRECTION_HIGH_TABLE)> == EXTENT<decltype(TRUMBLE_DIRECTION_TABLE)>,
                "and the high half is the same table");

} // namespace Elite
