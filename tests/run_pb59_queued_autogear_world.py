#!/usr/bin/env python3
"""Extract exact native PB/Core/IP methods; compile with mocked game objects.
Requires the three development worktrees. Does not contact a database or server.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--core", type=Path, required=True)
parser.add_argument("--ip", type=Path, required=True)
parser.add_argument("--cxx", default="g++")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
pb = Path(__file__).resolve().parent.parent

def excerpt(path, start, end):
    source = path.read_text()
    first = source.index(start)
    last = source.index(end, first)
    return source[first:last].rstrip() + "\n"

with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    shutil.copyfile(pb / "src/Bot/AutoGearQueuedCommand.h", d / "AutoGearQueuedCommand.h")
    shutil.copyfile(pb / "tests/pb59_queued_autogear_world.cpp", d / "fixture.cpp")
    (d / "world.inc").write_text(excerpt(pb / "src/Bot/PlayerbotAI.cpp",
        "namespace\n{\n    PB59::CommandOwner CapturePB59CommandOwner",
        "void PlayerbotAI::HandleCommands()"))
    (d / "core_provider.inc").write_text(excerpt(
        args.core / "src/server/game/Scripting/ScriptDefines/PlayerScript.cpp",
        "bool ScriptMgr::TryReadPB59AutoGearProgression", "void ScriptMgr::OnPlayerDelete"))
    (d / "ip_provider.inc").write_text(excerpt(args.ip / "src/IndividualProgression.cpp",
        "bool IndividualProgression::TryReadPB59AutoGearProgression", "bool IndividualProgression::hasPassedProgression"))
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run([args.cxx, *flags, str(d / "fixture.cpp"), "-o", str(d / "test")], check=True)
    subprocess.run([str(d / "test")], check=True, timeout=30)
