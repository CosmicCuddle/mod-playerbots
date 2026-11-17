#!/usr/bin/env python3
"""Compile the exact factory planning guard and actual installed set solver.

The program never creates, destroys, or equips a bot item. It is not a
PlayerbotFactory.cpp/full Worldserver build or gameplay test.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", default="g++")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()

root = Path(__file__).resolve().parents[1]
source = Path(__file__).with_name("pb59_autogear_factory_seam.cpp")
headers = root / "src/Bot"
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="pb59-factory-seam-") as temp:
    executable = Path(temp) / "test_pb59_autogear_factory_seam"
    subprocess.run([args.cxx, *flags, "-I", str(headers),
                    str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
