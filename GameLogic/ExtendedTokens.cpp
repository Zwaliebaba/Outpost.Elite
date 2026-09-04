#include "pch.h"

#include "ExtendedTokens.h"

#include "EliteConfig.h"
#include "LookupTables.h"

#include <algorithm>

/*
 * The extended token system (slices 1c-b and 1c-c-b).
 *
 * A byte inside a token's text means one of five things, and the ranges are the game's:
 *
 *     0-31      a control code: move the cursor, clear the screen, change the subject
 *     32-90     a character
 *     91-128    one of up to five randomised alternatives
 *     129-214   another extended token, expanded in place
 *     215-255   a pair of letters
 *
 * The randomised range is what makes the game's system descriptions read differently on each
 * visit, and it is why this printer needs the random generator: the choice comes out of the
 * same sequence everything else does, so a description is reproducible from the seed rather
 * than merely varied.
 *
 * The control codes are the other half of the system and are the reason it is an interpreter
 * rather than a decompressor. Fifteen of the twenty-one are pure text: they set the case, empty
 * the justification buffer, print a fixed character, or splice in the name of the system you
 * are looking at. Three reach the screen and are left to a seam. The three that remain are
 * TT27, twice, and the buffer flush.
 */

namespace Elite
{

  void StopJustifying(ExtendedTextState& _state) noexcept
  {
    // 6502: MT15 -- LDA #0 / STA DTW4 / ASL A / STA DTW5.
    _state.justify = 0;
    _state.bufferLength = 0;
  }

  namespace
  {
    constexpr std::uint8_t FIRST_CHARACTER = 32;
    constexpr std::uint8_t FIRST_VARIANT = 91;
    constexpr std::uint8_t FIRST_NESTED = 129;
    constexpr std::uint8_t FIRST_PAIR = 215;
    constexpr std::uint8_t UPPER_A = 'A';
    constexpr std::uint8_t SPACE = ' ';

    /// The four thresholds that turn a random byte into one of five alternatives.
    constexpr std::uint8_t VARIANT_THRESHOLDS[4] = {51, 102, 153, 204};

    /// 6502: VOWEL -- ORA #%00100000 folds the case, then five comparisons. The carry is set on a
    /// match and cleared by the CLC that the fall-through reaches, so "carry set" means vowel.
    [[nodiscard]] bool IsVowel(std::uint8_t _character) noexcept
    {
      const std::uint8_t lower = static_cast<std::uint8_t>(_character | 0x20u);
      return lower == 'a' || lower == 'e' || lower == 'i' || lower == 'o' || lower == 'u';
    }

    /// 6502: MT18's LDA TKN2+2,X and TKN2+3,X -- the letter-pair table, two bytes in.
    constexpr std::size_t RANDOM_WORD_OFFSET = 2;
  } // namespace

  // ======================================================================================
  // DASC -- where every printed character in the game goes
  // ======================================================================================

  void CharacterPrinter::Put(std::uint8_t _character) noexcept
  {
    // 6502: LDX #255 / STX DTW8. The case mask lasts exactly one character: MT19 sets it just
    // before the letter it applies to, and this is what takes it away again.
    state.caseMask = 0xFF;

    /*
     * 6502: the CMP ladder into DA8. X arrives at the store as 255 when one of the five matched
     * and 0 when none did, so DTW2 records "the character just printed ended a word". The next
     * letter through DTS is left capitalised on the strength of it, which is how the game gets
     * sentence case out of one byte and no lookahead.
     */
    state.sentenceStart = (_character == '.' || _character == ':' || _character == 10 || _character == FORM_FEED || _character == SPACE)
                            ? std::uint8_t{0xFF}
                            : std::uint8_t{0x00};

    // 6502: BIT DTW4 / BPL -- nobody asked for justification, so this goes straight to the screen.
    if ((state.justify & 0x80u) == 0u)
    {
      m_screen.Put(_character);
      return;
    }

    /*
     * 6502: BIT DTW4 / BVS -- bit 6 suppresses the flush entirely.
     *
     * Only the in-flight message printer sets it. That routine buffers a whole message so it can
     * measure it and centre it by hand, and a form feed inside such a message is a character to
     * be kept rather than an instruction to obey.
     */
    if ((state.justify & 0x40u) == 0u && _character == FORM_FEED)
    {
      Justify();
      return;
    }

    // 6502: LDX DTW5 / STA BUF,X / INC DTW5.
    if (state.bufferLength < buffer.size())
    {
      buffer[state.bufferLength] = _character;
      ++state.bufferLength;
    }
    // Past the buffer the original writes on into the recursive token table, which would be a
    // real defect rather than the benign spill into the ship tables below it. Nothing in the game
    // reaches that far; the port drops the character and does not count it, rather than
    // reproducing the corruption and then reading it back.
  }

