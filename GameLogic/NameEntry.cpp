#include "pch.h"

#include "NameEntry.h"

/*
 * The line editor and the commander's name (slice 2d).
 */

namespace Elite
{

  namespace
  {
    /// 6502: MAG2 = $40, purple, and the &10 that OSW03 and OSW04 put back. The same pair gnum uses.
    constexpr std::uint8_t TEXT_COLOUR_TYPING = 0x40;
    constexpr std::uint8_t TEXT_COLOUR_NORMAL = 0x10;

    /// 6502: CMP #13 / CMP #27 / CMP #127 -- the three keys that are not text.
    constexpr std::uint8_t KEY_RETURN = 13;
    constexpr std::uint8_t KEY_ESCAPE = 27;
    constexpr std::uint8_t KEY_DELETE = 127;

    /// 6502: LDA #7 -- the bell, printed in place of a character the line will not take.
    constexpr std::uint8_t BELL = 7;

    /// 6502: LDA #12 / JMP CHPR -- the newline OSW03 ends on.
    constexpr std::uint8_t NEWLINE = 12;

    /// 6502: LDY #8 / JSR DELAY.
    constexpr std::uint8_t SETTLE_FRAMES = 8;

    /// 6502: LDA #7 / STA RLINE+2 and LDA #9 / STA RLINE+2 -- what GTNME lowers the limit to, and
    /// what it puts back.
    constexpr std::uint8_t NAME_MAX_LENGTH = 7;
    constexpr std::uint8_t LINE_MAX_LENGTH = 9;

    /// 6502: LDA #8 / JSR DETOK -- "{single cap}COMMANDER'S NAME? ".
    constexpr std::uint8_t NAME_PROMPT_TOKEN = 8;

    /// 6502: LDX #7 / GTL1 and GTL2 -- eight bytes, counted down from seven inclusive.
    constexpr std::size_t NAME_BYTES = COMMANDER_NAME_SIZE;
  } // namespace

  LineResult ReadLine(KeySource& _keys, TextSink& _screen, TextState& _text, LineEntryEffects& _effects, std::span<std::uint8_t> _buffer,
                      const LineLimits& _limits) noexcept
  {
    // 6502: LDA #MAG2 / STA COL2 -- purple for what the player types, as gnum does.
    _text.cellColour = TEXT_COLOUR_TYPING;

    // 6502: LDY #8 / JSR DELAY / JSR FLKB -- settle, then throw away anything already buffered.
    _effects.WaitFrames(SETTLE_FRAMES);
    _effects.FlushKeyboard();

    LineResult result{};

    // 6502: LDY #0 / OSW0L: JSR TT217. The loop has no counter -- it ends on a key, not on a count.
    for (;;)
    {
      const std::uint8_t key = _keys.NextKey();

      if (key == KEY_RETURN)
      {
        /*
         * 6502: OSW03 -- STA INWK+5,Y / LDA #&10 / STA COL2 / LDA #12 / JMP CHPR.
         *
         * The carriage return goes INTO the buffer before the routine leaves, which is what makes a
         * stored name eight bytes ending in 13 rather than a length and seven characters.
         */
        if (result.length < _buffer.size())
        {
          _buffer[result.length] = KEY_RETURN;
        }
        _text.cellColour = TEXT_COLOUR_NORMAL;
        _screen.Put(NEWLINE);
        return result;
      }

      if (key == KEY_ESCAPE)
      {
        // 6502: OSW04 -- LDA #&10 / STA COL2 / SEC / RTS. No newline, and no terminator written.
        _text.cellColour = TEXT_COLOUR_NORMAL;
        result.escaped = true;
        return result;
      }

      /*
       * 6502: the accept/reject decision, and both answers print.
       *
       * An accepted character falls past `LDA #7` through an `EQUB &2C` -- a BIT absolute opcode
       * that swallows the two bytes after it -- so there is ONE `JSR CHPR` and what reaches it is
       * either the key or the bell. `BCC OSW0L` then always branches, because CHPR returns with
       * the carry clear; it is a jump written as a conditional.
       */
      bool accepted = false;

      if (key == KEY_DELETE)
      {
        // 6502: OSW05 -- TYA / BEQ OSW01 / DEY / LDA #127 / BNE OSW06. Deleting on an empty line
        // beeps; otherwise the DELETE character itself is printed, which is what moves the cursor.
        if (result.length != 0)
        {
          --result.length;
          _screen.Put(KEY_DELETE);
          continue;
        }
      }
      else if (result.length < _limits.maxLength && key >= _limits.lowest && key < _limits.highest)
      {
        // 6502: CPY RLINE+2 / BCS, CMP RLINE+3 / BCC, CMP RLINE+4 / BCS -- full, too low, too high.
        // The last is `BCS`, so RLINE+4 itself is refused and the range excludes '{'.
        if (result.length < _buffer.size())
        {
          _buffer[result.length] = key;
        }
        ++result.length;
        accepted = true;
      }

      _screen.Put(accepted ? key : BELL);
    }
  }

