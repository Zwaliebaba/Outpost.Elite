#pragma once

#include "Cpu6502.h"

#include <cstdint>
#include <string>
#include <unordered_map>

/*
 * The assembled original, loaded and callable by label (ADR-003 section 1).
 *
 * Everything this needs is produced by "python tools/labels.py --assemble":
 *
 *   Design/Reference/Labels.txt     ~1,800 labels and their runtime addresses
 *   Design/Reference/Binaries.txt   which assembled block loads where
 *   Upstream/.../3-assembled-output/ELTA.bin .. ELTK.bin
 *
 * None of those is committed (ADR-001 section 5), so a fresh clone has no oracle until the
 * assembler has been run. That is reported loudly rather than silently skipped: Available()
 * is false, Reason() says why, and OracleIsPresent fails.
 *
 * The image is loaded once and copied per call, so a test can scribble anywhere in the 64 KB
 * without leaking state into the next one.
 */

namespace Elite::Testing
{

  class OracleImage
  {
  public:
    /// Loads on first use. Cheap afterwards, and never throws.
    static const OracleImage& Instance();

    /*
     * The assembled LOADER, which is a separate 64 KB space and not part of the game's.
     *
     * `COMLOD` loads at &4000 -- the game's screen bitmap -- so the two can never be in one image:
     * putting the loader into the oracle would start every drawing comparison on a screen full of
     * loader code. It carries two things this port needs and nothing else does, `sdump` and
     * `cdump`, so `TableTests` is its only caller.
     */
    static const OracleImage& LoaderInstance();

    /// The sprite definitions' own image -- `elite-sprites.asm` at &7C3A (ADR-005 section 1).
    static const OracleImage& SpriteInstance();

    [[nodiscard]] bool Available() const noexcept
    {
      return m_available;
    }

    /// Why the oracle is unavailable, in a form a person can act on. Empty when it is available.
    [[nodiscard]] const std::string& Reason() const noexcept
    {
      return m_reason;
    }

    /// The address of a label, or false if this build has no such label.
    [[nodiscard]] bool TryLabel(const std::string& _name, std::uint16_t& _outAddress) const;

    /// The address of a label that must exist. Returns 0 and is a test failure waiting to happen
    /// if it does not, which is preferable to silently calling address zero.
    [[nodiscard]] std::uint16_t Label(const std::string& _name) const;

    /// A processor with the game loaded, registers cleared and the stack fresh.
    [[nodiscard]] Cpu6502 Fresh() const;

    [[nodiscard]] std::size_t LabelCount() const noexcept
    {
      return m_labels.size();
    }
    [[nodiscard]] std::size_t BlockCount() const noexcept
    {
      return m_blockCount;
    }

  private:
    OracleImage(const char* _labelsFile, const char* _binariesFile);

    bool m_available = false;
    std::string m_reason;
    std::size_t m_blockCount = 0;

    std::array<std::uint8_t, 65536> m_memory{};
    std::unordered_map<std::string, std::uint16_t> m_labels;
  };

} // namespace Elite::Testing
