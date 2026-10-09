// Machine/Dos.h
#pragma once

#include "FileStore.h"
#include "Registers.h"
#include "ServiceFault.h"
#include "Timing.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace Machine
{

class Bios;
class Memory;

/// DOS 3.30 at the call level, for exactly the int 21h functions the reference calls (plan §1) and int 20h. There is
/// no kernel in memory: the program's PSP, built by ExeLoader, is the only DOS structure it can see. Every function
/// either services the call or returns the fault, having changed nothing.
///
/// Int 21h, by AH. "Error" means CF set and AX = the DosError code; "success" means CF clear.
///  * 09h  Prints the string at DS:DX up to '$' through the BIOS teletype (Bios::WriteTeletype), expanding a tab to
///         the next multiple of 8 columns as the CON driver does. Returns AL = 24h, as DOS does. Faults
///         (UnsupportedRequest), printing nothing, in a graphics mode, for a BEL, or with no '$' in 64 KB.
///  * 0Ch  Faults: WouldBlock for AL = 0Ah, UnknownSubfunction for any other AL. Buffered input exists only for the
///         copy-protection question, which ADR-001's D5 patch skips, and the ROM's int 9 discards every key, so a
///         call here means the patch did not take: it must stop the run rather than wait for a line forever.
///  * 1Ah  Sets the disk transfer area to DS:DX. It starts at PSP:0080.
///  * 2Ch  CH:CL = hour:minute, DH:DL = second:hundredths from Now(). Unlike MS-DOS, whose clock is the BIOS tick
///         count and stands still while the game owns int 8, this one runs on machine time.
///  * 30h  AL = 3, AH = 30 (version 3.30), BH = 0 (IBM), BL = CX = 0 (no serial number).
///  * 3Ch  Creates (or truncates) the file named at DS:DX with attribute CL, open for reading and writing; AX = the
///         handle. Handles run from 5 to 19, the PSP's 20-entry table less the five standard ones.
///  * 3Dh  Opens the file named at DS:DX for AL bits 0-2: 0 read, 1 write, 2 both (InvalidAccessCode for anything
///         else; the sharing bits are ignored, as without SHARE); AX = the handle.
///  * 3Eh  Closes handle BX. InvalidHandle for a handle that is not open.
///  * 3Fh  Reads CX bytes from handle BX to DS:DX; AX = the bytes read, 0 at the end of the file.
///  * 40h  Writes CX bytes from DS:DX to handle BX; AX = the bytes written. CX = 0, which in DOS truncates the file
///         there, faults (UnsupportedRequest).
///  * 41h  Deletes the file named at DS:DX.
///  * 43h  AL=0: CX = AX = the attribute of the file named at DS:DX. AL=1: sets it from CL. Other AL: fault.
///  * 4Eh  Finds the first file matching the pattern at DS:DX ('?' and '*' wildcards) whose hidden, system and
///         directory bits are all in CL, and fills the DTA (below); AX = 0. NoMoreFiles when none matches,
///         PathNotFound for a pattern with a path, FileNotFound for a malformed one.
///  * 4Fh  Finds the next, from the state 4Eh left in the DTA; NoMoreFiles at the end.
/// Handles 0 to 4 (CON, AUX, PRN) are not provided: 3Eh, 3Fh or 40h on one faults (UnsupportedRequest). Any other AH
/// is UnknownFunction. File names go to the FileStore, which refuses paths and drives; see FileStore::CanonicalName.
///
/// The DTA after a successful find, in DOS's layout: 00h the drive (3, C:), 01h the 11-byte search template, 0Ch the
/// search attribute, 0Dh the index of the next entry to examine (a word), 0Fh-14h zero; then 15h the attribute, 16h
/// the time, 18h the date, 1Ah the size (a dword) and 1Eh the name, "NAME.EXT" in upper case, zero-padded to 13
/// bytes. 4Fh reads only the DTA, as DOS does, so a program may keep several searches in several DTAs.
///
/// Int 20h ends the program with exit code 0 (Terminate).
class Dos
{
public:
  /// A moment on DOS's calendar. A plain record (R8). The year must be 1980 to 2099.
  struct DateTime
  {
    std::uint16_t year = 1987;
    std::uint8_t month = 10;
    std::uint8_t day = 1;
    std::uint8_t hour = 12;
    std::uint8_t minute = 0;
    std::uint8_t second = 0;
    std::uint8_t hundredths = 0;
  };

  static constexpr std::uint16_t FIRST_FILE_HANDLE = 5;
  static constexpr std::uint16_t HANDLE_COUNT = 20;
  static constexpr std::uint8_t CURRENT_DRIVE = 3; // C:, counting A: as 1, as the DTA does
  static constexpr std::uint16_t DTA_BYTES = 43;

  /// _clock is the machine's cycle counter, which the integrator owns and advances. DOS's date and time are _start
  /// plus the time it has counted, so a run never depends on the host's clock.
  Dos(Memory& _memory, Bios& _bios, FileStore& _files, const Cycles& _clock, const DateTime& _start);

  /// Tells DOS which program runs: the DTA moves to its PSP:0080, every file is closed and the program is no longer
  /// terminated.
  void StartProgram(std::uint16_t _pspSegment);

  /// Int 21h.
  [[nodiscard]] std::optional<FaultKind> Call(Registers& _regs);

  /// Int 20h: ends the program with exit code 0, as DOS does. Every open file is closed, the int 22h, 23h and 24h
  /// vectors are restored from the PSP, and execution goes to the PSP's terminate address (PSP:000A), which the
  /// loader took from vector 22h: the ROM's CLI and HLT, since there is no parent to return to.
  void Terminate(Registers& _regs);

  [[nodiscard]] bool Terminated() const noexcept
  {
    return m_terminated;
  }

  [[nodiscard]] std::uint8_t ExitCode() const noexcept
  {
    return m_exitCode;
  }

  /// The start moment plus the machine time counted so far, to the hundredth of a second.
  [[nodiscard]] DateTime Now() const;

  /// A moment as a DOS directory entry packs it.
  [[nodiscard]] static FileStamp Stamp(const DateTime& _moment) noexcept;

private:
  void PrintString(Registers& _regs, std::optional<FaultKind>& _fault);
  void Create(Registers& _regs);
  void Open(Registers& _regs);
  void Close(Registers& _regs);
  void Read(Registers& _regs);
  void Write(Registers& _regs);
  void Delete(Registers& _regs);
  void Attributes(Registers& _regs, std::optional<FaultKind>& _fault);
  void FindFirst(Registers& _regs);
  void FindNext(Registers& _regs);

  /// Continues a search from entry _index, filling the DTA on a match.
  [[nodiscard]] DosError Find(const std::array<char, 11>& _pattern, std::uint8_t _attribute, std::uint16_t _index);

  /// The ASCIIZ name at DS:DX, or nothing if it runs past 128 bytes.
  [[nodiscard]] std::optional<std::string> NameAt(const Registers& _regs) const;
  [[nodiscard]] OpenFile* HandleFile(std::uint16_t _handle) const noexcept;
  [[nodiscard]] int FreeHandle() const noexcept;

  Memory& m_memory;
  Bios& m_bios;
  FileStore& m_files;
  const Cycles& m_clock;
  DateTime m_start;
  std::array<std::unique_ptr<OpenFile>, HANDLE_COUNT> m_handles;
  std::uint16_t m_pspSegment = 0;
  std::uint16_t m_dtaSegment = 0;
  std::uint16_t m_dtaOffset = 0;
  bool m_terminated = false;
  std::uint8_t m_exitCode = 0;
};

} // namespace Machine
