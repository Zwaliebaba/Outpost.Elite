#!/usr/bin/env python3
"""Record the reference's boot trace in DOSBox-X's debugger, for comparison with the host's (ADR-003).

  python Tools/ReferenceTrace.py --dosbox PATH --out FILE [--until-offset 0x7616] [--limit N]

ADR-003 compares the host's interpreter, instruction by instruction, with an independent emulator from
the program's entry to its first keyboard read. This produces the independent half. It starts
ELITES.EXE with DEBUGBOX in a DOSBox-X built with its heavy debugger (`./build-debug`, which configures
`--enable-debug=heavy`; the distributions' packages leave the debugger out). The D5 byte is applied to a
scratch copy, and the machine is the one Tools/ReferenceScreens.py uses. DEBUGBOX stops at the entry
point, and the debugger's LOGL command then logs every instruction. Its log file is a named pipe, read
as it is written, so the multi-gigabyte text log never reaches the disk.

The output is a trace file: the 8 bytes "XTRACE1\\0", then one record per instruction of fourteen
little-endian 16-bit words, the state before the instruction executes:

  CS IP AX BX CX DX SI DI BP SP DS ES SS FLAGS

ReferenceRunner writes the same format, and Tools/CompareTrace.py compares two of them. Every
instruction DOSBox-X executes is recorded, including those of its own BIOS and DOS handlers in segment
F000, which the host does not execute; the comparison sets those aside.

The trace ends just before the first instruction at --until-offset in the entry code segment
(default 0x7616, GetKey, Design/Symbols.tsv), or after --limit instructions.

Development tool only (AGENTS.md R14): it needs that DOSBox-X build, Xvfb and a POSIX pseudo-terminal.
"""

import argparse
import os
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ReferenceScreens  # noqa: E402  (the shared machine configuration and the patched copy)

MAGIC = b"XTRACE1\0"
RECORD = struct.Struct("<14H")
LINE = re.compile(r"^([0-9A-F]{4}):([0-9A-F]{8}) .*? EAX:([0-9A-F]{8}) EBX:([0-9A-F]{8}) ECX:([0-9A-F]{8}) "
                  r"EDX:([0-9A-F]{8}) ESI:([0-9A-F]{8}) EDI:([0-9A-F]{8}) EBP:([0-9A-F]{8}) ESP:([0-9A-F]{8}) "
                  r"DS:([0-9A-F]{4}) ES:([0-9A-F]{4}) FS:[0-9A-F]{4} GS:[0-9A-F]{4} SS:([0-9A-F]{4}) "
                  r"CF:([01]) ZF:([01]) SF:([01]) OF:([01]) AF:([01]) PF:([01]) .*FLG:([0-9A-F]{8})")
# DOSBox-X evaluates the arithmetic flags lazily, and FLG prints its raw flags word, in which they can be
# stale. The CF..PF fields are evaluated; DF, IF and TF are stored directly and are right in FLG. The
# 8088 reads bits 12-15 and bit 1 as one.
ARITHMETIC_FLAGS = ((0x0001, 14), (0x0040, 15), (0x0080, 16), (0x0800, 17), (0x0010, 18), (0x0004, 19))
DIRECT_FLAGS = 0x0700
FIXED_ONES = 0xF002
# What the debugger prints at its command prompt, once DEBUGBOX has stopped at the entry point.
PROMPT = b"->"


