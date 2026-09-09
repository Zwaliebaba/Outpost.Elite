#include "pch.h"

#include "NameEntry.h"

#include "Controls.h"
#include "Frame.h"

/*
 * The line editor and the commander's name (slice 2d).
 */

namespace Elite
{

  namespace
  {
    /// The three keys that are not text.
    constexpr std::uint8_t KEY_RETURN = 13;
    constexpr std::uint8_t KEY_ESCAPE = 27;
    constexpr std::uint8_t KEY_DELETE = 127;

    /// The bell, printed in place of a character the line will not take.
    constexpr std::uint8_t BELL = 7;

    /// The newline OSW03 ends on.
    constexpr std::uint8_t NEWLINE = 12;

    /// The frames `GTNME` waits before it reads a key.
    constexpr std::uint8_t SETTLE_FRAMES = 8;

    /// What GTNME lowers the line limit to, and what it puts back.
    constexpr std::uint8_t NAME_MAX_LENGTH = 7;
    constexpr std::uint8_t LINE_MAX_LENGTH = 9;

    /// The extended token "{single cap}COMMANDER'S NAME? ".
    constexpr std::uint8_t NAME_PROMPT_TOKEN = 8;

    /// GTL1 and GTL2 -- eight bytes, counted down from seven inclusive.
    constexpr std::size_t NAME_BYTES = COMMANDER_NAME_SIZE;
  } // namespace

  LineResult ReadLine(Keyboard& _keys, TextSink& _screen, TextState& _text, Presenter& _present, std::span<std::uint8_t> _buffer,
                      const LineLimits& _limits) noexcept
  {
    // Purple for what the player types, as gnum does.
    _text.palette = TEXT_COLOUR_PURPLE; // TextPrint.h's, not a second copy (slice 5a-8)

    // Settle for eight frames, then throw away anything already buffered.
    WaitFrames(_present, nullptr, SETTLE_FRAMES); // a docked prompt: nothing of this is on the frame
    _keys.Flush();

    LineResult result{};

    // The loop has no counter; it ends on a key, not on a count.
    for (;;)
    {
      const std::uint8_t key = _keys.NextKey();

      if (key == KEY_RETURN)
      {
        /*
         * The key stored, the colour back to white, and a newline.
         *
         * The carriage return goes INTO the buffer before the routine leaves, which is what makes a
         * stored name eight bytes ending in 13 rather than a length and seven characters.
         */
        if (result.length < _buffer.size())
        {
          _buffer[result.length] = KEY_RETURN;
        }
        _text.palette = TEXT_COLOUR_WHITE;
        _screen.Put(NEWLINE);
        return result;
      }

      if (key == KEY_ESCAPE)
      {
        // The colour back, and the carry SET. No newline, and no terminator
        // written.
        _text.palette = TEXT_COLOUR_WHITE;
        result.escaped = true;
        return result;
      }

      /*
       * The accept/reject decision, and both answers print.
       *
       * An accepted character falls past the bell's load through an `EQUB &2C` -- a BIT absolute
       * opcode that swallows the two bytes after it -- so there is ONE call to `CHPR` and what
       * reaches it is either the key or the bell. The branch back to the loop then always branches,
       * because `CHPR` returns with the carry clear; it is a jump written as a conditional.
       */
      bool accepted = false;

      if (key == KEY_DELETE)
      {
        // Deleting on an empty line beeps; otherwise the DELETE character itself is
        // printed, which is what moves the cursor.
        if (result.length != 0)
        {
          --result.length;
          _screen.Put(KEY_DELETE);
          continue;
        }
      }
      else if (result.length < _limits.maxLength && key >= _limits.lowest && key < _limits.highest)
      {
        // Three comparisons against `RLINE` -- full, too low, too high. The last is a
        // carry-set branch, so `RLINE+4` itself is refused and the range excludes '{'.
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
    // GTL1 copies eight bytes from the line buffer into the name.
    for (std::size_t index = 0; index < NAME_BYTES; ++index)
    {
      _name[index] = (index < _buffer.size()) ? _buffer[index] : std::uint8_t{0};
    }

    /*
     * And then it FALLS INTO TR1, which copies the same eight bytes straight back.
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
    // GTL2 copies the same eight bytes the other way.
    for (std::size_t index = 0; index < NAME_BYTES && index < _buffer.size(); ++index)
    {
      _buffer[index] = _name[index];
    }
  }

  LineResult AskCommanderName(Keyboard& _keys, TextSink& _screen, TextState& _text, ExtendedTokenPrinter& _extended, Presenter& _present,
                              std::span<std::uint8_t> _buffer, std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name,
                              LineLimits& _limits) noexcept
  {
    /*
     * Five bytes from in front of the name into the front of `INWK`.
     *
     * The five bytes before the name are the drive and directory part of the filename, and the
     * whole of INWK becomes what the Kernal is handed. That is file-system state, it belongs with
     * SaveStore in the executable, and the port's name has nothing in front of it -- so this copy
     * has no counterpart here and is deliberately absent rather than forgotten.
     */

    // The name is shorter than the line the buffer can hold.
    _limits.maxLength = NAME_MAX_LENGTH;

    // The prompt.
    _extended.Print(NAME_PROMPT_TOKEN);

    const LineResult result = ReadLine(_keys, _screen, _text, _present, _buffer, _limits);

    // And the limit is restored whether a name was typed or not.
    _limits.maxLength = LINE_MAX_LENGTH;

    /*
     * A length of zero goes to `TR1`; otherwise the length is recorded in `thislong`.
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
