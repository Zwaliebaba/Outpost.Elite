#!/usr/bin/env python3
"""Take reference screenshots of the original game running in DOSBox-X (ADR-003 item 1).

  python Tools/ReferenceScreens.py --out DIR [--steps "wait 20; shot title; key space; wait 5; shot launch" | --replay FILE]

ADR-003 checks our interpreter's picture against an independent one before anything is ported against
it. This runs ELITES.EXE in DOSBox-X, headless on a virtual X display, with the same one-byte patch
the host applies in memory (ADR-001, D5) applied to a scratch copy of the file. It then follows a
small script of steps, the format GameLogic/Replay.h defines (steps separated by ';' or a new line,
'#' starting a comment):

  wait N             let N seconds of wall time pass
  key NAME           press and release one key (xdotool key names: F1, space, Escape, Return, a, ...)
  down NAME, up NAME press a key and hold it; release it
  shot NAME          write NAME.png to --out
  digest NAME [HEX]  ignored here: ReferenceRunner prints a state digest, and one script serves both
  file NAME SOURCE   copy SOURCE, a file beside the replay (Replays/ for --steps), onto the mounted drive
                     as NAME, upper case, at that moment: how a replay starts from a prepared commander

A replay (ADR-008) is timed in paced time and DOSBox-X runs in wall time, so a replay played here is
an approximation of the run it records: good for comparing static screens, not for flight. The drive
is mounted with -nocachedir, so that a file a step copies there mid-run is in DOS's directory at once.

Each shot is the emulated screen as a 640x200 image on the CGA's dot grid, the same grid
Machine::Cga renders: DOSBox-X draws the 200-line picture doubled to 400, so every other row is
taken, after checking that each pair of rows is identical. Wall-clock timing makes animated screens
(the title's turning ship, flight) differ from run to run; static ones (the docked screens, the
charts) do not, and two runs give the same bytes.

Development tool only (AGENTS.md R14): it needs dosbox-x, Xvfb, xdotool and ImageMagick from the
system's packages, for example `apt-get install dosbox-x xvfb xdotool imagemagick`.
"""

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REFERENCE_SHA256 = "18b5076a54733dea2d45b1e3b280fd1377067b6cb1b27166d3873aa7e7744363"
# ADR-001 D5: the protection's "already shown" flag, DS:0x25E4, is file offset 0x40 + 0x8F40 + 0x25E4.
PATCH_OFFSET = 0xB564
PATCH_FROM = 0x00
PATCH_TO = 0x01
# DOSBox-X's fixed instructions per emulated millisecond: about a 4.77 MHz 8088's pace, so that `wait N`
# lets roughly N seconds of the original's time pass.
CYCLES_PER_MILLISECOND = 310
WINDOW_WIDTH = 640
WINDOW_HEIGHT = 400
# How DOSBox-X keeps a file's DOS attributes on a host without them: beside NAME, a file of this prefix and
# NAME whose length has bit 0 set when the archive bit is clear, bit 1 for hidden and bit 2 for system
# (localDrive::GetFileAttr and add_special_file_to_disk, DOSBox-X 2024.03.01).
ATTRIBUTE_SIDECAR = ".DBLOCALFILE_ATR_"

# The clock is set the way a PC/XT without a clock card boots, so that the game's reads of the time of
# day (int 21h AH=2Ch, twice in RestartPlay) return the same in every run and in the host. DOS reports
# 3.30, the version the host's DOS emulates and the one current when the game shipped.
CONFIG = """[sdl]
output=surface
[dosbox]
machine=cga
memsize=1
[dos]
ver=3.30
[cpu]
cputype=8086
core=normal
cycles=fixed {cycles}
[mixer]
nosound=true
[speaker]
pcspeaker=false
[autoexec]
mount c "{drive}" -nocachedir
c:
date 01-01-1980
time 00:00:00
{command}
"""


def run(_command: list[str], _env: dict[str, str]) -> subprocess.CompletedProcess:
  return subprocess.run(_command, env=_env, capture_output=True, text=True, check=False)


def is_bare_file_name(_name: str) -> bool:
  """A file name with no directory or drive in it: what a file step may copy, as GameLogic/Replay.cpp checks."""
  return _name not in ("", ".", "..") and not any(character in _name for character in "/\\:")


def patched_copy(_exe: Path, _drive: Path) -> None:
  data = bytearray(_exe.read_bytes())
  digest = hashlib.sha256(data).hexdigest()
  if digest != REFERENCE_SHA256:
    sys.exit(f"{_exe} is not the reference binary (SHA-256 {digest}); see ADR-001")
  if data[PATCH_OFFSET] != PATCH_FROM:
    sys.exit(f"byte at 0x{PATCH_OFFSET:X} is 0x{data[PATCH_OFFSET]:02X}, not 0x{PATCH_FROM:02X}")
  data[PATCH_OFFSET] = PATCH_TO
  (_drive / "ELITES.EXE").write_bytes(data)


def free_display() -> str:
  for number in range(90, 100):
    if not Path(f"/tmp/.X11-unix/X{number}").exists():
      return f":{number}"
  sys.exit("no free X display between :90 and :99")


def find_window(_env: dict[str, str], _timeout_seconds: float) -> str:
  deadline = time.monotonic() + _timeout_seconds
  while time.monotonic() < deadline:
    found = run(["xdotool", "search", "--name", "DOSBox"], _env).stdout.split()
    if found:
      return found[0]
    time.sleep(0.25)
  sys.exit("DOSBox-X opened no window")


