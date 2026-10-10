// Machine/NativeFirmware.h
#pragma once

namespace Machine
{

class Pc;

/// The ROM's two hardware interrupt handlers (Firmware), as native routines for a Pc's Dispatcher
/// (ADR-011): the BIOS timer handler at F000:E000 and the keyboard handler at F000:E040. They run when
/// the program has put the BIOS's vectors back, around a disk operation or a screenshot, and an interrupt
/// falls due before it installs its own again. RestoreTimerInterrupt reprograms the PIT, which raises
/// IRQ 0 at once, so the timer handler does run there.
///
/// Each does what the ROM code does, in the same order: every byte it writes, the stack included, every
/// port it reads and writes, the registers and flags it leaves, and the 8088 cycles the instruction
/// clock counts (Pc::CountInstructionCycles), as the interpreter charged them. The timer handler's int 1Ch reaches
/// the ROM's IRET; if the program had hooked int 1Ch it would be called as native code calls an
/// interrupt (Pc::CallInterrupt).
///
/// Pc hooks them itself when it powers on.
void HookNativeFirmware(Pc& _pc);

} // namespace Machine