def read_log(_pipe: Path, _out: Path, _entry_ip: int, _until_offset: int, _limit: int, _done: threading.Event,
             _result: dict) -> None:
  count = 0
  entry_cs = None
  with open(_pipe, "r", encoding="ascii", errors="replace") as log, open(_out, "wb") as out:
    out.write(MAGIC)
    for line in log:
      match = LINE.match(line)
      if match is None:
        continue
      cs, ip = int(match.group(1), 16), int(match.group(2), 16) & 0xFFFF
      if entry_cs is None:
        if ip != _entry_ip:
          _result["error"] = f"the log starts at {cs:04X}:{ip:04X}, not at the entry point"
          break
        entry_cs = cs
      if (cs == entry_cs and ip == _until_offset) or count == _limit:
        _result["stopped_at"] = f"{cs:04X}:{ip:04X}"
        break
      words = [cs, ip] + [int(match.group(index), 16) & 0xFFFF for index in range(3, 11)]
      flags = (int(match.group(20), 16) & DIRECT_FLAGS) | FIXED_ONES
      for bit, group in ARITHMETIC_FLAGS:
        flags |= bit if match.group(group) == "1" else 0
      words += [int(match.group(index), 16) for index in range(11, 14)] + [flags]
      out.write(RECORD.pack(*words))
      count += 1
  _result["count"] = count
  _result["entry"] = f"{entry_cs:04X}:0000" if entry_cs is not None else None
  _done.set()


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("--dosbox", type=Path, required=True, help="a DOSBox-X built with --enable-debug=heavy")
  parser.add_argument("--exe", type=Path, default=ReferenceScreens.ROOT / "ELITES.EXE", help="the reference binary")
  parser.add_argument("--out", type=Path, required=True, help="the trace file to write")
  parser.add_argument("--until-offset", type=lambda text: int(text, 0), default=0x7616,
                      help="stop before this IP in the entry code segment (default 0x7616, GetKey)")
  parser.add_argument("--limit", type=int, default=20_000_000, help="stop after this many instructions")
  parser.add_argument("--cycles", type=int, default=ReferenceScreens.CYCLES_PER_MILLISECOND,
                      help="DOSBox-X's fixed cycles; a different value moves where the timer interrupts land")
  parser.add_argument("--timeout", type=float, default=600.0, help="give up after this many seconds")
  args = parser.parse_args()

  with tempfile.TemporaryDirectory(prefix="reference-trace-") as scratch:
    drive = Path(scratch) / "c"
    drive.mkdir()
    ReferenceScreens.patched_copy(args.exe, drive)
    config = Path(scratch) / "dosbox.conf"
    config.write_text(ReferenceScreens.CONFIG.format(drive=drive, command="DEBUGBOX ELITES.EXE", cycles=args.cycles),
                      encoding="ascii")
    pipe = Path(scratch) / "LOGCPU.TXT"  # where LOGL writes, relative to DOSBox-X's working directory
    os.mkfifo(pipe)

    display = ReferenceScreens.free_display()
    xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", "1024x768x24"], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    pid = -1
    try:
      time.sleep(1.0)
      pid, terminal = pty.fork()
      if pid == 0:
        os.environ.update(DISPLAY=display, SDL_AUDIODRIVER="dummy", TERM="xterm")
        os.chdir(scratch)
        os.execv(str(args.dosbox),
                 [str(args.dosbox), "-conf", str(config), "-nomenu", "-fastlaunch", "-nopromptfolder"])

      deadline = time.monotonic() + args.timeout
      screen = b""
      while PROMPT not in screen:
        if time.monotonic() > deadline:
          sys.exit("the debugger never stopped at the entry point")
        ready, _, _ = select.select([terminal], [], [], 0.2)
        if ready:
          screen = (screen + os.read(terminal, 65536))[-4096:]
      time.sleep(0.5)

      done = threading.Event()
      result: dict = {}
      entry_ip = struct.unpack_from("<H", args.exe.read_bytes(), 0x14)[0]  # the MZ header's initial IP
      reader = threading.Thread(target=read_log, daemon=True,
                                args=(pipe, args.out, entry_ip, args.until_offset, args.limit, done, result))
      reader.start()
      os.write(terminal, f"LOGL {args.limit + 1:X}\r".encode("ascii"))
      while not done.wait(0.2):
        if time.monotonic() > deadline:
          sys.exit("timed out before the trace ended")
        ready, _, _ = select.select([terminal], [], [], 0)
        if ready:
          os.read(terminal, 65536)  # keep the terminal drained so the debugger never blocks on it
      if "error" in result:
        sys.exit(result["error"])
      print(f"{result['count']} instructions from {result['entry']} to {result.get('stopped_at', 'the limit')}, "
            f"written to {args.out}")
    finally:
      if pid > 0:
        os.kill(pid, signal.SIGKILL)  # DOSBox-X ignores SIGTERM while the debugger holds it
        os.waitpid(pid, 0)
      xvfb.terminate()  # SIGTERM lets Xvfb remove its socket and lock file; SIGKILL would leave the display taken
      try:
        xvfb.wait(timeout=5)
      except subprocess.TimeoutExpired:
        xvfb.kill()
        xvfb.wait()
  return 0


if __name__ == "__main__":
  sys.exit(main())
