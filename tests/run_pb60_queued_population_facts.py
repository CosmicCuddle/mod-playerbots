#!/usr/bin/env python3
"""Extract the exact queued identity facts from RunPB59AutoGearWorldPlan and compile them with mocks.
No database, server or Core tree needed. Not native proof."""
import argparse
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", default="g++")
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--pb", type=Path, default=Path(__file__).resolve().parent.parent)
args = parser.parse_args()
pb = args.pb
trainer = (pb / "src/Ai/Base/Actions/TrainerAction.cpp").read_text()
plan = trainer[trainer.index("bool RunPB59AutoGearWorldPlan("):]
start = "    PlayerbotsAutoGearExec::CommandFacts facts;\n"
# Through the (possibly altered) randomAccount line, so a changed identity read is compiled, not skipped.
end = re.compile(r"^    facts\.randomAccount = [^\n]*;\n", re.M)
if plan.count(start) != 1:
    raise SystemExit("FAIL: queued facts block not found exactly once")
first = plan.index(start)
match = end.search(plan, first)
if not match or plan.index("ResolveCommand(", first) < match.start():
    raise SystemExit("FAIL: randomAccount fact is not read before ResolveCommand")
excerpt = plan[first:match.end()]
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d / "facts.inc").write_text(excerpt)
    shutil.copyfile(Path(__file__).resolve().parent / "pb60_queued_population_facts.cpp", d / "fixture.cpp")
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", f"-I{pb / 'src/Bot'}", f"-I{d}"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    subprocess.run([args.cxx, *flags, str(d / "fixture.cpp"), "-o", str(d / "test")], check=True)
    raise SystemExit(subprocess.run([str(d / "test")]).returncode)