  void CharacterPrinter::Emit(std::uint8_t _count) noexcept
  {
    // 6502: DAS1 -- LDY #0 / LDA BUF,Y / JSR CHPR / INY / DEX / BNE DAL5.
    const std::size_t count = std::min<std::size_t>(_count, buffer.size());
    for (std::size_t index = 0; index < count; ++index)
    {
      m_screen.Put(buffer[index]);
    }
  }

  bool CharacterPrinter::PadToWidth(std::uint8_t& _rotor) noexcept
  {
    int scan = 0;
    bool restart = true;
    bool firstPass = true;
    bool sawGap = false;

    for (;;)
    {
      if (restart)
      {
        /*
         * 6502: DA11 -- the rotating bit is reseeded only when it has run out of places to go,
         * and that is deliberate. It carries across passes, so a line whose only gap was skipped
         * on one pass has that gap widened on the next.
         *
         * A pass that saw no gap at all is a different matter: the original loops here forever,
         * because it is waiting for a space that a line of thirty unbroken characters will never
         * produce. The port gives up and lets the caller break the line where it stands. No token
         * in the game reaches this, and a hang is not a behaviour worth reproducing.
         */
        if (!sawGap && !firstPass)
        {
          return false;
        }

        if ((_rotor & 0x80u) == 0u)
        {
          _rotor = 0x40;
        }
        scan = LINE_WIDTH - 1; // 6502: LDY #29
        restart = false;
        firstPass = false;
        sawGap = false;
      }

      // 6502: DAL1 -- the line is ready the moment the thirty-first character is a space.
      if (buffer[LINE_WIDTH] == SPACE)
      {
        return true;
      }

      // 6502: DAL2 -- back through the line looking for a gap this pass is allowed to widen.
      bool widen = false;
      for (;;)
      {
        --scan;
        if (scan <= 0)
        {
          // 6502: BMI DA11 / BEQ DA11 -- off the front of the line, so round again.
          restart = true;
          break;
        }

        if (buffer[scan] != SPACE)
        {
          continue;
        }

        sawGap = true;

        // 6502: ASL SCH / BMI DAL2 -- every other gap is passed over, so the padding spreads
        // along the line instead of piling up in the first space it finds.
        _rotor = static_cast<std::uint8_t>(_rotor << 1);
        if ((_rotor & 0x80u) != 0u)
        {
          continue;
        }

        widen = true;
        break;
      }

      if (!widen)
      {
        continue;
      }

      if (static_cast<std::size_t>(state.bufferLength) >= buffer.size())
      {
        return false;
      }

      /*
       * 6502: STY SC / LDY DTW5 / DAL6 -- shift the tail right by one to open the gap.
       *
       * The loop starts one PAST the last character, so the original moves a byte of whatever
       * follows the text along with it. Nothing ever prints that byte: DTW5 counts the text and
       * the copy lands beyond it.
       */
      const int gap = scan;
      for (int index = static_cast<int>(state.bufferLength); index >= gap; --index)
      {
        if (static_cast<std::size_t>(index) + 1 < buffer.size())
        {
          buffer[static_cast<std::size_t>(index) + 1] = buffer[static_cast<std::size_t>(index)];
        }
      }
      ++state.bufferLength;

      /*
       * 6502: DAL3 -- step back over the whole run of spaces this gap belongs to. A still holds
       * the space that was moved, which is what the CMP compares against, and the BNE returns to
       * DAL1 WITHOUT resetting Y: the scan carries on from where it is rather than starting over.
       */
      scan = gap - 1;
      while (scan >= 0 && buffer[static_cast<std::size_t>(scan)] == SPACE)
      {
        --scan;
      }
      if (scan < 0)
      {
        restart = true;
      }
    }
  }

