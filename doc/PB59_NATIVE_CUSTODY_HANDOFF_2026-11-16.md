# PB59 native custody handoff — development draft

Owner: CosmicCuddle. Coordination #51. Parent PB e52523fee1d97c48075c4f8432f6f9c2735e3f15; required Core child ad02036c8908e232bb5397a4dcbab1ed25556170. Do not merge or deploy this branch. The known-good all-module build remains Core39/PB36/IP43c35580/CSf43bc2c9.

## Implemented
- Explicit world update owner binding before timed retirement, pending login and queued dispatch. Queued login refuses unbound/foreign context.
- Pending session remains unique owner until exact holder entry and timer slot are installed; preallocated PublishedSessionOwner reservation receives the actual unique_ptr. No raw release on that publication path.
- Mixed query completion pump uses WithPublished ownership borrow, not ordinary equipment admission. Recheck exact identity after callback.
- Native TryLogoutPlayerBot reserves exclusive published retirement permission before journal mutation; transfers ownership into custody before holder unlink; native logout must return detached before deletion. Operation ID captured before journal move for diagnostics.
- AI-only disable refuses an exact registry-owned headless session; real/non-headless legacy behavior unchanged.
- Pending fixture uses real registry and extracts actual native custody tail; tests normal completion, publication loss, callback exceptions, native logout veto and throw retaining independent custody. Timed source contracts updated.

## Evidence and commands
Exact-source ordering checks run through repository contents: reserve/insert/register/publish; permit before mutation; custody before unlink; logout before detached delete; completion borrow; world owner binding; real registry fixture. These are source checks, not compilation.

Run on a complete checkout of this branch and its paired Core:
```bash
python3 tests/run_pb59_pending_login.py --cxx g++
python3 tests/run_pb59_pending_login.py --cxx clang++ --sanitizers
python3 tests/run_pb59_timed_retirement.py --core /path/to/paired-core --cxx g++
g++ -std=c++20 -Wall -Wextra -Werror -pthread -Isrc/Bot tests/pb59_published_session_owner.cpp -o /tmp/pb59-owner && /tmp/pb59-owner
```
Full native build, exact repository fixture execution and gameplay NOT yet verified at this checkpoint.

## Blocking release gates
1. Audit all headless ownership/destruction routes, including Core WorldSession destructor/direct logout, map worker WorldSession::Update, direct login producers, shutdown, RandomPlayerbotFactory and cluster/reconnect. Registry ownership alone does not pin Player or prevent unrelated native raw deletion. Do not add a destructor veto that leaves an attached Player in a destroyed session.
2. Foreign-context AddPlayerBot/retirement producers must dispatch immutable exact intent to the world owner; refusing foreign logout is not a substitute for preserving accepted command intent. Verify existing battleground/map producers and personal/master logout and selfbot paths.
3. Explicit recovery/cleanup for incomplete post-publication initialization, direct Core detach, lost holder, shutdown and sticky recovery; process-lifetime registry is not restart durability. Native packet/teleport/AI callback lifetime and master/group reacquisition still need review. Confirm headless disable semantics before release.
4. Group cleanup queue acceptance is not execution/DB acknowledgement. Need exact completion/drop/reconciliation and durable save/login ordering.
5. Compose exact branches, run focused fixtures and full static all-module build, then staged gameplay/regression/fault injection. Preserve previous tuple. No feature activation, production changes, SQL or upstream sync.

Rollback for development: keep this isolated child unmerged; use its parent e52523fe for the prior callback draft and the known-good Core39/PB36 tuple for compatibility evidence. Do not roll back a live session ownership change by swapping binaries while sessions remain attached; no live deployment authorized.
