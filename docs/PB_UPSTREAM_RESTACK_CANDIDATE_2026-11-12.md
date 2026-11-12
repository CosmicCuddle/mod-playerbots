# Playerbots current-upstream F14 restack candidate — 12 November 2026

Owner: CosmicCuddle
Status: **DRAFT / DEVELOPMENT ONLY — no merge or deployment authority**

## Exact ancestry

- shared base: `e88cc4099e69f35126b28942a412ca702d20453b`
- protected local PB15: `4b355ea3a8fd00a0b116681bf9db4c7a3eb0e051`
- current upstream: `405f81196161017eebae3ee7a612d786a3ed2a9d`

GitHub's direct PB15 -> current-upstream merge analysis is conflict-bearing. This candidate therefore resolves the conflict deliberately on top of current upstream instead of taking either side wholesale.

## Population adaptation

The current upstream scheduler owns `RandomBotCountMode`, scheduled/variable targets and `RemoveRandomBots()`. The F14 manual Vanilla BE/Draenei hold remains default OFF.

When ON, one world-thread snapshot:
- starts from exact `currentBots.size()`;
- excludes only positively verified type-1 BE/Draenei held GUIDs;
- retains those GUIDs and saved add events;
- provides the same effective scheduled count to the outer add/remove decision and private Add/Remove capacity arithmetic;
- excludes the same held GUID set from effective online login capacity and from downsize faction occupancy;
- preserves the raw online count for upstream operational behavior such as no-real-player logout-all.

Ordinary upstream non-removable bots still occupy their faction share and keep all existing safety checks.

## Config preservation

Current upstream count modes and validation are retained. Removed periodic-online/offline settings are not restored. CosmicCuddle's reset-marker mask and three F14 options remain additive/default OFF.

## Evidence boundary

The new workflow is source + pure C++ model only. It is not a full Worldserver compile, startup, gameplay proof, installed-owner configuration inventory or production release. Full combined native compile/link and controlled gameplay remain mandatory.
