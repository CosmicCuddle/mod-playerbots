"""Synthetic definition of the NotReady (A1) native case. DEFINED, NOT EXECUTED.

It is not in `Runner.cases()` and changes no case count. It states what a later native run must
show, and what it needs before it may run at all.

Capabilities, all required:
- `executor_api`: the build has `PB59_AUTOGEAR_CORE_BATCH`. Without the executor API,
  `PlayerAutoGearInventory::Ready()` returns true, so NotReady can never occur. That fallback is
  legacy and unverified, not native readiness proof, and the case refuses to run on it.
- `quarantine_fixture`: a reviewed, fixture-only way to put one random bot into Core's inventory
  quarantine (`CapturePB59InventoryStamp()` returns nothing). None exists in the R1 tuple.
- `readiness_probe`: a read-only way to confirm the quarantine both before and after the command.

Invariants on a pass:
- exactly one result line, `refusal=10 ran=false stripped=0 new=0 revoked=0`;
- inventory rows, mail count and equipped durability are unchanged;
- no finisher, repair or factory log line;
- the quarantine is still set afterwards: never reset, bypassed or retried.
"""

CASE_ID = "N1 quarantined random bot: AutoGear refused as NotReady"
STATUS = "DEFINED / NOT EXECUTED"
COMMAND = "autogear"
EXPECT = {"refusal": 10, "ran": 0, "unchanged": True}
CAPABILITIES = ("executor_api", "quarantine_fixture", "readiness_probe")
FORBIDDEN_LOG_MARKERS = ("DurabilityRepair", "AutoGear finisher", "PlayerbotFactory")


def blockers(capabilities):
    """Why the case may not run; empty only when every capability is present and proven."""
    problems = []
    for name in CAPABILITIES:
        if capabilities.get(name) is not True:
            problems.append(f"{name} unavailable")
    if capabilities.get("executor_api") is not True:
        problems.append("without the executor API, Ready() is always true: legacy fallback, unverified")
    return problems


def invariant_problems(before, after, quarantined_before, quarantined_after, log_lines):
    """Evidence beyond the result line. `before`/`after` hold inventory rows, mail and durability."""
    problems = []
    if not quarantined_before:
        problems.append("bot was not quarantined before the command; the case proves nothing")
    if not quarantined_after:
        problems.append("quarantine was cleared: reset, bypass or retry")
    for key in ("rows", "mail", "durability"):
        if key not in before or key not in after:
            problems.append(f"{key} evidence missing")
        elif before[key] != after[key]:
            problems.append(f"{key} changed on a NotReady refusal")
    for line in log_lines:
        if any(marker in line for marker in FORBIDDEN_LOG_MARKERS):
            problems.append(f"gear activity logged on a NotReady refusal: {line[:80]}")
    return problems