  void CharacterPrinter::Justify() noexcept
  {
    /*
     * 6502: DA5 / LSR SCH.
     *
     * SCH is the screen pointer's high byte, borrowed here as the rotating bit that decides which
     * gap to widen. The LSR is dead: it always clears bit 7, so DA11 always reseeds the bit at
     * the start of a line. Which means the justification does not depend on what the last drawing
     * routine left in SCH -- a port that carefully threaded that value in would be threading
     * noise, and would be harder to test for it.
     */
    std::uint8_t rotor = 0;

    while (state.bufferLength != 0)
    {
      if (state.bufferLength <= LINE_WIDTH)
      {
        // 6502: DA6 -- CPX #31 / BCC. Short enough to print as it stands.
        Emit(state.bufferLength);
        state.bufferLength = 0;
        break;
      }

      rotor = static_cast<std::uint8_t>(rotor >> 1);

      const bool broke = PadToWidth(rotor);

      // 6502: DA2 -- thirty characters and a newline. The space that broke the line goes with
      // them, which is why the subtraction below takes thirty-ONE away: CHPR returns with the
      // carry clear, so the SBC #30 that follows it borrows.
      Emit(LINE_WIDTH);
      m_screen.Put(FORM_FEED);

      if (!broke)
      {
        state.bufferLength = 0;
        break;
      }

      state.bufferLength = static_cast<std::uint8_t>(state.bufferLength - LINE_WIDTH - 1);
      if (state.bufferLength == 0)
      {
        break;
      }

      /*
       * 6502: DAL4 -- move what is left down to the front. X is one more than the count, so the
       * original copies one byte more than there is text; DTW5 stops it ever being printed.
       */
      for (std::size_t index = 0; index <= static_cast<std::size_t>(state.bufferLength); ++index)
      {
        const std::size_t source = index + LINE_WIDTH + 1u;
        if (index >= buffer.size())
        {
          break;
        }
        buffer[index] = (source < buffer.size()) ? buffer[source] : std::uint8_t{0};
      }
    }

    // 6502: DA7 -- however it got here, a flush ends on a newline.
    m_screen.Put(FORM_FEED);
  }

  // ======================================================================================
  // DETOK -- the token walker
  // ======================================================================================

  void ExtendedTokenPrinter::Print(std::uint8_t _token) noexcept
  {
    Walk(EXTENDED_TOKEN_TABLE.data(), EXTENDED_TOKEN_TABLE.size(), _token);
  }

  void ExtendedTokenPrinter::PrintSystemOverride(std::uint8_t _token) noexcept
  {
    Walk(SYSTEM_TOKEN_TABLE.data(), SYSTEM_TOKEN_TABLE.size(), _token);
  }

  void ExtendedTokenPrinter::Walk(const std::uint8_t* _table, std::size_t _size, std::uint8_t _token) noexcept
  {
    // Find the token by counting terminators, exactly as the recursive table is walked. The
    // difference is the key, and that the text may run past what a single byte could index.
    std::size_t offset = 0;
    std::uint8_t remaining = _token;

    while (remaining != 0 && offset < _size)
    {
      const std::uint8_t decoded = static_cast<std::uint8_t>(_table[offset] ^ EXTENDED_TOKEN_KEY);
      if (decoded == 0)
      {
        --remaining;
      }
      ++offset;
    }

    if (remaining != 0)
    {
      return;
    }

    // Unlike the recursive table this checks before printing, so an empty token prints nothing.
    while (offset < _size)
    {
      const std::uint8_t decoded = static_cast<std::uint8_t>(_table[offset] ^ EXTENDED_TOKEN_KEY);
      if (decoded == 0)
      {
        return;
      }

      PrintByte(decoded);
      ++offset;
    }
  }

