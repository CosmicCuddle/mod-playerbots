#!/usr/bin/env python3
"""Source contract: owner-directed Altbot no-op precedes every AutoGear execution path."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
action = (root / "src/Ai/Base/Actions/TrainerAction.cpp").read_text()
route = (root / "src/Bot/AutoGearCommandRoute.h").read_text()

for func in ("bool AutoGearAction::Execute(Event event)", "bool BisGearAction::Execute(Event event)"):
    start = action.index(func)
    first = action[start:start + 900]
    guard = "botAI->IsAltBot() || !sRandomPlayerbotMgr.IsRandomBot(bot)"
    config = "if (!sPlayerbotAIConfig.AutoGear"
    assert guard in first, func
    assert first.index(guard) < first.index(config), func
    assert "return false;" in first[first.index(guard):first.index(config)], func

assert "request.altCommandAllowed = false;" in action
start = route.index("RouteDecision ResolveAutoGearRoute(")
body = route[start:]
# TierSync deliberately checks its own ipCapEnabled first. Compare against
# the normal command branch's legacy fallback, not the earlier tier-sync gate.
command = body[body.index("if (!request.commandEnabled)"):]
gate = "request.population != Population::RandomBot"
assert gate in command
assert command.index(gate) < command.index("if (!request.ipCapEnabled)")
assert "decision.rejected = true;" in command[command.index(gate):command.index("if (!request.ipCapEnabled)")]
print("AutoGear/BiS and typed route RNDBOT-only source gates: PASS")
