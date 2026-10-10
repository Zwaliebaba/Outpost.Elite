// GameLogic/Text.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's text routines, ported (plan §5 Phase 3, ADR-010): printing text and numbers. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> TextEntries() noexcept;

/// DrawViewChar (CS:3130): the 8x8 glyph of AL, in BX on textPaperPattern, at DS:DI in the space-view buffer. Out: DI+2;
/// AX and BP clobbered.
void DrawViewChar(Guest& _guest);

/// DrawViewString (CS:31EC): DrawViewChar for each character of the text at SI. Out: SI at the terminator.
void DrawViewString(Guest& _guest);

/// DrawScreenChar (CS:31F9): the 8x8 glyph of AL, in BX on textPaperPattern, at ES:DI in CGA memory. Out: DI+2; AX
/// and SI clobbered.
void DrawScreenChar(Guest& _guest);

/// DrawScreenString (CS:32D8): DrawScreenChar for each character of the text at SI. Out: SI at the terminator, DI past
/// the text.
void DrawScreenString(Guest& _guest);

/// FormatDecimal5 (CS:3407): AX as five ASCII digits at DS:DI. AX clobbered.
void FormatDecimal5(Guest& _guest);

/// BlankLeadingZeros (CS:3432): up to CL leading characters below '1' at DI become spaces. Out: DI at the first kept
/// character, CH=0.
void BlankLeadingZeros(Guest& _guest);

/// DrawSmallViewChar (CS:349A): the 5-row letter AL ('A'-'Z') in BX at DS:DI, shifted by CL. Out: DI and CL six
/// pixels on.
void DrawSmallViewChar(Guest& _guest);

/// DrawSmallViewString (CS:3527): DrawSmallViewChar for each letter at SI, from DI = (line+1)*256 + x.
void DrawSmallViewString(Guest& _guest);

/// FormatCredits (CS:3543): creditsTenths into creditBalanceText. Out: SI=creditBalanceText.
void FormatCredits(Guest& _guest);

/// UpdateMessageLine (CS:35A3): the message line's work for one frame. Clobbers all.
void UpdateMessageLine(Guest& _guest);

/// ClearMessageLine (CS:3609): zeroes the message line at ES:0058. Clobbers AX, BX, CX, DX, BP, SI, DI.
void ClearMessageLine(Guest& _guest);

/// ShowBountyMessage (CS:3626): shows AX tenths of a credit in bountyText for 20 frames.
void ShowBountyMessage(Guest& _guest);

/// PrintTextModeString (CS:60D2): the text at SI to B800:DI in textAttribute. Out: SI at the NUL, DI past the text,
/// ES=B800; AX clobbered.
void PrintTextModeString(Guest& _guest);

/// ToggleMenuRowHighlight (CS:6328): the swapped textAttribute into 36 attribute bytes at ES:SI. Out: AL.
void ToggleMenuRowHighlight(Guest& _guest);

/// SwapTextAttributeNibbles (CS:6580): textAttribute's nibbles exchanged. Out: AL=textAttribute.
void SwapTextAttributeNibbles(Guest& _guest);

/// PrintCountedTextLines (CS:65FA): a count byte at SI, then that many lines, one a row from DI. Out: SI past the
/// last line; AX and CX clobbered.
void PrintCountedTextLines(Guest& _guest);

/// FormatTenths (CS:69B3): AX tenths into priceText as "nnnn.n".
void FormatTenths(Guest& _guest);

/// ToggleInputCursor (CS:7727): the input cursor's character flips between blank and block.
void ToggleInputCursor(Guest& _guest);

/// RedrawInputLine (CS:773A): the BX characters at SI, then the cursor, printed for the layout. Preserves BX, SI, DI.
void RedrawInputLine(Guest& _guest);

/// PrintStringForLayout (CS:7750): PrintTextModeString in the text layout, DrawScreenString otherwise.
void PrintStringForLayout(Guest& _guest);

} // namespace Elite
