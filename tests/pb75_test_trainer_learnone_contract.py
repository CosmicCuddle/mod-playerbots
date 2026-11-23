#!/usr/bin/env python3
"""Source contract for "trainer learnone" in TrainerAction.cpp (coordination #75, comment 7079993298).

The parser is tested on its own (pb75_trainer_learnone.cpp). The action calls Core (Trainer, Player),
which needs a running server, so these checks pin what the action source does: learnone is decided
before the existing parse, refuses before any change, checks trainer, spell and eligibility, and
learns through the existing Learn() exactly once. Each rule also runs against deliberately broken
copies (negative controls). This does not prove in-game behaviour.
"""
from pathlib import Path
import sys


def body(source, signature):
    if source.count(signature) != 1:
        raise RuntimeError(f"expected exactly one: {signature}")
    pos = source.index(signature)
    depth = 0
    for i in range(source.index("{", pos), len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[pos:i + 1]
    raise RuntimeError(f"unterminated: {signature}")


def ordered(text, *needles):
    pos = -1
    for needle in needles:
        found = text.find(needle, pos + 1)
        if found < 0:
            return False
        pos = found
    return True


def check(source):
    errors = []

    def rule(ok, text):
        if not ok:
            errors.append(text)

    execute = body(source, "bool TrainerAction::Execute(Event event)")
    one = body(source, "bool TrainerAction::LearnOne(")
    iterate = body(source, "void TrainerAction::Iterate(Creature* creature, bool learnSpells, uint32 spellId)")

    rule(ordered(execute, "ParseLearnOne(param)", "!= PlayerbotsTrainerLearnOne::Parse::NotLearnOne",
                 "return LearnOne(", "chat->parseSpell(param)", "Iterate("),
         "learnone is decided and returned before the existing parse and loop")
    rule(ordered(one, "!= PlayerbotsTrainerLearnOne::Parse::Spell", "return false;",
                 "IsTrainerValidForPlayer(bot)", "return false;",
                 "GetSpell(one.spellId)", "if (!spellInfo)", "return false;",
                 "IsTrainerSpellAllowedForBot(", "CanTeachSpell(bot, trainerSpell)", "return false;",
                 "SpellsTaught(spellInfo->Id, effects)", "if (taught.empty())", "return false;",
                 "< cost)", "return false;",
                 "bot->CastSpell(bot, spellInfo->Id, true);", "bot->learnSpell(spellInfo->Id, false);",
                 "AllKnown(taught,", "return false;",
                 "bot->ModifyMoney(-static_cast<int32>(cost));", "\" - learned\"", "return true;"),
         "learnone refuses bad input, trainer, spell, eligibility, an unverifiable result and the price; "
         "then learns, confirms the taught spells, and only then charges and reports success")
    rule("effect.Effect == SPELL_EFFECT_LEARN_SPELL, effect.TriggerSpell" in one,
         "the taught spells come from the spell's learn effects")
    rule(one.count("ModifyMoney(") == 1 and one.count("CastSpell(") == 1 and one.count("learnSpell(") == 1,
         "learnone charges once and learns once")
    rule(one.count("return true;") == 1, "learnone reports success in one place")
    for banned in ("Iterate(", "Learn(spellInfo", "GetSpells()", "parseSpell("):
        rule(banned not in one, f"learnone never calls {banned}")
    rule("if (spellId && trainerSpell->SpellId != spellId)" in iterate, "the existing loop is unchanged")
    rule(execute.count("return LearnOne(") == 1, "one learnone entry")
    return errors


def mutants(source):
    one = body(source, "bool TrainerAction::LearnOne(")
    yield "no early return", source.replace("return LearnOne(target, trainer, one);", "LearnOne(target, trainer, one);", 1)
    yield "no trainer check", source.replace(one, one.replace("!trainer->IsTrainerValidForPlayer(bot)", "false", 1), 1)
    yield "no teach check", source.replace(one, one.replace("!trainer->CanTeachSpell(bot, trainerSpell)", "false", 1), 1)
    yield "teach all", source.replace(one, one.replace("bot->learnSpell(spellInfo->Id, false);",
                                                       "Iterate(creature, true, 0);", 1), 1)
    yield "bad argument learns", source.replace(one, one.replace(
        "if (one.status != PlayerbotsTrainerLearnOne::Parse::Spell)", "if (false)", 1), 1)
    yield "parse after old path", source.replace(
        "PlayerbotsTrainerLearnOne::Result const one = PlayerbotsTrainerLearnOne::ParseLearnOne(param);",
        "PlayerbotsTrainerLearnOne::Result const one{};", 1)
    yield "old Learn path", source.replace(one, one.replace("bot->learnSpell(spellInfo->Id, false);",
                                                            "Learn(spellInfo, cost, out);", 1), 1)
    yield "charge before learning", source.replace(one, one.replace(
        "    if (spellInfo->HasEffect(SPELL_EFFECT_LEARN_SPELL))\n",
        "    bot->ModifyMoney(-static_cast<int32>(cost));\n    if (spellInfo->HasEffect(SPELL_EFFECT_LEARN_SPELL))\n", 1), 1)
    yield "no confirmation", source.replace(one, one.replace(
        "if (!PlayerbotsTrainerLearnOne::AllKnown(taught, [&](uint32 spell) { return bot->HasSpell(spell); }))",
        "if (false)", 1), 1)
    yield "trainer spell checked instead of taught", source.replace(one, one.replace(
        "effect.Effect == SPELL_EFFECT_LEARN_SPELL, effect.TriggerSpell", "false, 0", 1), 1)
    yield "too expensive succeeds", source.replace(one, one.replace(
        "\"I cannot spare the money for spell %spell. Nothing learned.\", names));\n        return false;",
        "\"I cannot spare the money for spell %spell. Nothing learned.\", names));\n        return true;", 1), 1)
    yield "loop filter removed", source.replace("if (spellId && trainerSpell->SpellId != spellId)",
                                                "if (false && trainerSpell->SpellId != spellId)", 1)


def main():
    root = Path(__file__).resolve().parents[1]
    source = (root / "src/Ai/Base/Actions/TrainerAction.cpp").read_text(encoding="utf-8")
    failures = 0
    errors = check(source)
    for error in errors:
        print(f"FAIL {error}")
    failures += len(errors)
    caught = total = 0
    for name, broken in mutants(source):
        total += 1
        if broken == source:
            print(f"FAIL negative control '{name}' did not change the source")
            failures += 1
        elif check(broken):
            caught += 1
        else:
            print(f"FAIL negative control '{name}' was not caught")
            failures += 1
    print(f"contract rules pass on the real source: {not errors}; negative controls caught {caught}/{total}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