  void ExtendedTokenPrinter::PrintByte(std::uint8_t _byte) noexcept
  {
    if (_byte < FIRST_CHARACTER)
    {
      // 6502: DT3 -- a control code, dispatched through the JMTB jump table.
      RunControlCode(_byte);
      return;
    }

    if ((m_characters.state.toLineBuffer & 0x80u) != 0u)
    {
      /*
       * 6502: BIT DTW3 / BPL DT8.
       *
       * Everything above 31 goes to the RECURSIVE printer instead -- not just characters, but the
       * variants, the nested tokens and the letter pairs as well, because the test is on the byte
       * before any of them have been recognised. Control code 6 turns this on and code 5 turns it
       * off, and between them a stretch of extended text is read by the other system entirely.
       */
      m_recursive.Print(_byte);
      return;
    }

    if (_byte < FIRST_VARIANT)
    {
      PrintCharacter(_byte);
      return;
    }

    if (_byte < FIRST_NESTED)
    {
      PrintRandomVariant(_byte);
      return;
    }

    if (_byte < FIRST_PAIR)
    {
      Print(_byte);
      return;
    }

    // 6502: the TKN2 path -- a pair of letters from the top of the range.
    const std::size_t index = static_cast<std::size_t>(_byte - FIRST_PAIR) * 2u;
    if (index + 1 >= EXTENDED_PAIR_TABLE.size())
    {
      return;
    }

    PrintCharacter(EXTENDED_PAIR_TABLE[index]);
    PrintCharacter(EXTENDED_PAIR_TABLE[index + 1]);
  }

  void ExtendedTokenPrinter::PrintCharacter(std::uint8_t _character) noexcept
  {
    ExtendedTextState& state = m_characters.state;

    if (_character >= UPPER_A)
    {
      // Lower case applies always, or while a word is already under way. The one case that skips
      // it is a word that has just started with nothing forcing lower case.
      const bool forceLower = (state.alwaysLower & 0x80u) != 0u;
      const bool startOfWord = (state.sentenceStart & 0x80u) != 0u;

      if (forceLower || !startOfWord)
      {
        _character = static_cast<std::uint8_t>(_character | state.lowerCaseBits);
      }

      _character = static_cast<std::uint8_t>(_character & state.caseMask);
    }

    // 6502: DT9 / JMP DASC -- digits, spaces and punctuation skip the folding and arrive here
    // anyway, which matters: DASC is what notices that a full stop ended a sentence.
    m_characters.Put(_character);
  }

  void ExtendedTokenPrinter::PrintRandomVariant(std::uint8_t _byte) noexcept
  {
    // 6502: DT6 is reached by a BCC, so the carry is clear on the way into DORND -- deterministic
    // rather than incidental, which is why this can use the repeatable entry point.
    const std::uint8_t roll = m_rng.NextRepeatable().value;

    // How many thresholds the roll cleared decides which alternative is used. The last threshold
    // is folded into the addition rather than counted separately, which is an instruction saved
    // in the original and a footnote here.
    std::uint8_t choice = 0;
    for (std::size_t index = 0; index < 3; ++index)
    {
      if (roll >= VARIANT_THRESHOLDS[index])
      {
        ++choice;
      }
    }

    const std::size_t baseIndex = static_cast<std::size_t>(_byte) - FIRST_VARIANT;
    if (baseIndex >= VARIANT_BASE_TABLE.size())
    {
      return;
    }

    const bool clearedLast = roll >= VARIANT_THRESHOLDS[3];
    const std::uint8_t token = static_cast<std::uint8_t>(choice + VARIANT_BASE_TABLE[baseIndex] + (clearedLast ? 1u : 0u));

    Print(token);
  }

  // ======================================================================================
  // The control codes
  // ======================================================================================

