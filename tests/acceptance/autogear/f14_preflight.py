"""Source-only guard for the disposable stranger-whisper fixture.

Require the exact R1 IP template to explicitly disable social isolation.
This is intentionally stricter than the Core config parser: unknown/defaulted,
quoted, duplicate or environment-overridden values are not accepted. It is
not evidence of the loaded setting in a running worldserver.
"""
from collections.abc import Mapping

KEY = "IndividualProgression.F14.RandomBotSocialIsolation"
TEMPLATE = "modules/individualProgression.conf.dist"
ENV_NORMALIZED = "ACINDIVIDUALPROGRESSIONF14RANDOMBOTSOCIALISOLATION"


def f14_preflight_problems(sources: Mapping[str, str], env: Mapping[str, str]) -> list[str]:
    problems = []
    hits = []
    if TEMPLATE not in sources:
        problems.append("missing exact R1 Individual Progression template")
    for name, text in sources.items():
        for line in text.splitlines():
            line = line.strip()
            if not line or line.startswith(("#", "[")):
                continue
            key, sep, value = line.partition("=")
            if key.strip() == KEY:
                hits.append((name, value.strip() if sep else None))
    if len(hits) != 1 or hits[0] != (TEMPLATE, "0"):
        problems.append("require exactly one explicit F14 isolation = 0 in the R1 template")
    if any(name.replace("_", "").upper() == ENV_NORMALIZED for name in env):
        problems.append("F14 isolation environment override present")
    return problems