  void StoreCommanderName(std::span<std::uint8_t> _buffer, std::span<std::uint8_t, COMMANDER_NAME_SIZE> _name) noexcept
  {
    // 6502: TRNME -- LDX #7 / GTL1: LDA INWK+5,X / STA NA%,X / DEX / BPL GTL1.
    for (std::size_t index = 0; index < NAME_BYTES; ++index)
    {
      _name[index] = (index < _buffer.size()) ? _buffer[index] : std::uint8_t{0};
    }

    /*
     * 6502: and then it FALLS INTO TR1, which copies the same eight bytes straight back.
     *
     * Redundant here and not a mistake: TR1 is GTNME's "nothing was typed" path and TRNME simply
     * sits above it in the same block of bytes. Reproduced because a caller of TRNME gets both
     * loops whether it wants them or not, and because leaving it out would make the two routines
     * look independent when they are not.
     *
     * REMOVING IT CANNOT BE DETECTED, and provably: the loop above has just written the buffer's
     * eight bytes into the name, so this writes the same eight bytes back over the buffer they came
     * from. A mutation that drops the call survives every test and always will. That is an
     * equivalent mutation rather than a gap -- the third this port has kept and labelled, after
     * gnum's two dead carries and the interpreter's page-crossing flag -- and the reason to keep it
     * is that the equivalence is a property of TRNME's callers, not of TR1, and a future caller
     * that filled the buffer differently would break it.
     */
    LoadCommanderName(_name, _buffer);
  }

  void LoadCommanderName(std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name, std::span<std::uint8_t> _buffer) noexcept
  {
    // 6502: TR1 -- LDX #7 / GTL2: LDA NA%,X / STA INWK+5,X / DEX / BPL GTL2 / RTS.
    for (std::size_t index = 0; index < NAME_BYTES && index < _buffer.size(); ++index)
    {
      _buffer[index] = _name[index];
    }
  }

  LineResult AskCommanderName(KeySource& _keys, TextSink& _screen, TextState& _text, ExtendedTokenPrinter& _extended,
                              LineEntryEffects& _effects, std::span<std::uint8_t> _buffer,
                              std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name, LineLimits& _limits) noexcept
  {
    /*
     * 6502: LDX #4 / GTL3: LDA NA%-5,X / STA INWK,X / DEX / BPL GTL3.
     *
     * The five bytes before the name are the drive and directory part of the filename, and the
     * whole of INWK becomes what the Kernal is handed. That is file-system state, it belongs with
     * SaveStore in the executable, and the port's name has nothing in front of it -- so this copy
     * has no counterpart here and is deliberately absent rather than forgotten.
     */

    // 6502: LDA #7 / STA RLINE+2 -- the name is shorter than the line the buffer can hold.
    _limits.maxLength = NAME_MAX_LENGTH;

    // 6502: LDA #8 / JSR DETOK.
    _extended.Print(NAME_PROMPT_TOKEN);

    const LineResult result = ReadLine(_keys, _screen, _text, _effects, _buffer, _limits);

    // 6502: LDA #9 / STA RLINE+2 -- and it is restored whether a name was typed or not.
    _limits.maxLength = LINE_MAX_LENGTH;

    /*
     * 6502: TYA / BEQ TR1 / STY thislong / RTS.
     *
     * Nothing typed means the existing name is copied back and kept. `thislong` records the length
     * for TRNME to copy into `oldlong`, which the original's own comment says is never read.
     */
    if (result.length == 0)
    {
      LoadCommanderName(_name, _buffer);
    }

    return result;
  }

} // namespace Elite
