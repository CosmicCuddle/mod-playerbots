#!/usr/bin/env python3
"""Read-only source/SQL text guard for four *unapplied* newer Playerbots updates.

Tests work exclusively on repository files. There is no database, connection,
migration executor, SQL execution, account access, or production-side effect.
Source baseline: CosmicCuddle/mod-playerbots@405f81196161017eebae3ee7a612d786a3ed2a9d.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SQL = ROOT / "data" / "sql" / "playerbots" / "updates"

CRAFT = "2026_10_06_00_ai_playerbot_craft_link_texts.sql"
SPANISH = "2026_10_16_00_ai_playerbot_spanish_es-MX_texts.sql"
CALC = "2026_10_22_00_ai_playerbot_calc_breakdown_text.sql"
ROLES = "2026_10_27_00_ai_playerbot_class_role_texts.sql"

ROLE_NAMES = (
    "class_role_assistant",
    "class_role_preferred_spec_bot",
    "class_role_bot",
    "class_role_none",
    "class_role_handover",
    "class_role_krosh_mage_tank",
    "class_role_kiggler_moonkin_tank",
    "class_role_leotheras_warlock_tank",
    "class_role_capernian_warlock_tank",
    "class_role_sanguinar_debuff_hunter",
    "class_role_class_mage",
    "class_role_class_balance_druid",
    "class_role_class_warlock",
    "class_role_class_demonology_warlock",
    "class_role_class_hunter",
)
EXPECTED = {
    CRAFT: {1830: "craft_usage", 1945: "craft_no_professions"},
    CALC: {1929: "calc_item_breakdown"},
    ROLES: {1930 + i: name for i, name in enumerate(ROLE_NAMES)},
}

# Each Spanish translation must touch ONLY one existing text_loc7 value at
# exactly one numeric ID. The SQL file currently uses one UPDATE per line.
SPANISH_UPDATE = re.compile(
    r"^\s*UPDATE\s+\x60ai_playerbot_texts\x60\s+SET\s+"
    r"\x60text_loc7\x60='(?:[^']|'')*'\s+WHERE\s+\x60id\x60=(\d+);\s*$",
    re.IGNORECASE,
)
INSERT_TEXT_ROW = re.compile(
    r"\(\s*(\d{1,5})\s*,\s*'((?:craft_|calc_|class_role_)[a-z0-9_]+)'",
    re.IGNORECASE,
)
DML_TABLE = re.compile(
    r"\b(?:DELETE\s+FROM|INSERT\s+INTO|UPDATE)\s+\x60?([a-z_]+)\x60?",
    re.IGNORECASE,
)


class NewerPlayerbotsSQLScope(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.docs = {name: (SQL / name).read_text(encoding="utf-8")
                    for name in (CRAFT, SPANISH, CALC, ROLES)}

    def test_no_ddl_or_other_tables(self):
        for name, source in self.docs.items():
            with self.subTest(name=name):
                self.assertNotRegex(
                    source, r"\b(?:CREATE|ALTER|DROP|TRUNCATE)\s+TABLE\b"
                )
                self.assertNotRegex(source, r"\bREPLACE\s+INTO\b")
                written = set(DML_TABLE.findall(source))
                expected_tables = (
                    {"ai_playerbot_texts"} if name == SPANISH
                    else {"ai_playerbot_texts", "ai_playerbot_texts_chance"}
                )
                self.assertEqual(written, expected_tables)

    def test_locale_scope_exactly_1739_ids_and_one_column(self):
        source = self.docs[SPANISH]
        lines = [line for line in source.splitlines()
                 if line.lstrip().upper().startswith("UPDATE ")]
        self.assertEqual(len(lines), 1739)
        identifiers = []
        for line in lines:
            match = SPANISH_UPDATE.fullmatch(line)
            self.assertIsNotNone(match, "Unexpected UPDATE shape: " + line[:140])
            identifiers.append(int(match.group(1)))
        self.assertEqual(len(set(identifiers)), 1739)
        self.assertEqual(set(identifiers), set(range(1, 1740)))
        self.assertNotRegex(source, r"\b(?:INSERT|DELETE|REPLACE|TRUNCATE)\b")

    def test_fixed_numeric_text_ids_and_names(self):
        all_ids = {}
        for name, expected in EXPECTED.items():
            with self.subTest(name=name):
                found = {}
                for id_text, text_name in INSERT_TEXT_ROW.findall(self.docs[name]):
                    ident = int(id_text)
                    self.assertNotIn(ident, found, "duplicate inserted ID")
                    found[ident] = text_name
                self.assertEqual(found, expected)
                for ident, label in found.items():
                    self.assertNotIn(ident, all_ids, "cross-file fixed ID reuse")
                    all_ids[ident] = label
        self.assertEqual(sorted(all_ids),
                         [1830] + list(range(1929, 1946)))

    def test_named_replacements_are_explicit_not_table_wipes(self):
        for name, expected in EXPECTED.items():
            source = self.docs[name]
            with self.subTest(name=name):
                for table in ("ai_playerbot_texts", "ai_playerbot_texts_chance"):
                    self.assertRegex(
                        source,
                        rf"(?is)DELETE\s+FROM\s+\x60?{table}\x60?\s+WHERE\s+"
                        r"\x60?name\x60?\s*(?:IN\s*\(|=)",
                    )
                self.assertEqual(
                    len(re.findall(r"\bDELETE\s+FROM\b", source, re.I)), 2
                )
                self.assertEqual(
                    len(re.findall(r"\bINSERT\s+INTO\b", source, re.I)), 2
                )
                for label in expected.values():
                    self.assertGreaterEqual(source.count("'" + label + "'"), 2)

    def test_source_only_never_connects_to_database(self):
        # The test itself only calls pathlib read_text and regex checks.
        # NO SQL is executed and these flags are not release permission.
        self.assertTrue(SQL.is_dir())
        self.assertEqual(set(self.docs), {CRAFT, SPANISH, CALC, ROLES})


if __name__ == "__main__":
    unittest.main()
