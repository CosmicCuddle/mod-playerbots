"""Database identity and privilege allowlist: matches pass, anything unknown or unverified refuses."""
import copy
import json
import tempfile
import unittest
from pathlib import Path

from db_identity import db_identity_problems, grant_scope_problems, load_expected, probe, redact

MYSQL = {"flavor": "mysql", "server_uuid": "3e11fa47-71ca-11e1-9e33-c80aa9429562", "hostname": "fixture",
         "port": "3307", "datadir": "/srv/agacc/data/", "server_id": "1", "account": "agacc@localhost",
         "grants": ["GRANT USAGE ON *.* TO `agacc`@`localhost`",
                    "GRANT ALL PRIVILEGES ON `agacc1\\_%`.* TO `agacc`@`localhost`"],
         "isolation_evidence": "dedicated mysqld, own datadir"}
MARIADB = dict(MYSQL, flavor="mariadb", server_uuid=None)


def rows_for(identity, version="8.0.36", uuid_fails=False, grants=None, identity_row=None):
    def rows(statement):
        if statement.startswith("SELECT @@version"):
            return identity_row if identity_row is not None else [[
                version, identity["hostname"], identity["port"], identity["datadir"], identity["server_id"],
                identity["account"]]]
        if statement == "SELECT @@server_uuid":
            if uuid_fails:
                raise OSError("Unknown system variable 'server_uuid'")
            return [[identity["server_uuid"]]]
        return [[g] for g in (grants if grants is not None else identity["grants"])]
    return rows


def problems(expected, **kw):
    return db_identity_problems(probe(rows_for(expected, **kw)), expected, "agacc1")


class DbIdentityTest(unittest.TestCase):
    def test_recorded_mysql_instance_passes(self):
        self.assertEqual(problems(MYSQL), [])

    def test_recorded_mariadb_instance_passes_without_uuid(self):
        self.assertEqual(problems(MARIADB, version="10.11.6-MariaDB", uuid_fails=True), [])

    def test_mysql_without_uuid_refuses(self):
        self.assertTrue(problems(MYSQL, uuid_fails=True))

    def test_flavor_mismatch_refuses(self):
        self.assertTrue(problems(MYSQL, version="10.11.6-MariaDB"))
        self.assertTrue(problems(MARIADB, uuid_fails=True))

    def test_each_identity_field_mismatch_refuses(self):
        for key in ("server_uuid", "hostname", "port", "datadir", "server_id", "account"):
            with self.subTest(key=key):
                live = dict(MYSQL, **{key: "other"})
                self.assertTrue(db_identity_problems(probe(rows_for(live)), MYSQL, "agacc1"))

    def test_loopback_tunnel_to_another_server_refuses(self):
        # Same loopback address, different server behind it: only the recorded identity decides.
        tunnel = dict(MYSQL, server_uuid="aaaaaaaa-0000-0000-0000-000000000000", hostname="shared-db")
        self.assertTrue(db_identity_problems(probe(rows_for(tunnel)), MYSQL, "agacc1"))

    def test_unexpected_identity_shape_raises(self):
        with self.assertRaises(ValueError):
            probe(rows_for(MYSQL, identity_row=[]))

    def test_extra_or_missing_grant_refuses(self):
        extra = MYSQL["grants"] + ["GRANT SELECT ON `agacc1\\_x`.* TO `agacc`@`localhost`"]
        self.assertTrue(problems(MYSQL, grants=extra))
        self.assertTrue(problems(MYSQL, grants=MYSQL["grants"][:1]))

    def test_grants_outside_the_prefix_refuse_even_if_allowlisted(self):
        for grant in ("GRANT ALL PRIVILEGES ON *.* TO `agacc`@`localhost`",
                      "GRANT SELECT ON `acore_characters`.* TO `agacc`@`localhost`",
                      "GRANT ALL PRIVILEGES ON `agacc%`.* TO `agacc`@`localhost`",
                      "GRANT ALL PRIVILEGES ON `agacc1_%`.* TO `agacc`@`localhost`",
                      "GRANT PROXY ON ''@'' TO `agacc`@`localhost`",
                      "GRANT `admin` TO `agacc`@`localhost`"):
            with self.subTest(grant=grant):
                expected = dict(MYSQL, grants=MYSQL["grants"] + [grant])
                self.assertTrue(problems(expected))
                self.assertTrue(grant_scope_problems([grant], "agacc1"))

    def test_password_material_is_redacted(self):
        line = "GRANT USAGE ON *.* TO 'agacc'@'localhost' IDENTIFIED BY PASSWORD '*ABCDEF0123'"
        self.assertNotIn("ABCDEF", redact(line))
        live = dict(MYSQL, grants=MYSQL["grants"] + [line])
        report = db_identity_problems(probe(rows_for(live)), MYSQL, "agacc1")
        self.assertTrue(report)
        self.assertNotIn("ABCDEF", json.dumps(report))

    def test_expected_identity_file_is_strict(self):
        bad = [
            {k: v for k, v in MYSQL.items() if k != "datadir"},
            dict(MYSQL, flavor="postgres"),
            dict(MYSQL, server_uuid=None),
            dict(MARIADB, server_uuid="x"),
            dict(MYSQL, isolation_evidence=" "),
            dict(MYSQL, grants=[]),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "identity.json"
            for data in bad:
                with self.subTest(data=data):
                    path.write_text(json.dumps(data))
                    with self.assertRaises(ValueError):
                        load_expected(path)
            path.write_text(json.dumps(MYSQL))
            self.assertEqual(load_expected(path), copy.deepcopy(MYSQL))


if __name__ == "__main__":
    unittest.main()
