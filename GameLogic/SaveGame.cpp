#include "pch.h"

#include "SaveGame.h"

#include "EliteTypes.h"
#include "LookupTables.h"

/*
 * Saving and loading a commander (slice 2d).
 */

namespace Elite
{

  namespace
  {
    /// 6502: ORA #%10000000 -- bit 7 forced on, so the first byte is never small.
    constexpr std::uint8_t COMPETITION_HIGH_BIT = 0x80;

    /// 6502: EOR #&5A and EOR #&A9. Two constants with no meaning beyond being hard to guess.
    constexpr std::uint8_t COMPETITION_MIX = 0x5A;
    constexpr std::uint8_t CHECKSUM_STAMP = 0xA9;

    /// 6502: CMP #'Y' / CMP #'N'.
    constexpr std::uint8_t KEY_YES = 'Y';
    constexpr std::uint8_t KEY_NO = 'N';

    /// 6502: NA2%+8 -- the block begins after the eight bytes of name, in the file and in NA2%.
    constexpr std::size_t BLOCK_IN_FILE = COMMANDER_NAME_SIZE;

    /*
     * 6502: the extended tokens SVE and its error paths print.
     *
     *   1    the menu itself
     *   4    "COMPETITION NUMBER:", the heading over the number a save prints
     *   9    ELT2F -- "ILLEGAL ELITE II FILE"
     *   224  "ARE YOU SURE?", before the default commander is put back
     *   255  tapeerror -- the media's name and the word "ERROR"
     */
    constexpr std::uint8_t MENU_TOKEN = 1;
    constexpr std::uint8_t SAVE_TOKEN = 4;
    constexpr std::uint8_t BAD_FILE_TOKEN = 9;
    constexpr std::uint8_t CONFIRM_TOKEN = 224;
    constexpr std::uint8_t DEVICE_ERROR_TOKEN = 255;

    /*
     * 6502: LOD's `LDA TAP% / BMI ELT2F`.
     *
     * The whole test for "is this a commander file". Bit 7 of the first byte of the block is bit 7
     * of TP, the mission flags, and no commander the game writes has it set -- so a file from some
     * other program is caught only if its first byte happens to be negative. The checksums that
     * would catch the rest are DFAULT's, and DFAULT hangs rather than reporting.
     */
    constexpr std::uint8_t NOT_A_COMMANDER = 0x80;

    /*
     * 6502: tapeerror and ELT2F -- print a token, wait for a key, and jump back to SVE.
     *
     * Two labels with one body between them, which is why the port has one function: the only thing
     * that differs is the token, and both end at the same `JMP SVE`.
     */
    void ReportAndReturnToMenu(SaveScreen& _screen, std::uint8_t _token) noexcept
    {
      _screen.extended.Print(_token);
      (void)_screen.keys.NextKey();
    }
  } // namespace

  CompetitionNumber MakeCompetitionNumber(const CommanderBlock& _image) noexcept
  {
    /*
     * 6502: PHA / ORA #%10000000 / STA K / EOR COK / STA K+2 / EOR CASH+2 / STA K+1 /
     *       EOR #&5A / EOR TALLY+1 / STA K+3, then later PLA / EOR #&A9 / STA CHK2.
     *
     * ONE ACCUMULATOR, four stores, and the stores are out of order. Each EOR builds on what the
     * last one left, so the four bytes are a chain rather than four independent expressions -- and
     * they are written to K, K+2, K+1, K+3, which is what stops the printed number reading as the
     * inputs in sequence.
     *
     * `CASH+2` is the THIRD of the four cash bytes, and cash is stored most significant first
     * (§6.14), so it is the second-least significant -- tens of credits. A player who spent a
     * little between saves gets a visibly different competition number; one who spent nothing gets
     * the same one.
     */
    const std::uint8_t checksum = _image.At(Field::ChecksumByte);

    CompetitionNumber result{};

    std::uint8_t accumulator = static_cast<std::uint8_t>(checksum | COMPETITION_HIGH_BIT);
    result.value[0] = accumulator;

    accumulator = static_cast<std::uint8_t>(accumulator ^ _image.At(Field::Competition));
    result.value[2] = accumulator;

    // 6502: EOR CASH+2 -- the third byte of the four, counting from the most significant.
    accumulator = static_cast<std::uint8_t>(accumulator ^ _image.bytes[static_cast<std::size_t>(Field::Cash) + 2u]);
    result.value[1] = accumulator;

    accumulator = static_cast<std::uint8_t>(accumulator ^ COMPETITION_MIX);
    accumulator = static_cast<std::uint8_t>(accumulator ^ _image.bytes[static_cast<std::size_t>(Field::Kills) + 1u]);
    result.value[3] = accumulator;

    // 6502: PLA / EOR #&A9 / STA CHK2 -- the checksum as it was BEFORE the chain above touched it.
    result.checksum2 = static_cast<std::uint8_t>(checksum ^ CHECKSUM_STAMP);

    return result;
  }

