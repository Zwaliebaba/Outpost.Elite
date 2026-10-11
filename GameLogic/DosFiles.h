// GameLogic/DosFiles.h
#pragma once

#include "Dos.h"
#include "FileStore.h"
#include "Timing.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace Elite
{

class GameState;

/// What a DOS file service (int 21h) answers: CF, and AX.
struct DosAnswer
{
  bool failed;         ///< CF: the service failed
  std::uint16_t value; ///< AX: the error if it failed; otherwise a handle, a count of bytes, 0 from a find, or 0 from a close, a
                       ///< delete or a set of attributes, where DOS leaves AX as it went in and no caller reads it
};

/// What DOS answers to int 21h AX=4300h: a DosAnswer, and the file's attributes in CX.
struct DosFileAttributes
{
  DosAnswer answer;
  std::uint16_t attributes; ///< CX: the attributes when the service did not fail; 0 when it did, where DOS leaves CX as it went in
                            ///< and no caller reads it
};

/// DOS's file services as the game uses them (int 21h 1Ah, 3Ch-3Fh, 40h, 41h, 43h, 4Eh and 4Fh), done natively over a
/// Machine::FileStore (D27): the first of the native platform's devices (Platform). Machine::Dos is the specification, and
/// this is its file half ported call for call: the same handles from 5, the same CF and AX on every path, the same bytes
/// read into the game's memory and the same transfer area after a find, the same files left in the store, and the same
/// stamps, from DOS's clock (Now).
///
/// It holds what DOS held: the open handles and the transfer area's address. The search in progress is in the transfer
/// area itself, at its reserved bytes 00h-14h, as DOS keeps it: find-next reads it from there, as Machine::Dos does.
///
/// The game's memory is read and written through the GameState's far accessors, at the segment and offset or the linear
/// address the emulated DOS used, so every byte a call changes goes through Machine::Memory once and paced time's change
/// count sees it as before (ADR-015 item 5). A call takes no time, writes no port and counts no instruction cycles, as the
/// emulated DOS's calls did not.
///
/// What the emulated DOS refused as a fault, stopping the run, this refuses by throwing std::logic_error: a close, read or
/// write on a standard handle (0 to 4), and a write of no bytes. The game reaches neither.
class DosFiles
{
public:
  static constexpr std::uint16_t FIRST_HANDLE = 5;
  static constexpr std::uint16_t HANDLE_COUNT = 20; // the PSP's table, the first five DOS's standard handles
  static constexpr std::uint8_t CURRENT_DRIVE = 3;  // C:, counting A: as 1, as the transfer area does
  static constexpr std::uint16_t TRANSFER_AREA_BYTES = 43;
  static constexpr std::uint16_t DEFAULT_TRANSFER_AREA = 0x80; // at the PSP, over the command tail

  /// _state is the game's memory and _files where the files are; both must outlive this. DOS's clock starts at
  /// _startMoment.
  DosFiles(GameState& _state, Machine::FileStore& _files, const Machine::Dos::DateTime& _startMoment) noexcept;
  DosFiles(const DosFiles&) = delete;
  DosFiles& operator=(const DosFiles&) = delete;

  /// The program whose PSP is at _pspSegment starts, as Machine::Dos::StartProgram has it: every file closed, and the
  /// transfer area at PSP:0080.
  void StartProgram(std::uint16_t _pspSegment) noexcept;

  /// Int 21h AH=1Ah: the transfer area, where find-first and find-next leave what they find, at _segment:_offset.
  void SetTransferArea(std::uint16_t _segment, std::uint16_t _offset) noexcept;

  /// Int 21h AX=4300h: the attributes of the file named at _segment:_name.
  [[nodiscard]] DosFileAttributes ReadAttributes(std::uint16_t _segment, std::uint16_t _name);

  /// Int 21h AX=4301h: the attributes of the file named at _segment:_name set to _attributes. AccessDenied above FFh.
  DosAnswer SetAttributes(std::uint16_t _segment, std::uint16_t _name, std::uint16_t _attributes);

  /// Int 21h AH=3Ch: the file named at _segment:_name created, or emptied, with _attributes' low byte, open to read and write,
  /// and stamped with DOS's time at _clock; the value its handle.
  [[nodiscard]] DosAnswer Create(std::uint16_t _segment, std::uint16_t _name, std::uint16_t _attributes, Machine::Cycles _clock);

  /// Int 21h AH=3Dh: the file named at _segment:_name opened for _access's bits 0-2, 0 to read, 1 to write, 2 both; any other
  /// is InvalidAccessCode, and the sharing bits are ignored. The value its handle.
  [[nodiscard]] DosAnswer Open(std::uint16_t _segment, std::uint16_t _name, std::uint8_t _access);

  /// Int 21h AH=3Fh: up to _bytes from the file _handle into the memory from _segment:_buffer, by linear address; the value
  /// the count read, 0 at the end of the file.
  [[nodiscard]] DosAnswer Read(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes);

  /// Int 21h AH=40h: _bytes from the memory at _segment:_buffer, by linear address, written to the file _handle, which is
  /// stamped with DOS's time at _clock; the value the count written.
  [[nodiscard]] DosAnswer Write(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes,
                                Machine::Cycles _clock);

  /// Int 21h AH=3Eh: the file _handle closed.
  DosAnswer Close(std::uint16_t _handle);

  /// Int 21h AH=41h: the file named at _segment:_name deleted.
  DosAnswer Delete(std::uint16_t _segment, std::uint16_t _name);

  /// Int 21h AH=4Eh: the first file matching the pattern at _segment:_pattern ('?' and '*'), with no hidden, system or
  /// directory bit beyond _attributes' low byte's, into the transfer area. NoMoreFiles when none matches, PathNotFound for a
  /// pattern with a path, FileNotFound for a malformed one.
  [[nodiscard]] DosAnswer FindFirst(std::uint16_t _segment, std::uint16_t _pattern, std::uint16_t _attributes);

  /// Int 21h AH=4Fh: the next file of the search the transfer area holds, into it; NoMoreFiles at the end.
  [[nodiscard]] DosAnswer FindNext();

  /// DOS's date and time at _clock, the machine's cycles since power-on: the start moment plus the time counted, to the
  /// hundredth of a second.
  [[nodiscard]] Machine::Dos::DateTime Now(Machine::Cycles _clock) const;

  /// A moment as a DOS directory entry packs it.
  [[nodiscard]] static Machine::FileStamp Stamp(const Machine::Dos::DateTime& _moment) noexcept;

private:
  /// Continues a search for _pattern, DOS's 11-character template, from entry _index, filling the transfer area, and on a
  /// match its file.
  [[nodiscard]] Machine::DosError Find(const std::array<char, 11>& _pattern, std::uint8_t _attribute, std::uint16_t _index);

  /// The ASCIIZ name at _segment:_name, or nothing if it runs past 128 bytes.
  [[nodiscard]] std::optional<std::string> NameAt(std::uint16_t _segment, std::uint16_t _name) const;
  [[nodiscard]] Machine::OpenFile* HandleFile(std::uint16_t _handle) const noexcept;
  [[nodiscard]] int FreeHandle() const noexcept;

  GameState& m_state;
  Machine::FileStore& m_files;
  Machine::Dos::DateTime m_startMoment;
  std::array<std::unique_ptr<Machine::OpenFile>, HANDLE_COUNT> m_handles;
  std::uint16_t m_transferSegment = 0;
  std::uint16_t m_transferOffset = 0;
};

} // namespace Elite
