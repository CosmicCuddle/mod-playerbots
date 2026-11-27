# Acceptance harness: secret redaction audit

CS01 [#80 7216999038](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/80#issuecomment-7216999038) item 2.
The only secret the harness holds is the fixture MySQL password. The game account passwords are fixed fixture
constants for disposable accounts. This audit traces the MySQL password from input to every place it can go.

| Path | Before | Now |
|---|---|---|
| Runner argv (`--mysql host;port;user;password`) | visible to every local user in `/proc/<pid>/cmdline` and `ps` | `--mysql host;port;user`; a fourth field is refused. The password comes from `--mysql-password-file`: a regular file (no symlink), owned by the user, mode 600, one line |
| `mysql` child argv (`-p<password>`) in `Fixture.sql` and the world SQL step | visible in `ps`; a `CalledProcessError` from `check=True` printed the full command, password included, in the traceback | every call goes through `mysql_client.run`, using `--defaults-file=<private option file>`. The argv has no secret. The 0600 file sits in a 0700 temp directory and is deleted when the call ends, including on failure, timeout or a missing client |
| `MYSQL_PWD` | set by the PR77 preflight query, and inherited from the operator's shell | never set, and removed from every `mysql` child's environment |
| Errors | `CalledProcessError` text, including argv | `MysqlError` gives the return code and the last stderr line, scrubbed of the password, with no cause chained. A timeout reports only its duration |
| Timeouts | none on `Fixture.sql`, so a hung server stalled the run | every call has one: 600 s by default, 60 s for preflight, 3600 s for world SQL |
| Credential object | a dict holding the password | `Credentials` with `__slots__`; `repr` and `str` redact the password |
| Preflight output, refusal text, final report | `json.dumps` of collected text | each is scrubbed of the password before printing or writing |
| Build manifest and DB identity files | not secret-bearing | the identity file holds grants, not passwords. Grant lines are redacted (`db_identity.redact`) |

**Residual risks, unchanged in this scope:**
- **The servers' environment.** Core reads database credentials only from its config or `AC_*_DATABASE_INFO`, so
  the authserver and worldserver environments hold the password. On Linux that's readable only by the same user
  and root, through `/proc/<pid>/environ`. The runner never prints that environment. Removing the password from
  it would need a Core change.
- **Server logs.** The runner scrubs its own report, including `Errors.log` lines copied into it. It doesn't
  rewrite the server log files themselves. Core doesn't log the password in its connection lines, but this has
  only been checked by reading the source.
- **Option file lifetime.** The file exists on disk while each `mysql` call runs. Same user only.
- **Not executed natively.** All of this is tested with fakes. No real MySQL, server or credential was used.
