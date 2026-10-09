// Machine/DirectoryFileStore.h
#pragma once

#include "FileStore.h"

#include <filesystem>
#include <map>
#include <string>

namespace Machine
{

/// A FileStore over one host directory: DOS name NAME.EXT is the file NAME.EXT in it. Lookups ignore case, so an
/// existing jameson.cdr is JAMESON.CDR, and a new file is created with the upper-case name. Subdirectories, and host
/// files whose names are not valid 8.3 ASCII names, are invisible. Nothing outside the directory can be named
/// (FileStore::CanonicalName). The directory must exist; without it every operation fails with PathNotFound.
///
/// Attributes and stamps are kept per file, in memory, for the store's lifetime, because the host has nowhere to keep
/// DOS's attribute bits. A file the store has not created or changed reads as attribute 0 (a plain file with the
/// archive bit clear) stamped 1980-01-01 00:00:00. That default is load-bearing: the game refuses to load a commander
/// file with any attribute set (LoadCommanderFile, CS:031A) and lists only files whose attribute byte is 0
/// (ListCommanderFiles, CS:03BC). It clears the archive bit itself after saving (SaveCommanderFile, CS:0382), so a
/// commander saved in this session loads, and one saved in an earlier session, or copied in, does too. Stamps come
/// from DOS's clock, never from the host's file times, so a run does not depend on when its files were made.
///
/// Not thread-safe. The store must outlive every file it opens.
class DirectoryFileStore final : public FileStore
{
public:
  explicit DirectoryFileStore(std::filesystem::path _directory);

  [[nodiscard]] DosError Open(std::string_view _name, FileAccess _access, std::unique_ptr<OpenFile>& _file) override;
  [[nodiscard]] DosError Create(std::string_view _name, std::uint8_t _attribute, const FileStamp& _stamp,
                                std::unique_ptr<OpenFile>& _file) override;
  [[nodiscard]] DosError Delete(std::string_view _name) override;
  [[nodiscard]] DosError Attribute(std::string_view _name, std::uint8_t& _attribute) override;
  [[nodiscard]] DosError SetAttribute(std::string_view _name, std::uint8_t _attribute) override;
  [[nodiscard]] std::vector<FileEntry> List() override;

private:
  class HostFile;

  struct Record
  {
    std::uint8_t attribute = 0;
    FileStamp stamp;
  };

  /// The host file a canonical DOS name stands for: the entry spelled exactly so if there is one, else the first,
  /// in host-name order, that matches it ignoring case, else the name itself in the directory.
  [[nodiscard]] std::filesystem::path HostPath(const std::string& _canonical) const;

  /// Canonicalizes a name and finds it as an existing regular file: FileNotFound when there is none, PathNotFound
  /// without the directory, or the name's own error.
  [[nodiscard]] DosError ExistingFile(std::string_view _name, std::string& _canonical, std::filesystem::path& _path) const;

  [[nodiscard]] Record RecordOf(const std::string& _canonical) const;
  void RecordWrite(const std::string& _canonical, const FileStamp& _stamp);

  std::filesystem::path m_directory;
  std::map<std::string, Record> m_records;
};

} // namespace Machine