  bool AskYesNo(KeySource& _keys) noexcept
  {
    // 6502: YESNO -- JSR t / CMP #'Y' / BEQ PL6 / CMP #'N' / BNE YESNO / CLC / RTS.
    for (;;)
    {
      const std::uint8_t key = _keys.NextKey();
      if (key == KEY_YES)
      {
        return true;
      }
      if (key == KEY_NO)
      {
        return false;
      }
    }
  }

  void ResetToDefaultCommander(std::span<std::uint8_t, COMMANDER_FILE_SIZE> _outFile) noexcept
  {
    /*
     * 6502: JAMESON -- LDY #(NAEND%-NA2%) / JAMEL1: LDA NA2%,Y / STA NA%,Y / DEY / BPL JAMEL1,
     * then LDY #7 / STY oldlong.
     *
     * It writes over NA%, the SAVE IMAGE, and not over the commander at TP -- so the reset does
     * nothing until something loads that image. SVE's option 4 does exactly that: JAMESON, then a
     * jump to DFAULT.
     */
    for (std::size_t index = 0; index < COMMANDER_FILE_SIZE; ++index)
    {
      _outFile[index] = DEFAULT_COMMANDER[index];
    }
  }

  SaveOutcome SaveCommanderTo(CommanderStore& _store, CommanderBlock& _block,
                              std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name) noexcept
  {
    /*
     * 6502: LSR SVC -- and this HALVES the save count rather than incrementing it.
     *
     * So it decays towards zero and a commander saved ten times looks like one saved once. It is
     * the live commander that changes here, not the file image, which is why this takes the block
     * by reference where SaveCommander takes it by const.
     */
    _block.At(Field::SaveCount) = static_cast<std::uint8_t>(_block.At(Field::SaveCount) >> 1);

    std::array<std::uint8_t, COMMANDER_FILE_SIZE> file{};
    SaveCommander(_block, _name, file);

    SaveOutcome outcome{};

    // The competition number reads the file's checksums, so it is worked out from the image rather
    // than from the block -- the same distinction SaveCommander's header makes.
    CommanderBlock image;
    for (std::size_t index = 0; index < COMMANDER_BLOCK_SIZE; ++index)
    {
      image.bytes[index] = file[BLOCK_IN_FILE + index];
    }
    outcome.competition = MakeCompetitionNumber(image);

    // 6502: NA% -- kept because DFAULT reads it back, not because the write needs it.
    outcome.image = file;

    outcome.written = _store.Write(_name, file);
    return outcome;
  }

  bool LoadCommanderFrom(CommanderStore& _store, CommanderBlock& _outBlock, std::span<std::uint8_t, COMMANDER_NAME_SIZE> _name) noexcept
  {
    std::array<std::uint8_t, COMMANDER_FILE_SIZE> file{};
    if (!_store.Read(_name, file))
    {
      return false;
    }

    // 6502: DFAULT -- and the checksums are what decide, not the read.
    return LoadCommander(file, _outBlock, _name);
  }

