// GameLogic/DosFiles.h
#pragma once

#include "Counted.h"
#include "FileStore.h"
#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Elite
{

class GameState;

/// A moment on DOS's calendar, to the hundredth of a second. A plain record (R8). The year must be 1980 to 2099.
struct DosDateTime
{
  std::uint16_t year = 1987;
  std::uint8_t month = 10;
  std::uint8_t day = 1;
  std::uint8_t hour = 12;
  std::uint8_t minute = 0;
  std::uint8_t second = 0;
  std::uint8_t hundredths = 0;
};

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

/// DOS's file services as the game uses them (int 21h 1Ah, 3Ch-3Fh, 40h, 41h, 43h, 4Eh and 4Fh), done natively over a FileStore
/// (D27): the first of the native platform's devices (Platform). The emulated DOS, Machine::Dos until D33, was the
/// specification, and this is its file half ported call for call: the same handles from 5, the same CF and AX on every path,
/// the same bytes read into the game's memory and the same transfer area after a find, the same files left in the store, and
/// the same stamps, from DOS's clock (Now).
///
/// It holds what DOS held: the open handles and the transfer area. The search in progress is in the transfer area itself, at
/// its reserved bytes 00h-14h, as DOS keeps it: find-next reads it from there, as DOS did.
///
/// Where the original hands DOS an address in DS:DX, a call takes what is there (D38): the name, up to its NUL, or the buffer, a
/// span of the member that holds it; and the transfer area is a span of its member too. A byte a call writes is a Counted of
/// the game's state, and a buffer or a transfer area that is not all in the game's state throws std::out_of_range, and nothing is
/// written, as it did while the state counted its changes (ADR-024). A call takes no time and counts no instruction cycles, as the emulated DOS's calls did not.
///
/// What the emulated DOS refused as a fault, stopping the run, this refuses by throwing std::logic_error: a close, read or
/// write on a standard handle (0 to 4), and a write of no bytes. So it refuses a search before the program has set a transfer
/// area: DOS's default was PSP:0080, and the PSP is not the game's memory (D33). The game reaches none of them.
class DosFiles
{
public:
  static constexpr std::uint16_t FIRST_HANDLE = 5;
  static constexpr std::uint16_t HANDLE_COUNT = 20;        // the PSP's table, the first five DOS's standard handles
  static constexpr std::uint8_t CURRENT_DRIVE = 3;         // C:, counting A: as 1, as the transfer area does
  static constexpr std::size_t TRANSFER_AREA_BYTES = 0x2B; // what find-first and find-next fill
  static constexpr std::size_t MOST_BUFFER_BYTES = 0xFFFF; // CX's count

  /// _state is the game's memory and _files where the files are; both must outlive this. DOS's clock starts at
  /// _startMoment.
  DosFiles(GameState& _state, FileStore& _files, const DosDateTime& _startMoment) noexcept;
  DosFiles(const DosFiles&) = delete;
  DosFiles& operator=(const DosFiles&) = delete;

  /// The program starts, as DOS starts it: every file closed, and no transfer area until it sets one.
  void StartProgram() noexcept;

  /// Int 21h AH=1Ah: the transfer area, where find-first and find-next leave what they find: the first TRANSFER_AREA_BYTES of
  /// _area, which must be in the game's state. A shorter one throws std::logic_error.
  void SetTransferArea(std::span<Counted<std::uint8_t>> _area);

  /// Int 21h AX=4300h: the attributes of the file _name names. A name, here and in every call that takes one, is DOS's ASCIIZ
  /// name: up to its first NUL, or all of it, and PathNotFound when no NUL ends it in DOS's 128 bytes.
  [[nodiscard]] DosFileAttributes ReadAttributes(std::string_view _name);

  /// Int 21h AX=4301h: the attributes of the file _name names set to _attributes. AccessDenied above FFh.
  DosAnswer SetAttributes(std::string_view _name, std::uint16_t _attributes);

  /// Int 21h AH=3Ch: the file _name names created, or emptied, with _attributes' low byte, open to read and write, and stamped
  /// with DOS's time at _clock; the value its handle.
  [[nodiscard]] DosAnswer Create(std::string_view _name, std::uint16_t _attributes, Cycles _clock);

  /// Int 21h AH=3Dh: the file _name names opened for _access's bits 0-2, 0 to read, 1 to write, 2 both; any other is
  /// InvalidAccessCode, and the sharing bits are ignored. The value its handle.
  [[nodiscard]] DosAnswer Open(std::string_view _name, std::uint8_t _access);

  /// Int 21h AH=3Fh: up to all of _buffer's bytes from the file _handle into it, from its first; the value the count read, 0
  /// at the end of the file. A buffer that is not all in the game's state, or longer than CX counts, throws std::out_of_range,
  /// and nothing is read.
  [[nodiscard]] DosAnswer Read(std::uint16_t _handle, std::span<Counted<std::uint8_t>> _buffer);

  /// Int 21h AH=40h: _buffer's bytes written to the file _handle, which is stamped with DOS's time at _clock; the value the
  /// count written. A buffer longer than CX counts throws std::out_of_range, and nothing is written.
  [[nodiscard]] DosAnswer Write(std::uint16_t _handle, std::span<const std::uint8_t> _buffer, Cycles _clock);

  /// Int 21h AH=3Eh: the file _handle closed.
  DosAnswer Close(std::uint16_t _handle);

  /// Int 21h AH=41h: the file _name names deleted.
  DosAnswer Delete(std::string_view _name);

  /// Int 21h AH=4Eh: the first file matching _pattern ('?' and '*'), a name as ReadAttributes reads one, with no hidden, system
  /// or directory bit beyond _attributes' low byte's, into the transfer area. NoMoreFiles when none matches, PathNotFound for a
  /// pattern with a path, FileNotFound for a malformed one.
  [[nodiscard]] DosAnswer FindFirst(std::string_view _pattern, std::uint16_t _attributes);

  /// Int 21h AH=4Fh: the next file of the search the transfer area holds, into it; NoMoreFiles at the end.
  [[nodiscard]] DosAnswer FindNext();

  /// DOS's date and time at _clock, the machine's cycles since power-on: the start moment plus the time counted, to the
  /// hundredth of a second.
  [[nodiscard]] DosDateTime Now(Cycles _clock) const;

  /// A moment as a DOS directory entry packs it.
  [[nodiscard]] static FileStamp Stamp(const DosDateTime& _moment) noexcept;

private:
  /// Continues a search for _pattern, DOS's 11-character template, from entry _index, filling the transfer area, and on a
  /// match its file.
  [[nodiscard]] DosError Find(const std::array<char, 11>& _pattern, std::uint8_t _attribute, std::uint16_t _index);

  /// The ASCIIZ name _name, up to its first NUL, or nothing if no NUL ends it in 128 bytes.
  [[nodiscard]] static std::optional<std::string> NameIn(std::string_view _name);
  [[nodiscard]] OpenFile* HandleFile(std::uint16_t _handle) const noexcept;
  [[nodiscard]] int FreeHandle() const noexcept;
  // Throws std::logic_error when the program has set no transfer area.
  void RefuseNoTransferArea() const;
  // Throws std::out_of_range when _bytes are not all in the game's state, where their writes count.
  void RefuseOutsideState(std::span<const Counted<std::uint8_t>> _bytes) const;

  GameState& m_state;
  FileStore& m_files;
  DosDateTime m_startMoment;
  std::array<std::unique_ptr<OpenFile>, HANDLE_COUNT> m_handles;
  /// The transfer area's TRANSFER_AREA_BYTES, empty until the program sets one.
  std::span<Counted<std::uint8_t>> m_transferArea;
};

} // namespace Elite
