# Fixture preflight limits: build provenance, database identity, port races

CS01 request [#80 7215821131](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/80#issuecomment-7215821131).
This is an audit of the preflight added in PR69 (`preflight.py`, `install_identity.py`), read against the runner.
Nothing here is implemented yet. Each limit says what the preflight proves today, how it can be fooled, and the
smallest proposed check.

## 1. Build provenance

**What it proves today:**
- The install's binaries read module configs from this install's `etc/` (compiled `CONF_DIR` from `CMakeCache.txt`).
- The installed binaries are the build tree's binaries (equal ELF build-IDs).
- Core and each module listed in `--expect-tuple` are clean git checkouts at the expected SHA.

**Gaps:**

| Gap | How it can be fooled | Proposed check |
|---|---|---|
| Build tree to source | The build tree may have been compiled from a different checkout, or before the checkout moved. A build-ID binds binary to build tree, not to source. | Compare the Core revision Core embeds in the binary (`revision_data.h`; `strings` shows the 12-character short hash, e.g. `c779e7b4f735`) with the expected Core SHA. Refuse a mismatch or a missing value. |
| Stale revision stamp | `revision_data.h` is regenerated only at configure. A rebuild after a checkout without `cmake .` keeps the old hash. In this session the binary still said `c779e7b4f735` after the tree moved to Core88. | Also require every object to be newer than its source? That's too heavy. Instead require a fresh configure-and-build log as an operator artefact, and record its SHA-256. |
| Modules | Modules carry no revision stamp in the binary. | Record the module trees' HEADs at build time into a build manifest (an operator step), and compare that manifest with `--expect-tuple`. Until then, module provenance stays operator-attested. |
| rsync-deployed module | A module folder copied with rsync keeps another commit's `.git`. The current check reports it as dirty or the wrong HEAD, which is correct fail-closed behaviour. | None needed. |

## 2. Database endpoint identity and privileges

**What it proves today:**
- The host string is `127.0.0.1` or `localhost`.
- `SHOW DATABASES` lists only system schemas.

**Gaps:**

| Gap | How it can be fooled | Proposed check |
|---|---|---|
| A local address isn't a local instance | `127.0.0.1:<port>` can be an SSH or socat tunnel to a shared or remote server. | Read `@@hostname`, `@@port`, `@@datadir` and `@@server_uuid`. Require `@@server_uuid` to equal a value the operator records when they create the dedicated instance (`--expect-server-uuid`). |
| A restricted user sees an "empty" instance | `SHOW DATABASES` lists only schemas the user can see. A user with narrow grants on a shared server sees nothing and passes. | `SHOW GRANTS FOR CURRENT_USER()` must show global `ALL PRIVILEGES ON *.*`, so the listing is complete. Without that, refuse. Combine with the server UUID. |
| An over-privileged user | The same global grant lets a mistake reach any schema. | That's acceptable only because the instance is dedicated, so the server UUID check is mandatory with it. |
| Race between preflight and provisioning | Another client creates `agacc*_*` between the check and the dry-run. | `provision()` already re-runs `fresh_database_names` just before writing. The window that remains is milliseconds, and the dry-run would then update the other database, so keep the instance dedicated (UUID check). |

## 3. Port races

**What it proves today:** nothing accepts connections on `127.0.0.1:<auth>` or `<world>` at preflight time.

**Gaps:**

| Gap | How it can be fooled | Proposed check |
|---|---|---|
| Time of check vs time of use | Another process binds the port after preflight. Our server then fails to bind and exits. `start()` already refuses an early exit, so that's fail-closed. | None needed for this half. |
| A wrong server answers | If another worldserver or authserver is already listening on a different interface, or binds after ours fails to start but before the client connects, the client could talk to that server. The worst case is commands sent to a foreign server. | After `start()`, confirm the listening socket belongs to our process: match the listening inode in `/proc/net/tcp` to an fd of our child PID. Compare the realmlist port with the world port as well. Refuse before any client connects. |
| Interfaces other than 127.0.0.1 | The check only probes 127.0.0.1. A server bound to `0.0.0.0` is caught, but one bound to another local address isn't. | The fixture binds 127.0.0.1 (`BindIP`), and the ownership check above covers this. |

## Recommendation

Add three checks in a later test-only child:
1. the Core revision stamp against the expected Core SHA;
2. `@@server_uuid` against `--expect-server-uuid`, plus global grants;
3. after start, ownership of the listening sockets by our server PIDs.

Each check refuses before any write or client connection. Module provenance stays operator-attested until there's a
build manifest. None of this needs a native run to unit-test: the inputs are strings, grant rows and `/proc` text.