  void ExtendedTokenPrinter::RunControlCode(std::uint8_t _code) noexcept
  {
    ExtendedTextState& state = m_characters.state;

    switch (_code)
    {
    case 1:
      // 6502: MT1 -- upper case, and stop forcing it.
      state.lowerCaseBits = 0;
      state.alwaysLower = 0;
      return;

    case 2:
      // 6502: MT2 -- sentence case. The two routines share their tail through a BIT that
      // swallows the instruction between them.
      state.lowerCaseBits = 0x20;
      state.alwaysLower = 0;
      return;

    case 3:
    case 4:
      /*
       * 6502: JMTB sends both of these to TT27 with the code itself in A. Token 3 is the name
       * of the system you are looking at and token 4 the one you are docked at, and both are
       * value tokens -- so they arrive here and leave through whatever the recursive printer
       * was given for them.
       */
      m_recursive.Print(_code);
      return;

    case 5:
      // 6502: MT5 -- stop routing extended text through the recursive printer.
      state.toLineBuffer = 0;
      return;

    case 6:
      // 6502: MT6 -- start routing it there, in sentence case.
      m_recursive.SetCaseFlags(0x80);
      state.toLineBuffer = 0xFF;
      return;

    case 7:
    case 10:
    case 12:
    case 20:
      // 6502: JMTB sends these four straight to DASC with the code in A. Seven is the bell,
      // ten and twenty are line movements, and twelve is the newline that also empties the
      // justification buffer.
      m_characters.Put(_code);
      return;

    case 8:
      // 6502: MT8 -- LDA #6 / JSR DOXC, then DTW2. The column belongs to the canvas and goes to
      // the seam; the flag belongs here.
      if (m_controls != nullptr)
      {
        m_controls->Run(_code);
      }
      state.sentenceStart = 0xFF;
      return;

    case 13:
      // 6502: MT13 -- lower case, forced.
      state.alwaysLower = 0x80;
      state.lowerCaseBits = 0x20;
      return;

    case 14:
      // 6502: MT14 -- justify from here on. The ASL that clears DTW5 is the same instruction
      // MT15 uses, which is why the two routines overlap in the binary.
      state.justify = 0x80;
      state.bufferLength = 0;
      return;

    case 15:
      // 6502: MT15 -- stop justifying, and throw away anything buffered. The same two stores the
      // free function below makes, because `MESS` reaches them by `JSR` rather than by token.
      StopJustifying(state);
      return;

    case 16:
      // 6502: MT16 -- print DTW7, which is the operand byte of this routine's own LDA. It goes
      // to DASC rather than DTS, so no case folding is applied to it.
      m_characters.Put(state.literal);
      return;

    case 17:
      PrintSystemAdjective();
      return;

    case 18:
      PrintRandomWord();
      return;

    case 19:
      // 6502: MT19 -- clear bit 5 of the next letter, which forces it to upper case. DASC puts
      // the mask back to 255 immediately afterwards, so this reaches exactly one character.
      state.caseMask = 0xDF;
      return;

    case 21:
      // 6502: CLYNS -- clears the bottom of the screen and puts the cursor there. The two flags
      // are text state and are set here; the rest is the seam's.
      state.sentenceStart = 0xFF;
      m_recursive.SetCaseFlags(0x80);
      if (m_controls != nullptr)
      {
        m_controls->Run(_code);
      }
      return;

    case 23:
    case 29:
      /*
       * 6502: MT23 and MT29 -- move to row 10 or row 6, switch to white text, then fall into
       * MT13. They are one routine with two entry points, and the entry that sets the row is
       * skipped by a BIT, the same trick MT1 and MT2 use.
       *
       * WHITETEXT between them is an RTS in this version; the C64's text is one colour. So all
       * that is left besides the cursor move is MT13's pair of stores, which land here.
       */
      if (m_controls != nullptr)
      {
        m_controls->Run(_code);
      }
      state.alwaysLower = 0x80;
      state.lowerCaseBits = 0x20;
      return;

    default:
      /*
       * The eight codes that leave the text system entirely, and every one of them is reached
       * by a token the game actually prints:
       *
       *     9        MT9    -- clears to a new view
       *     11       NLIN4  -- draws a rule across the screen
       *     22, 24   PAUSE, PAUSE2 -- spin the ship and wait for a key
       *     25       BRIS   -- a token and a hundred-frame delay
       *     26       MT26   -- read a line of text from the keyboard
       *     27, 28   MT27, MT28 -- the mission captain's and planet's names, keyed on GCNT
       *     30, 31   FILEPR, OTHERFILEPR -- disk names, keyed on DISK
       *
       * The last four are tokens under a game-state index rather than screen work, so they will
       * land with the state that indexes them rather than with the canvas.
       *
       * Code 0 is the one entry that means nothing: JMTB is indexed from one, so code 0 reads
       * the two bytes before the table and jumps to $6060. No token can contain it either --
       * zero is what terminates one.
       */
      if (m_controls != nullptr)
      {
        m_controls->Run(_code);
      }
      return;
    }
  }

