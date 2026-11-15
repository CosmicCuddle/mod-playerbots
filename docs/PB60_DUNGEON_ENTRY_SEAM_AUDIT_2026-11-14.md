# PB60 bot dungeon bracket: entry seam audit

**Status:** source audit and unmerged PB entry-policy candidate on PB `3a9e39f1` and Core `21ff1973`. No live behavior or data changed. Tracked in [coordination Issue #60](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/60).

## Entry paths and common policy point

| Route | Source path | Core check |
| --- | --- | --- |
| Normal portal/area trigger | Core `Handlers/MiscHandler.cpp`, area trigger | `MapMgr::PlayerCannotEnter(targetMap, player, false)` |
| PB direct teleport, summon, meeting stone, RPG stuck relocation | PB `UseMeetingStoneAction.cpp`, `NewRpgBaseAction.cpp`, other `TeleportTo` call sites | `Player::TeleportTo` calls `OnPlayerBeforeTeleport`, then `PlayerCannotEnter` for far cross-map teleport |
| Group/command teleport | PB `PlayerbotOperations.h`, command and movement actions; Core spell/command callers | Same `Player::TeleportTo` path when far cross-map |
| LFG teleport | PB `LfgTeleportAction.cpp` queues `CMSG_LFG_TELEPORT` | Core LFG teleport eventually calls `Player::TeleportTo` |
| Login/reconnect already inside instance | Core `PlayerStorage.cpp::CanEnterMap` | `MapMgr::PlayerCannotEnter(currentMap, player, true)` |

`MapMgr::PlayerCannotEnter` checks map metadata and calls `OnPlayerCanEnterMap(player, entry, instance, mapDiff, loginCheck)` for dungeons. This is the closest shared bot-only policy seam for ordinary entry **and** login. It runs before the existing raid, LFG map, and other Core checks; a successful bracket check must leave those checks and IP era restrictions intact. The candidate now registers `PLAYERHOOK_CAN_ENTER_MAP` alongside the existing `PLAYERHOOK_ON_BEFORE_TELEPORT`.

For an **ordinary dungeon entrance without LFG**, PB's `AreaTriggerAction` sends `CMSG_AREATRIGGER` to Core's area-trigger handler. Core calls `PlayerCannotEnter` before teleporting, which invokes the new PB `OnPlayerCanEnterMap` hook. The resolver receives selected dungeon ID `0` and uses matching physical dungeon rows by destination map and difficulty. The current candidate always supplies `0` because Core's mutable LFG group-state read is not verified as safe here. LFG membership is only needed to disambiguate exact selected rows or seasonal exceptions; it is not required for normal entrance enforcement.

The single hook does not cover every shortcut: Core returns early for Game Masters before `OnPlayerCanEnterMap`, and `Player::TeleportTo` skips `PlayerCannotEnter` with `TELE_TO_GM_MODE`. Same-map teleports may also skip `PlayerCannotEnter`; the candidate calls the same policy from `OnPlayerBeforeTeleport` for same-map, GM-mode, or GM-player destinations. Normal cross-map teleports use `OnPlayerCanEnterMap`, avoiding duplicate denials. Login still uses the map-entry hook. `Playerbots.EnforceDungeonLevelBracket` defaults OFF until installed data and staged behavior have been reviewed.

## Level source

Core `LFGDungeonEntry` has `MinLevel`, `MaxLevel`, `MapID`, `Difficulty`, and seasonal flags. `LFGMgr::LFGDungeonData` copies those fields. `MapEntry` and `MapDifficulty` do not provide a min/max dungeon bracket. The checked-in `lfgdungeons_dbc.sql` is a schema-only snapshot; the actual server DBC/overrides and map-to-dungeon rows are absent from this checkout. A map can have several LFG rows, including difficulty and event variants, so choosing a range by map ID alone would be unsafe. The exact installed data coverage and any gaps need a read-only inventory before activating the candidate or approving a further override table.

The candidate intersects each nonzero Core `access_requirement` bound with the physical LFG row: the higher minimum and lower maximum win. A missing LFG bound can use an explicit Core bound, but an inverted intersection denies. Without any matching physical LFG row, the resolver denies even when Core has both bounds; that combination needs an explicit reviewed policy after installed-data inventory. An exact selected dungeon ID must identify agreeing physical rows; conflicting duplicates deny. An unmatched selected ID falls back to the physical map/difficulty rows, which keeps ordinary portal entry independent of an unrelated LFG selection. The installed DBC and override rows still need a read-only inventory before activation.

## Holiday exception

Core `LFGMgr::IsSeasonActive(dungeonId)` maps four named seasonal dungeon IDs to their active holidays. The exception must require both an exact seasonal dungeon identity and an active event. Some seasonal content shares a physical map with ordinary dungeons; an active holiday must not exempt every entry into that map. The LFG group's selected dungeon ID may identify the event route. For a non-LFG portal/summon into a shared map, the current hook arguments identify the map but not necessarily the event encounter. Such an entry should not receive a holiday bypass without an authoritative discriminator.

Threading remains unresolved: Core `LFGMgr::GetDungeon` reads `GroupsStore[guid]` without a visible lock and may insert on a miss. The PB hook no longer calls it. Core's own `PlayerCannotEnter` may still reach that path later for LFG groups, which is outside this candidate. Exact selected-row and seasonal identity handling needs a safe accessor or snapshot before use. Human-controller notification also needs staged validation across map and world threads. The candidate remains OFF pending installed data and staged gameplay.

## Implementation and acceptance still required

1. Inventory the installed LFG/DBC rows by map, difficulty, min/max, and seasonal status; compare to accessible non-raid instance maps. Decide a reviewed override only for missing or unsuitable data.
2. Implement one bot-only, read-only policy helper used by map-entry and teleport hooks. Classify bots during login without assuming their AI object has already been attached. Return a clear denial to the controlling player where a controller exists.
3. Keep event bypass scoped to an identified seasonal dungeon while its holiday is active. Preserve Core and IP checks after a bracket pass.
4. Test in-range, under/over-level, human, normal portal, summon, command, LFG, reconnect inside instance, active/inactive holiday, shared holiday map, raid exclusion, and IP era denial. Native build, gameplay tests, installed DBC inventory, and deployment have not run in this workspace.
