// Machine/ExeLoader.h
#pragma once

#include "Registers.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Machine
{

class Memory;

enum class LoadError : std::uint8_t
{
  None,
  NotExecutable,      ///< No MZ signature, no pages, or a header that cannot hold its own fields.
  Truncated,          ///< The file is shorter than its header or its relocation table says.
  BadRelocation,      ///< A relocation points outside the image.
  Unsupported,        ///< A load-high executable: minimum and maximum allocation both 0.
  CommandTailTooLong, ///< More than 126 characters.
  PspTooLow,          ///< No room for the environment between the DOS communication area and the PSP.
  DoesNotFit          ///< The PSP, the image and the minimum allocation do not fit below the memory top.
};

/// Where a program was loaded and how it starts. A plain record (R8).
struct LoadedProgram
{
  std::uint16_t pspSegment = 0;
  std::uint16_t loadSegment = 0; ///< The image's first paragraph: the PSP plus 10h.
  std::uint16_t environmentSegment = 0;
  std::uint16_t memoryTopSegment = 0; ///< The first paragraph past the program's memory, as PSP:0002 says.
  std::uint32_t imageBytes = 0;
  Registers registers;
};

/// Loads a DOS MZ executable the way DOS 3.30's EXEC does, into memory with the firmware already installed. Nothing is
/// written unless the whole load succeeds.
///
/// Memory layout, from the bottom: the interrupt table, the BIOS data area and the DOS communication area up to
/// 0060:0000 (Firmware); free memory; the environment, in the paragraphs directly below the PSP; the PSP at the
/// segment the caller chooses (Desc::pspSegment); the image from the PSP plus 10h; the program's further allocation
/// up to the memory top, 640 KB by default. There is no DOS kernel and no memory arena: nothing a program can see
/// below its environment belongs to DOS.
///
/// The allocation honours the header as EXEC does: the program needs the PSP, the image and its minimum allocation,
/// and is given up to its maximum allocation (FFFFh, as the reference asks, means everything to the memory top). If what
/// it needs does not fit, the load fails with DoesNotFit.
///
/// The PSP: CD 20 (int 20h) at 00h; the memory top at 02h; at 0Ah, 0Eh and 12h the int 22h, 23h and 24h vectors
/// from the interrupt table, which int 20h restores and returns through; the parent PSP at 16h (itself, as for a
/// first program); the 20-entry handle table at 18h, handles 0-4 the standard devices and 5-19 free; the
/// environment segment at 2Ch; the table's size and far address at 32h and 34h; FFFF:FFFF at 38h; CD 21 CB (int 21h,
/// retf) at 50h; two unopened FCBs at 5Ch and 6Ch, blank, since the command tail is not parsed into them; and the
/// command tail at 80h: its length, its text and 0Dh. A five-byte CP/M call at 05h is not provided.
///
/// The environment: COMSPEC=C:\COMMAND.COM, an empty string ending the list, the word 1, and the program's path.
///
/// The registers are the ones MS-DOS starts a program with, as DOSBox-X copies them (src/dos/dos_execute.cpp), so that
/// a boot trace can be compared with DOSBox-X's register for register (ADR-003): CS:IP and SS:SP from the header,
/// relocated; DS = ES = the PSP; AX and BX from the caller (Desc), 0 by default, the FCB drive-validity word for a
/// command tail with no drive letters; CX = 00FFh; DX = the PSP; SI = the initial IP; DI = the initial SP; BP = 091Ch;
/// FLAGS = F202h, IF set and everything else clear. DOS enters the program through a RETF, so the initial IP and CS
/// are also left at SS:SP-4 and SS:SP-2.
class ExeLoader
{
public:
  static constexpr std::uint16_t DEFAULT_PSP_SEGMENT = 0x1000;
  static constexpr std::uint16_t DEFAULT_MEMORY_TOP_SEGMENT = 0xA000;
  static constexpr std::uint16_t PSP_PARAGRAPHS = 0x10;
  static constexpr std::size_t MAX_COMMAND_TAIL = 126;
  /// The first paragraph above the interrupt table, the BIOS data area and the DOS communication area.
  static constexpr std::uint16_t LOWEST_FREE_SEGMENT = 0x0060;

  struct Desc
  {
    std::uint16_t pspSegment = DEFAULT_PSP_SEGMENT;
    std::uint16_t memoryTopSegment = DEFAULT_MEMORY_TOP_SEGMENT;
    std::uint16_t initialAx = 0;
    std::uint16_t initialBx = 0;
    std::string_view commandTail;                    ///< As COMMAND.COM passes it, leading space included: " cheat"
    std::string_view programPath = "C:\\ELITES.EXE"; ///< For the environment
  };

  /// Loads _file. On success fills _program and returns LoadError::None; on failure returns why, having written
  /// nothing.
  [[nodiscard]] static LoadError Load(Memory& _memory, std::span<const std::uint8_t> _file, const Desc& _desc,
                                      LoadedProgram& _program) noexcept;

  /// Replaces the byte at (_program's load segment + _imageSegment):_offset with _replacement if it holds _expected,
  /// and returns whether it did; otherwise it writes nothing. ADR-001's D5 patch is
  /// PatchByte(memory, program, 0x08F4, 0x25E4, 0x00, 0x01): DS:25E4h, the protection's "already shown" flag.
  [[nodiscard]] static bool PatchByte(Memory& _memory, const LoadedProgram& _program, std::uint16_t _imageSegment, std::uint16_t _offset,
                                      std::uint8_t _expected, std::uint8_t _replacement) noexcept;
};

} // namespace Machine
