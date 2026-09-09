<!--
  One slice per pull request (Design/Platform-Build.md §0). Delete a section only if it is
  genuinely empty; "nothing outstanding" is worth a line, an absent heading is not.
  AGENTS.md §8 is the hand-back checklist and is NOT repeated here -- run it.
-->

## What this changes

<!--
  Prose, not a list of files. What the code does now that it did not, and why that is the right
  answer -- the same argument the commit messages make. A reader who never opens the diff should
  finish this section knowing what moved and what it cost.
-->

## Departures from the design or the plan

<!--
  Anything you built differently from Design/*.md, and the reason. A departure is not a
  confession: the plan is written ahead of the tree and the tree is what is true. What IS
  required is that the document be amended in this pull request and the slice's journal entry
  say so -- code and Design/ must not be left disagreeing (AGENTS.md §7).

  Write "none" if there were none.
-->

## Verification

<!--
  Fill the right-hand column. Say which configurations you actually BUILT, not which ones exist.
-->

| | |
|---|---|
| `Tests/PortableRunner/run_tests.sh` | N passed, 0 failed |
| `python tools/check_all.py` | 13 of 13 |
| `python tools/mutate.py --unit X --runner portable` | which units, or not run and why |
| Ratchets in `tools/modernize_ratchet.json` | which moved, which were raised, and the journalled reason for any raise |
| Marked counts (`python tools/check_counts.py`) | agree |
| Configurations built | Debug x64, Release x64, or which |

<!--
  A new test earns its place by failing. Say how you know each one does: which mutation you
  planted, or which recorded mutant covers it. A test that cannot fail is worse than no test,
  because it is counted.
-->

## What is not verified here

<!--
  The honest half, and the one most worth writing.

  The Ubuntu leg does not compile Outpost/Window.cpp, ScreenPresenter.cpp, Shell.cpp, Main.cpp or
  SoundOutput.cpp -- only the five files EXECUTABLE_SOURCES names. If the change touches any of
  those, the Windows job is the only witness that it BUILDS and nothing on either leg can see what
  it LOOKS LIKE. Say so plainly rather than letting a green tick imply otherwise.

  Also name any digest that was re-recorded, and what makes the new value right.
-->

## Outstanding, and whose

<!--
  What this pull request does not finish, and who it is waiting on. A gate that says "play:" is
  the owner's: the beep lengths, the cadence, the legibility, PresentMon's numbers, a look at the
  screen on a real panel. List them so they are not mistaken for done.
-->
