# ADR-047 — The game waits as a coroutine: no native thread

**Status:** accepted 2026-10-14, with the change that implements it, the first part of step 7 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's ruling D77.

## Context

Since ADR-010 item 8 the game has run on a thread of its own, the native thread, which handed the platform back to the host through two semaphores whenever a wait reached the end of a run (`Platform::RunUntil`), and an exception unwound it when the platform was destroyed under it. D77 ruled that the game waits by C++20 coroutines instead: a function that waits is a coroutine, a wait is a `co_await`, and the host resumes the game, on one thread.

## Decision

1. **`Task<T>`** (`GameLogic/Task.h`) is the game's coroutine: lazy, awaited once, resuming the routine that awaited it by symmetric transfer when it finishes, and throwing again where it is awaited an exception it ended with. No other coroutine type exists.
2. **A routine that waits is a `Task`.** The 59 routines that reach a wait, from `Start` and `GameLoop` down to `WaitForTimerTick`, `AwaitNextKey`, `WaitForRetrace` and `PresentSpaceView`, return `Task<T>` and `co_await` every routine of theirs that waits; the docked screens' table holds `Task<ScreenKey>` functions. The routines that do not wait, and the interrupt handlers, are unchanged and run to their end.
3. **The game suspends only where a run ends.** `Hardware::Wait` moves the clock to the next device event, as before, and gives an awaiter that is ready unless the clock reached the run's end; then the game suspends there and `RunUntil` returns, and the next `RunUntil` resumes it. `Spend` is a `Task` that suspends the same way at each step that reaches the end. Spinning never moves the clock, so it never suspends, and stays a plain call. The interrupts due are taken when a wait goes on, as they were, so every interrupt lands where it did.
4. **The platform holds the program's coroutine** (`m_program`) and the wait it is suspended in. Destroying the platform destroys the coroutine and, with it, every routine it was waiting in: their locals' destructors run, as the abandoned thread's unwinding ran them. A stop inside a service or an interrupt leaves the coroutines as `ProgramStopped`, which `RunUntil` catches where the program's coroutine ends.
5. **No `co_await` inside an operand of `?:`.** Two such expressions, `buy ? co_await BuyCargo(...) : co_await SellCargo(...)` and the shipyard's like it, did not run the purchase under g++, and eight tests failed; whether the fault is the compiler's was not pursued. They are written as `if`/`else`, and no conditional operand in the tree waits.
6. **The coroutine protocol's names keep the standard's spelling** (R4): `promise_type`, `await_ready` and the rest are exempt from `readability-identifier-naming` by `IgnoredRegexp`, by name and nothing else.

## What moved, and how it was measured

Nothing. Every digest, picture and outcome of the corpus and the twins is as recorded: the suite passes in check mode, 178 tests, built with g++ and with clang++ 18 (`Tools/BuildPortable.py`), with no re-base and no recording. The platform's own tests drive the program as coroutines: a wait that reaches the end of a run carries on in the next, a destroyed platform destroys the program mid-wait, and a spin is stopped.

The whole suite took 3.5 s of wall time under g++ where it took 8.3 s before, by `time` over one run of each build on the same machine.

| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |
|---|---|

## What this forecloses

- **A second thread for the game.** It runs on the host's thread, resumed by `RunUntil`.
- **A wait outside a coroutine.** A routine that waits returns a `Task`; `[[nodiscard]]` on every `Task` and on the wait's awaiter makes a wait nobody awaits a compile error.
