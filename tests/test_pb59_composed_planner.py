#!/usr/bin/env python3
"""Compile/run PB59 composed planner proof fixtures from this exact checkout.

Standalone C++20 header tests only. Passing does not establish a native
Playerbots/Worldserver build, truthful item source, safe item custody or gameplay.
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path


SOURCES = (
    "pb59_autogear_selection_plan.cpp",
    "pb59_autogear_variant_solver.cpp",
    "pb59_autogear_cp23_known_defects.cpp",
    "pb59_autogear_candidate_collector.cpp",
    "pb59_autogear_fact_source.cpp",
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++", help="C++20 compiler (e.g., g++ or clang++)")
    parser.add_argument("--sanitize", action="store_true", help="Build and run with ASan/UBSan")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent.parent
    src = repo / "src" / "Bot"
    tests = repo / "tests"
    if not src.is_dir():
        print(f"FAIL missing source directory: {src}", file=sys.stderr)
        return 1

    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(src)]
    if args.sanitize:
        flags.extend(("-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"))

    total = 0
    with tempfile.TemporaryDirectory(prefix="pb59-composed-") as work:
        for number, name in enumerate(SOURCES, 1):
            source = tests / name
            if not source.is_file():
                print(f"FAIL missing fixture: {source}", file=sys.stderr)
                return 1
            executable = Path(work) / f"pb59_{number}"
            build = [args.cxx, *flags, str(source), "-o", str(executable)]
            try:
                compiled = subprocess.run(build, text=True, capture_output=True, timeout=120, check=False)
                if compiled.returncode:
                    print(f"FAIL compile {name} rc={compiled.returncode}", file=sys.stderr)
                    print(compiled.stdout + compiled.stderr, file=sys.stderr)
                    return 1
                executed = subprocess.run([str(executable)], text=True, capture_output=True, timeout=120, check=False)
            except (OSError, subprocess.TimeoutExpired) as error:
                print(f"FAIL {name}: {error}", file=sys.stderr)
                return 1
            if executed.returncode:
                print(f"FAIL runtime {name} rc={executed.returncode}", file=sys.stderr)
                print(executed.stdout + executed.stderr, file=sys.stderr)
                return 1
            # Keep the published check count auditable; a zero-check binary isn't a pass.
            matches = re.findall(r"(?m)^checks=(\d+)\s*$", executed.stdout)
            if len(matches) != 1 or int(matches[0]) < 1:
                print(f"FAIL missing or invalid checks counter in {name}", file=sys.stderr)
                print(executed.stdout + executed.stderr, file=sys.stderr)
                return 1
            checks = int(matches[0])
            total += checks
            print(f"PASS {name} checks={checks}")
    print(f"PB59_COMPOSED_CXX=PASS compiler={args.cxx} sanitized={args.sanitize} suites={len(SOURCES)} checks={total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