  DiskMenuResult DiskAccessMenu(SaveScreen& _screen, CommanderBlock& _block, std::span<std::uint8_t, COMMANDER_NAME_SIZE> _name,
                                std::span<std::uint8_t, COMMANDER_FILE_SIZE> _image, std::span<std::uint8_t> _buffer,
                                std::uint8_t& _useDisk) noexcept
  {
    /*
     * 6502: RLINE, which is a global in the original and a local here.
     *
     * GTNME lowers the length to seven and puts it back to nine on BOTH exits, so nothing outside
     * this routine can observe the change -- and the menu is the game's only caller of MT26.
     */
    LineLimits limits;

    // 6502: NA% -- the eight bytes of name at the front of the image, which TRNME writes.
    const auto imageName = _image.first<COMMANDER_NAME_SIZE>();

    // 6502: INWK+5 -- what KERNALSETUP turns into a filename, which is the LINE and not the image.
    const auto TypedName = [&]() noexcept
    {
      std::array<std::uint8_t, COMMANDER_NAME_SIZE> typed{};
      for (std::size_t index = 0; index < COMMANDER_NAME_SIZE && index < _buffer.size(); ++index)
      {
        typed[index] = _buffer[index];
      }
      return typed;
    };

    /*
     * 6502: JSR LOD's return address, which the error paths never pop.
     *
     * `tapeerror` and `ELT2F` end in `JMP SVE`, not in an RTS -- so when a load fails, the menu is
     * RE-ENTERED with `loading`'s frame still on the stack. Whatever leaf the player reaches next
     * therefore returns into `loading` at its `JSR TRNME`, and runs the three instructions that
     * were waiting there: store the typed name, SEC, RTS.
     *
     * The consequences are not small. Leaving the menu with "5" after a failed load renames the
     * saved commander to whatever was typed and tells the caller a new commander was loaded, so
     * TT102 restarts the game instead of returning to the docking bay -- and the restart's DFAULT
     * then makes that rename stick. Nothing was loaded. The frame is pushed again on every failed
     * load, so the stack grows two bytes at a time: a player who fails a hundred and twenty-eight
     * loads without leaving the menu runs out of it.
     *
     * One flag is enough for the port because TRNME is idempotent: N pending frames run it N times
     * on the way out, and the second run copies the bytes the first one wrote.
     */
    bool loadFramePending = false;

    /*
     * The three instructions at the top of that frame, applied to whatever leaf pops it.
     *
     * The outcome still names the leaf the menu actually reached, because that is what it is for.
     * A caller that sees `Left` with `newCommander` set is not being lied to -- it is looking at
     * the game.
     */
    const auto Leave = [&](DiskMenuResult _result) noexcept
    {
      if (loadFramePending)
      {
        StoreCommanderName(_buffer, imageName); // 6502: JSR TRNME
        _result.newCommander = true;            // 6502: SEC
      }
      return _result;
    };

    for (;;)
    {
      // 6502: LDA #1 / JSR DETOK -- the menu, redrawn every time round.
      _screen.extended.Print(MENU_TOKEN);

      // 6502: JSR t.
      const std::uint8_t key = _screen.keys.NextKey();

      /*
       * 6502: `loading` -- JSR GTNMEW / JSR LOD / JSR TRNME / SEC / RTS.
       *
       * The order is the interesting part. LOD runs BEFORE TRNME, so the name it opens is the one
       * MT26 left in the line buffer while the image still holds the previous commander's, and
       * TRNME only makes them agree afterwards. And nothing here touches the live commander: the
       * carry is the whole message, and the caller's DFAULT is what acts on it.
       */
      if (key == DISK_MENU_LOAD)
      {
        // The name GTNME falls back on is the IMAGE's, through TR1's `LDA NA%,X` -- not the live
        // commander's. Type nothing and you keep the name you last saved under, which need not be
        // the name you are playing as.
        (void)AskCommanderName(_screen.keys, _screen.chpr, _screen.text, _screen.extended, _screen.effects, _buffer, imageName, limits);

        std::array<std::uint8_t, COMMANDER_FILE_SIZE> file{};

        // 6502: JSR KERNALLOAD / BCS tapeerror -- the device could not read it.
        if (!_screen.store.Read(TypedName(), file))
        {
          loadFramePending = true;
          ReportAndReturnToMenu(_screen, DEVICE_ERROR_TOKEN);
          continue;
        }

        // 6502: LDA TAP% / BMI ELT2F -- it read something, but not a commander.
        if ((file[BLOCK_IN_FILE] & NOT_A_COMMANDER) != 0u)
        {
          loadFramePending = true;
          ReportAndReturnToMenu(_screen, BAD_FILE_TOKEN);
          continue;
        }

        // 6502: `copyme` -- the BLOCK into NA%+8, and only the block. The image keeps the name it
        // had until TRNME below overwrites it.
        for (std::size_t index = 0; index < COMMANDER_BLOCK_SIZE; ++index)
        {
          _image[BLOCK_IN_FILE + index] = file[BLOCK_IN_FILE + index];
        }

        // 6502: JSR TRNME -- the typed name over the one the image was carrying.
        StoreCommanderName(_buffer, imageName);

        DiskMenuResult result;
        result.outcome = DiskMenuOutcome::Loaded;
        result.newCommander = true; // 6502: SEC
        return Leave(result);
      }

      /*
       * 6502: SV1 -- and every failure in it goes back to the menu rather than out of it.
       */
      if (key == DISK_MENU_SAVE)
      {
        (void)AskCommanderName(_screen.keys, _screen.chpr, _screen.text, _screen.extended, _screen.effects, _buffer, imageName, limits);

        // 6502: JSR TRNME -- and here it runs BEFORE the file is touched, so the name the store is
        // given and the name in the image are the same eight bytes.
        StoreCommanderName(_buffer, imageName);

        // 6502: LDA #4 / JSR DETOK. It comes one instruction after `LSR SVC` in the original and
        // one before it here, which nothing can observe: the token does not read the save count.
        _screen.extended.Print(SAVE_TOKEN);

        /*
         * The name goes in from the IMAGE, which is where TRNME just put it -- not from the line
         * buffer, even though the two hold the same eight bytes here.
         *
         * SV1 reads NA% for the file's name and INWK+5 for the device's filename, and TRNME is
         * what makes them agree. Taking both from the buffer would work and would make TRNME dead
         * code in the port: the original's copy would still be reproduced, but nothing would
         * depend on it, and a later change that moved it would go unnoticed.
         */
        const SaveOutcome saved = SaveCommanderTo(_screen.store, _block, imageName);

        // 6502: SVL1 and the three checksums -- the image is what they leave behind.
        for (std::size_t index = 0; index < COMMANDER_FILE_SIZE; ++index)
        {
          _image[index] = saved.image[index];
        }

        /*
         * 6502: CLC / JSR BPRNT -- the competition number, printed BEFORE the Kernal is called.
         *
         * So a save the device refuses still shows a number, and the number it shows is the one
         * the refused file would have had.
         */
        for (std::size_t index = 0; index < saved.competition.value.size(); ++index)
        {
          _screen.numbers.k[index] = saved.competition.value[index];
        }
        PrintNumber(_screen.characters, _screen.numbers, false);

        // 6502: JSR TT67 / JSR TT67 -- two of them, so the number gets a blank line under it.
        PrintNewline(_screen.printer);
        PrintNewline(_screen.printer);

        // 6502: BCS saveerror -- which is a JMP to tapeerror, the same body the load path uses. No
        // frame is left behind: SV1 is reached by a BRANCH from the menu, so the only return
        // address on the stack is still SVE's own.
        if (!saved.written)
        {
          ReportAndReturnToMenu(_screen, DEVICE_ERROR_TOKEN);
          continue;
        }

        /*
         * 6502: JSR DFAULT -- the save reads its own file image straight back.
         *
         * Which means a save is also a load: the platform bit goes on in the competition flags, the
         * block's checksum byte is left as DFAULT's copy loop leaves it, and the commander that
         * carries on is the rebuilt one rather than the one the player was playing. It is the same
         * bytes, so nothing visible moves -- but the routine that runs is a load.
         */
        (void)LoadCommander(_image, _block, _name);

        // 6502: JSR t -- one key before the menu gives the screen back.
        (void)_screen.keys.NextKey();

        DiskMenuResult result;
        result.outcome = DiskMenuOutcome::Saved;
        result.newCommander = false; // 6502: SVEX -- CLC, in spite of the DFAULT above
        result.competition = saved.competition;
        return Leave(result);
      }

      // 6502: feb10 -- LDA DISK / EOR #&FF / STA DISK / JMP SVE. Tape is 0 and disk is &FF, and
      // the redisplay is the point: control code 31 in the menu names the media it is not using.
      if (key == DISK_MENU_MEDIA)
      {
        // 6502: EOR #&FF -- the same all-eight-bits flip the pause screen's `DKS3` does, and
        // the reason `DISK` is a BYTE and not a bool: it is one of the thirteen toggles, and a
        // bool cannot hold the &FF the indexed store writes (§6.139).
        _useDisk = static_cast<std::uint8_t>(_useDisk ^ 0xFFu);
        continue;
      }

      /*
       * 6502: LDA #224 / JSR DETOK / JSR YESNO / BCC feb13 / JSR JAMESON / JMP DFAULT.
       *
       * "No" leaves the menu rather than redisplaying it, which is the one place SVE treats a
       * refusal as an exit. And the `JMP` is a tail call, so what the caller sees on return is
       * DFAULT's carry -- set, because the comparison it ends on is the one that agreed.
       */
      if (key == DISK_MENU_DEFAULT)
      {
        _screen.extended.Print(CONFIRM_TOKEN);
        if (!AskYesNo(_screen.keys))
        {
          return Leave(DiskMenuResult{});
        }

        // 6502: JSR JAMESON -- NA2% over NA%, which is an image and not the live commander.
        ResetToDefaultCommander(_image);

        // 6502: JMP DFAULT -- and only now is the default commander actually in play.
        (void)LoadCommander(_image, _block, _name);

        DiskMenuResult result;
        result.outcome = DiskMenuOutcome::Reset;
        result.newCommander = true; // 6502: DFAULT's own `CMP CHK3`, not anything SVE writes
        return Leave(result);
      }

      // 6502: feb13 -- CLC / RTS. Anything that is not one of the four keys.
      return Leave(DiskMenuResult{});
    }
  }

} // namespace Elite
