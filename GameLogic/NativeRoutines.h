// GameLogic/NativeRoutines.h
#pragma once

namespace Elite
{

class GameState;
class Platform;

/// Puts the native game where the reference's code was (ADR-010): each entry the platform can reach, with its native body.
/// Those are Start, where DOS starts the program, which becomes the platform's entry, and the timer's and the keyboard's
/// interrupt handlers, which serve int 8 and int 9 once the game installs them (Platform::SetGameHandler, ADR-041); every other
/// routine is called as a value from them (ADR-012). _state is the game's memory, loaded at the segments
/// DOS would have put the reference at (Game::Load), and _platform the devices; the routines run on both, so both must outlive
/// the running program.
void InstallNativeRoutines(Platform& _platform, GameState& _state);

} // namespace Elite
