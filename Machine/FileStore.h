// Machine/FileStore.h
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Machine
{

/// The DOS error codes the file functions return in AX with CF set. Exactly the ones the services produce.
enum class DosError : std::uint16_t
{
  None = 0x00,
  FileNotFound = 0x02,
  PathNotFound = 0x03,
  TooManyOpenFiles = 0x04,
  AccessDenied = 0x05,
  InvalidHandle = 0x06,
  InvalidAccessCode = 0x0C,
  NoMoreFiles = 0x12
};

/// A date and time packed the way a DOS directory entry holds them. A plain record (R8).
struct FileStamp
{
  std::uint16_t date = 0x0021; ///< (year - 1980) << 9 | month << 5 | day: 1980-01-01, DOS's earliest
  std::uint16_t time = 0;      ///< hour << 11 | minute << 5 | second / 2
};

/// One file as DOS's find functions report it. A plain record (R8).
struct FileEntry
{
  std::string name; ///< "NAME.EXT", upper case, a valid 8.3 name
  std::uint8_t attribute = 0;
  FileStamp stamp;
  std::uint32_t sizeBytes = 0;
};

enum class FileAccess : std::uint8_t
{
  Read,
  Write,
  ReadWrite
};

/// An open file: what a DOS handle refers to. It keeps its own position, shared by reads and writes, as a handle does.
class OpenFile
{
public:
  OpenFile() = default;
  OpenFile(const OpenFile&) = delete;
  OpenFile& operator=(const OpenFile&) = delete;
  virtual ~OpenFile() = default;

  /// Reads up to the buffer's size from the position into it, and sets _count to how many bytes it read: 0 at the end
  /// of the file. AccessDenied on a file opened for writing only, or when the host fails.
  [[nodiscard]] virtual DosError Read(std::span<std::uint8_t> _buffer, std::uint32_t& _count) = 0;

  /// Writes the bytes at the position, sets the file's archive bit and its stamp, and sets _count to how many it
  /// wrote. AccessDenied on a file opened for reading only, or when the host fails.
  [[nodiscard]] virtual DosError Write(std::span<const std::uint8_t> _bytes, const FileStamp& _stamp, std::uint32_t& _count) = 0;
};

/// Where DOS's files live: one flat directory of 8.3 names, with no drives and no subdirectories. DOS (Dos.h) owns the
/// handles and the find state; a store only opens, creates, deletes and lists files, and keeps their attributes.
///
/// Every name a store is given goes through CanonicalName first, and is refused with its error if it fails: a name
/// with a path in it is never resolved, so nothing outside the store's directory can be reached. Every operation
/// returns DosError::None or the error DOS reports, and writes its result to its last parameter only on success.
class FileStore
{
public:
  static constexpr std::uint8_t ATTRIBUTE_READ_ONLY = 0x01;
  static constexpr std::uint8_t ATTRIBUTE_HIDDEN = 0x02;
  static constexpr std::uint8_t ATTRIBUTE_SYSTEM = 0x04;
  static constexpr std::uint8_t ATTRIBUTE_VOLUME = 0x08;
  static constexpr std::uint8_t ATTRIBUTE_DIRECTORY = 0x10;
  static constexpr std::uint8_t ATTRIBUTE_ARCHIVE = 0x20;
  /// The bits a file can have set: 43h AX=4301h refuses any other.
  static constexpr std::uint8_t ATTRIBUTES_SETTABLE = 0x27;

  FileStore() = default;
  FileStore(const FileStore&) = delete;
  FileStore& operator=(const FileStore&) = delete;
  virtual ~FileStore() = default;

  /// Opens an existing file at position 0. FileNotFound when there is none; AccessDenied for writing to a read-only
  /// file, or when the host cannot open it.
  [[nodiscard]] virtual DosError Open(std::string_view _name, FileAccess _access, std::unique_ptr<OpenFile>& _file) = 0;

  /// Creates a file, or truncates an existing one, open for reading and writing. Its attribute becomes _attribute
  /// (read-only, hidden, system) plus archive, and its stamp _stamp. AccessDenied for the volume or directory bit, for
  /// an existing read-only file, or when the host cannot create it.
  [[nodiscard]] virtual DosError Create(std::string_view _name, std::uint8_t _attribute, const FileStamp& _stamp,
                                        std::unique_ptr<OpenFile>& _file) = 0;

  /// FileNotFound when there is no such file; AccessDenied for a read-only file, or when the host cannot delete it.
  [[nodiscard]] virtual DosError Delete(std::string_view _name) = 0;

  [[nodiscard]] virtual DosError Attribute(std::string_view _name, std::uint8_t& _attribute) = 0;

  /// AccessDenied for a bit outside ATTRIBUTES_SETTABLE.
  [[nodiscard]] virtual DosError SetAttribute(std::string_view _name, std::uint8_t _attribute) = 0;

  /// Every file, sorted by name.
  [[nodiscard]] virtual std::vector<FileEntry> List() = 0;

  /// A DOS file name in the form a store keys it by: upper case, "NAME" or "NAME.EXT" with a base of 1 to 8 and an
  /// extension of 0 to 3 printable ASCII characters other than the ones DOS reserves ("*+,./:;<=>?[\]| and space).
  /// PathNotFound for anything that names a directory or a drive -- a '\', '/' or ':', or "." and ".." -- and
  /// FileNotFound for any other malformed name, a wildcard included. Long names are refused, not truncated.
  [[nodiscard]] static DosError CanonicalName(std::string_view _name, std::string& _canonical);

  /// Whether a character may appear in the base or the extension of a DOS name (CanonicalName).
  [[nodiscard]] static bool IsNameCharacter(char _character) noexcept;
};

} // namespace Machine