def write_png(_target: Path, _width: int, _rows: list[bytes]) -> None:
  def chunk(_kind: bytes, _data: bytes) -> bytes:
    return struct.pack(">I", len(_data)) + _kind + _data + struct.pack(">I", zlib.crc32(_kind + _data))

  header = struct.pack(">IIBBBBB", _width, len(_rows), 8, 2, 0, 0, 0)
  pixels = zlib.compress(b"".join(b"\0" + row for row in _rows), 9)
  _target.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", pixels) + chunk(b"IEND", b""))


def capture(_env: dict[str, str], _window: str, _scratch: Path, _target: Path) -> None:
  raw = _scratch / "window.png"
  result = run(["import", "-window", _window, str(raw)], _env)
  if result.returncode != 0:
    sys.exit(f"capture failed: {result.stderr.strip()}")
  pixels = subprocess.run(["convert", str(raw), "-depth", "8", "rgb:-"], env=_env, capture_output=True, check=False)
  stride = WINDOW_WIDTH * 3
  if pixels.returncode != 0 or len(pixels.stdout) != stride * WINDOW_HEIGHT:
    sys.exit(f"the DOSBox-X window is not {WINDOW_WIDTH}x{WINDOW_HEIGHT}; the shot would not be on the CGA's dot grid")
  rows = [pixels.stdout[line * stride:(line + 1) * stride] for line in range(WINDOW_HEIGHT)]
  # Each of the 200 lines is drawn twice. A pair that differs means the picture is not a doubled
  # 200-line one (a VGA text font, a scaler), and taking every other row would lose half of it.
  for line in range(0, WINDOW_HEIGHT, 2):
    if rows[line] != rows[line + 1]:
      sys.exit(f"window rows {line} and {line + 1} differ; the picture is not 200 lines drawn twice")
  write_png(_target, WINDOW_WIDTH, rows[0::2])


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("--exe", type=Path, default=ROOT / "ELITES.EXE", help="the reference binary")
  parser.add_argument("--out", type=Path, required=True, help="directory for the PNG shots")
  parser.add_argument("--steps", default="wait 20; shot title", help="the script, steps separated by ';'")
  parser.add_argument("--replay", type=Path, help="read the steps from a replay file instead")
  args = parser.parse_args()

  for tool in ("dosbox-x", "Xvfb", "xdotool", "import", "convert"):
    if shutil.which(tool) is None:
      sys.exit(f"{tool} is not installed; see this script's docstring")
  text = args.replay.read_text(encoding="utf-8") if args.replay else args.steps
  lines = (line.split("#", 1)[0] for line in text.splitlines())
  steps = [part.split() for line in lines for part in line.split(";") if part.strip()]
  arguments = {"wait": (2,), "key": (2,), "down": (2,), "up": (2,), "shot": (2,), "digest": (2, 3), "file": (3,)}
  for step in steps:
    bad = f"bad step {' '.join(step)!r}"
    if len(step) not in arguments.get(step[0], ()):
      sys.exit(f"{bad}; steps are wait, key, down, up, shot, digest and file (GameLogic/Replay.h)")
    if step[0] == "file" and not is_bare_file_name(step[2]):
      sys.exit(f"{bad}; a file step copies a file beside the replay, named without a directory")
  sources = args.replay.resolve().parent if args.replay else ROOT / "Replays"
  args.out.mkdir(parents=True, exist_ok=True)

  with tempfile.TemporaryDirectory(prefix="reference-screens-") as scratch:
    drive = Path(scratch) / "c"
    drive.mkdir()
    patched_copy(args.exe, drive)
    config = Path(scratch) / "dosbox.conf"
    config.write_text(CONFIG.format(drive=drive, command="ELITES.EXE", cycles=CYCLES_PER_MILLISECOND), encoding="ascii")

    display = free_display()
    env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER="dummy")
    xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", "1024x768x24"], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    dosbox = None
    try:
      time.sleep(1.0)
      dosbox = subprocess.Popen(["dosbox-x", "-conf", str(config), "-nomenu", "-fastlaunch", "-nopromptfolder"],
                                env=env, cwd=scratch, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
      window = find_window(env, 15.0)
      for verb, argument, *rest in steps:
        if verb == "wait":
          time.sleep(float(argument))
        elif verb in ("key", "down", "up"):
          action = {"key": "key", "down": "keydown", "up": "keyup"}[verb]
          run(["xdotool", action, "--window", window, argument], env)
        elif verb == "digest":
          pass  # only the host can fingerprint its own state; the step is accepted so one script serves both
        elif verb == "file":
          shutil.copyfile(sources / rest[0], drive / argument.upper())
          # DOSBox-X reports a host file with the archive bit set, which the game's loader refuses, unless a
          # sidecar file of odd length says it is clear: what int 21h AX=4301h writes for the game's own save.
          (drive / f"{ATTRIBUTE_SIDECAR}{argument.upper()}").write_bytes(b"\0")
        else:
          target = args.out / f"{argument}.png"
          capture(env, window, Path(scratch), target)
          print(f"wrote {target}")
    finally:
      for process in (dosbox, xvfb):
        if process is not None:
          process.terminate()
          try:
            process.wait(timeout=5)
          except subprocess.TimeoutExpired:
            process.kill()
  return 0


if __name__ == "__main__":
  sys.exit(main())
