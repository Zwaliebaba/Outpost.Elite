#pragma once

#include "Commander.h"
#include "ExtendedTokens.h"
#include "TextPrint.h"

#include <cstdint>
#include <span>

namespace Elite
{

  /*
   * Typing a line at the keyboard, and the commander's name (slice 2d).
   *
   * 6502: MT26, TRNME, TR1 and GTNME. This is the game's ONLY line editor -- the save and load
   * screen is the one place a player types anything other than a number -- and the number entry in
   * `gnum` is a different routine that shares nothing with it.
   *
   * 2d's row deferred all of this to 2e on the grounds that it reads the keyboard. It does, through
   * `TT217`, which is the `KeySource` seam slice 2c built and proved against four screens. What
   * actually waits for 2e is the FILE: `SVE` and `DFAULT` end at the C64 Kernal's save and load
   * calls, and nothing here touches them.
   */

  /*
   * 6502: RLINE -- the OSWORD block MT26 reads its rules from.
   *
   *   RLINE+0/1  where to store the input, which is always INWK+5
   *   RLINE+2    the maximum line length, 9, which GTNME lowers to 7 and puts back
   *   RLINE+3    the lowest character accepted, '!'
   *   RLINE+4    the highest, '{' -- and the test is `CMP RLINE+4 / BCS`, so '{' itself is refused
   *              and the range is '!' to 'z' inclusive
   *
   * It is a block rather than three constants because the routine was written against the BBC's
   * OSWORD 0, and the C64 build keeps the shape even though nothing here is an operating system
   * call any more.
   */
  struct LineLimits
  {
    std::uint8_t maxLength = 9; ///< 6502: RLINE+2
    std::uint8_t lowest = '!';  ///< 6502: RLINE+3
    std::uint8_t highest = '{'; ///< 6502: RLINE+4, exclusive
  };

  /// What MT26 left behind: the length in Y, and the carry that says how the line ended.
  struct LineResult
  {
    std::uint8_t length = 0; ///< 6502: Y on return -- the characters typed, not counting the CR
    bool escaped = false;    ///< 6502: the carry, SET only when ESCAPE ended the line
  };

  /*
   * What the line editor needs from outside GameLogic.
   *
   * Both are the C64's rather than the game's: a wait measured in vertical syncs, and a hardware
   * keyboard buffer to empty.
   */
  class LineEntryEffects
  {
  public:
    virtual ~LineEntryEffects() = default;

    /// 6502: LDY #8 / JSR DELAY -- eight VERTICAL SYNCS before the first key is read, which is
    /// 0.16 seconds on PAL and 0.13 on NTSC. §6.17 records why that distinction is not pedantry.
    virtual void WaitFrames(std::uint8_t _frames) = 0;

    /// 6502: FLKB -- empty the keyboard buffer, so a key pressed before the prompt is discarded.
    virtual void FlushKeyboard() = 0;
  };

  /*
   * 6502: MT26 -- read a line of text.
   *
   * RETURN ends it, ESCAPE abandons it, DELETE removes a character, and anything outside the
   * allowed range makes a beep instead of appearing. Three details are worth knowing before
   * reading it.
   *
   * THE BEEP AND THE CHARACTER ARE THE SAME INSTRUCTION. An accepted character reaches `JSR CHPR`
   * by falling past `LDA #7` through an `EQUB &2C` -- a `BIT absolute` opcode that swallows the two
   * bytes after it -- so the routine has ONE print, and what it prints is either the key or the bell
   * depending on which way it arrived. That is why a rejected key is not silently ignored.
   *
   * THE LOOP CONDITION IS CHPR'S CARRY. `BCC OSW0L` after the print always branches, because CHPR
   * returns with the carry clear (the same exit slice 1c-c-b found the justification depends on).
   * So the branch is a jump written as a conditional, and reading it as conditional would suggest a
   * way out of the loop that does not exist.
   *
   * RETURN IS STORED IN THE BUFFER. `OSW03` writes the carriage return at the end before returning,
   * so the line is terminated in place -- which is what makes the commander's name a CR-terminated
   * eight bytes rather than a length and seven characters.
   */
  [[nodiscard]] LineResult ReadLine(KeySource& _keys, TextSink& _screen, TextState& _text, LineEntryEffects& _effects,
                                    std::span<std::uint8_t> _buffer, const LineLimits& _limits) noexcept;

  /*
   * 6502: TRNME, which FALLS INTO TR1 -- store the typed name, then read it straight back.
   *
   * The copy back is redundant on this path and is not a mistake: TR1 is a separate entry point
   * that GTNME jumps to when nothing was typed, and TRNME simply sits above it. So a name that has
   * just been stored is immediately reloaded over the buffer it came from, which changes nothing
   * and costs eight iterations. The port reproduces the fall-through rather than stopping at the
   * first loop, because the two routines are one block of bytes and a caller of either gets both.
   *
   * The buffer is therefore NOT const, which is the visible consequence of that fall-through: a
   * routine whose name says it stores something also writes back over what it read.
   */
  void StoreCommanderName(std::span<std::uint8_t> _buffer, std::span<std::uint8_t, COMMANDER_NAME_SIZE> _name) noexcept;

  /// 6502: TR1 -- copy the stored name back into the line buffer, which is what GTNME does when
  /// the player types nothing and keeps the name they had.
  void LoadCommanderName(std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name, std::span<std::uint8_t> _buffer) noexcept;

  /*
   * 6502: GTNME -- ask for the commander's name.
   *
   * Lowers the line limit to SEVEN for the question and puts it back to nine afterwards, so the
   * name is shorter than the filename the same buffer becomes. Prints extended token 8
   * ("{single cap}COMMANDER'S NAME? ") and reads a line; if nothing was typed, TR1 puts a name
   * back and the player keeps it.
   *
   * WHICH name is the part worth being careful about. TR1 reads NA%, the LAST SAVED commander's
   * name, and not NAME, the one being played. The two are the same after any load or save and can
   * differ before one, so `_name` here is the save image's.
   *
   * The five bytes it copies first are the drive and directory part of the filename, taken from the
   * five bytes BEFORE the name in the save image. That is file-system state and belongs with
   * `SaveStore` in the executable, so it is not reproduced here -- the port's name is eight bytes
   * with nothing in front of it.
   */
  [[nodiscard]] LineResult AskCommanderName(KeySource& _keys, TextSink& _screen, TextState& _text, ExtendedTokenPrinter& _extended,
                                            LineEntryEffects& _effects, std::span<std::uint8_t> _buffer,
                                            std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name, LineLimits& _limits) noexcept;

} // namespace Elite
