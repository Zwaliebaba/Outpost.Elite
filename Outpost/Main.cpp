#include "pch.h"

/*
 * The composition root, and for now nothing else.
 *
 * `Outpost.vcxproj` is `ConfigurationType=Application` and the template it came from compiles
 * only `pch.cpp`, so until this file existed the project had NO ENTRY POINT AT ALL. Building the
 * solution therefore failed at the link step with
 *
 *     LNK2019: unresolved external symbol WinMain referenced in function
 *              "int __cdecl invoke_main(void)"
 *
 * which is the C runtime looking for the entry the Windows subsystem requires and finding none.
 * Nothing in the port needed the executable, so nothing noticed: CI builds
 * `Tests\GameLogicTests\GameLogicTests.vcxproj` BY NAME rather than building the solution, for
 * the reasons the workflow's own comment gives. A person opening the solution in Visual Studio
 * and pressing Build hits it immediately, which is how it was found.
 *
 * BOTH ENTRY POINTS ARE DEFINED, and that is deliberate rather than belt-and-braces. Which one
 * the runtime wants depends on the linker's default entry symbol for `/SUBSYSTEM:WINDOWS`, and
 * that default is `WinMainCRTStartup` -- the NARROW one -- unless something sets `/ENTRY`
 * otherwise, even in a project built as Unicode. The reported error names `WinMain`, so this
 * toolchain takes the narrow path; a different Visual Studio, or a package that sets the entry
 * symbol, would take the other. Defining both costs a few lines and removes the question
 * entirely. The unused one is never called and never linked out of anything.
 *
 * WHAT THIS DOES NOT DO. It does not open a window, create a device, or run the game. That is
 * slice 2e, which needs a machine to run on; the cadence question it also waited on is settled
 * and its cycle counter is built. `GameLogic` is complete enough to drive -- the universe, the
 * charts, all four docked screens, the commander and its save format -- and none of it is
 * reachable from here yet ON PURPOSE. A shell that launches and shows nothing is harder to reason
 * about than one that does not launch, and this file exists to make the solution LINK rather than
 * to make the game run.
 */

namespace
{
/// What both entry points do, so the two below cannot drift apart. Slice 2e replaces the body
/// with the window, the presenter and the game loop.
int RunPlaceholder() noexcept
{
  return 0;
}
} // namespace

int APIENTRY wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
  return RunPlaceholder();
}

int APIENTRY WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
  return RunPlaceholder();
}
