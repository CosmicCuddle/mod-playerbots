#!/usr/bin/env python3
"""Compile the exact logout repository-save block against synthetic dependencies."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", default="g++")
parser.add_argument("--sanitizers", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / "src/Bot/PlayerbotMgr.cpp").read_text()
start = source.index("BotRetirementResult PlayerbotHolder::TryLogoutPlayerBot(")
end = source.index("bool PlayerbotHolder::DisablePlayerBot(", start)
logout = source[start:end]
anchor = "        // The old queued cleanup"
assert logout.count(anchor) == 1
start = logout.index(anchor)
end = logout.index("\n\n        LOG_DEBUG", start)
block = logout[start:end]
assert "BotLogoutGroupCleanupOperation" not in logout
assert logout.index("BeforeRepositorySave") < logout.index("TakeForRecovery")
with tempfile.TemporaryDirectory(prefix="pb59-save-order-") as temp:
    work = Path(temp)
    (work / "retirement_save.inc").write_text(block + "\n")
    exe = work / "regression"
    command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(work),
               str(root / "tests/pb59_retirement_save_order.cpp"), "-o", str(exe)]
    if args.sanitizers:
        command.append("-fsanitize=address,undefined")
    subprocess.run(command, check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: 7 exact native repository-save block regression scenarios")
