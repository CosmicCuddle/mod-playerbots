#!/usr/bin/env python3
"""Run the exact PB59 signed source fixture in a standalone compiler.

This is a structural signed-key/source copy regression, not a Playerbots,
Worldserver, database, native collector or gameplay test.
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
source = Path(__file__).with_name("pb59_autogear_signed_source.cpp")
headers = root / "src/Bot"
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O1"]
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="pb59-composed-signed-") as directory:
    target = Path(directory) / "test_pb59_signed_source"
    subprocess.run([args.cxx, *flags, "-I", str(headers), str(source), "-o", str(target)], check=True)
    subprocess.run([str(target)], check=True)
