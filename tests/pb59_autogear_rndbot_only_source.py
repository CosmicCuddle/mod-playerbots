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
gate = "request.population != Population::RandomBot"
assert gate in body
assert body.index(gate) < body.index("if (!request.ipCapEnabled)")
assert "decision.rejected = true;" in body[body.index(gate):body.index("if (!request.ipCapEnabled)")]
print("AutoGear/BiS and typed route RNDBOT-only source gates: PASS")