  void ExtendedTokenPrinter::PrintSystemAdjective() noexcept
  {
    ExtendedTextState& state = m_characters.state;

    // 6502: MT17 -- LDA QQ17 / AND #%10111111. Clearing the "first letter seen" bit makes the
    // recursive printer capitalise the name again.
    m_recursive.SetCaseFlags(static_cast<std::uint8_t>(m_recursive.CaseFlags() & 0xBFu));

    // 6502: LDA #3 / JSR TT27 -- the name of the system you are looking at.
    m_recursive.Print(3);

    /*
     * 6502: LDX DTW5 / LDA BUF-1,X / JSR VOWEL / BCC MT171 / DEC DTW5.
     *
     * The name has just gone into the justification buffer, so this reaches back into it: if the
     * name ended on a vowel that vowel is dropped, and then "IAN" is added. LAVE becomes LAVIAN,
     * TIBEDIED becomes TIBEDIEDIAN.
     *
     * It only works because justification is on. With DTW4 clear the name went to the screen and
     * DTW5 is stale, so the game is careful to send control code 14 before this one -- and the
     * dependency is invisible from the routine itself, which reads a buffer that a different
     * control code decided to fill.
     */
    if (state.bufferLength > 0 && static_cast<std::size_t>(state.bufferLength) <= m_characters.buffer.size())
    {
      if (IsVowel(m_characters.buffer[state.bufferLength - 1u]))
      {
        --state.bufferLength;
      }
    }

    // 6502: LDA #&99 / JMP DETOK -- the ending, which is a token of its own.
    Print(0x99);
  }

  void ExtendedTokenPrinter::PrintRandomWord() noexcept
  {
    // 6502: JSR MT19 -- upper case for one letter, which is the word's initial.
    m_characters.state.caseMask = 0xDF;

    /*
     * 6502: JSR DORND / AND #3 / TAY, then the loop.
     *
     * The carry matters here in a way it does not in DT6. DT3 reaches this routine through an
     * LSR of an even index, so the first DORND is called with the carry clear; the second is
     * called with whatever the first left. After that every call is preceded by two trips
     * through DASC, which returns with CLC on all three of its paths -- so the chain settles
     * clear and only the first pair of draws is coupled.
     */
    bool carry = false;

    RngResult roll = m_rng.Next(carry);
    carry = roll.carry;

    const int pairs = roll.value & 0x03;

    for (int index = 0; index <= pairs; ++index)
    {
      roll = m_rng.Next(carry);

      // 6502: AND #%00111110 / TAX / LDA TKN2+2,X / JSR DTS / LDA TKN2+3,X / JSR DTS. The mask
      // keeps the index even, so a pair is never read across a boundary.
      const std::size_t pair = static_cast<std::size_t>(roll.value & 0x3Eu) + RANDOM_WORD_OFFSET;
      if (pair + 1 < EXTENDED_PAIR_TABLE.size())
      {
        PrintCharacter(EXTENDED_PAIR_TABLE[pair]);
        PrintCharacter(EXTENDED_PAIR_TABLE[pair + 1]);
      }

      carry = false;
    }
  }

} // namespace Elite
