# Variant stress notes and alt-bot audit

Owner: CosmicCuddle. Coordination #59 comments `6926064554` and `6926719446`.
This note does not certify a native source. `splitVariant` remains CS01's signed-key proof. Stages 8 and above, Titan's Grip, and Darkmoon procs are not decided here. Cards 19287 through 19290 stay unknown.

## Include contract at core 8d6ac43

Repository: `mod-playerbots/azerothcore-wotlk`.
Commit: `8d6ac439eb8a42d028de2b30cb298c24552ba873`.

| Path | Blob |
| --- | --- |
| `src/server/game/Entities/Item/ItemEnchantmentMgr.h` | `2198208b0a4591311d01e14552a333c2a5e5623b` |
| `src/server/game/Entities/Item/ItemEnchantmentMgr.cpp` | `88473a8c82aef037f58df0893d86aad7f58b6405` |
| `src/server/game/Entities/Item/ItemTemplate.h` | `709e3eb491220b12c932bfee1d5563920eb73f69` |
| `src/server/shared/DataStores/DBCStructure.h` | `9a6cc24246fae387766ca0c346394893baef253f` |
| `src/server/shared/DataStores/DBCEnums.h` | `c33b862c76c650fa3a054d6c6c3670a62cc1c05d` |
| `src/common/Common.h` | `d0fe192f119b8fe1c9a1afcdd79f973f5525d409` |
| `src/server/shared/SharedDefines.h` | `502a8a7439ff5816cd24cb3316a572cdbae08de0` |
| `src/common/Utilities/Random.h` | `e6890795798682fa521467873ab9672f1cb61cb8` |
| `src/common/Utilities/Util.h` | `b0bbe0a78499e99034b3e5cdf141541881cd02ae` |
| `src/common/Debugging/Errors.h` | `043065a08341bde27c6ad5fe0a65d0f84cffcdc3` |

`HasItemEnchantMod` is `ItemEnchantmentMgr.cpp` line 84 at that commit. The loader keeps a row only when `chance > 0.000001f && chance <= 100.0f` (line 68). The checked-out `ItemTemplate.h` blob matches the pin. The enchantment header used for the earlier object compile matches blob `2198208b`.

The module side of the contract is `RandomItemMgr::GetEnchantmentPool` in this repository, not a Core blob. `AdaptLoadedGroup` compares that pool with the positive-weight Core rows. It does not call SQL.

## Alt-bot gear entry points at 7b203f4c

Branch `feature/cs01-autogear-rndbot-only-20261116`, commit `7b203f4ce82ec6f007d1ec64398b49d3bc9af1d5`, parent base `f2b1d032`. This audit did not edit that branch.

Gated before any legacy gear write:

- `AutoGearAction::Execute` returns when the bot is missing, `IsAltBot()` is true, or `IsRandomBot` is false.
- `BisGearAction::Execute` uses the same check.
- `ResolveAutoGearRoute` rejects every population other than a known random bot, including when the old alt-allow flag is set and the IP-cap flag is off.
- `AutoMaintenanceOnLevelupAction::AutoUpgradeEquip` already returns unless `IsRandomBot`.

Not gated, and still able to replace gear for a non-random bot:

- `PlayerbotHolder::ProcessBotCommand` in `src/Bot/PlayerbotMgr.cpp` still calls `PlayerbotFactory::Randomize` for an addclass bot on `init=white|green|blue|epic|legendary|auto`, `init=<number>`, `levelup`, and `refresh`. Personal alt bots are refused by the addclass check. Addclass bots are not random bots, and the new route rejects them, but these commands do not.
- The login path in the same file still calls `PlayerbotFactory::Randomize` for an addclass bot whose level is more than 3 away from the master.

`BisGearAction::RunAutogearFallback` still calls `DestroyEquippedGear` and `AutoGear` with no population check of its own. On this commit the only caller is `BisGearAction::Execute`, which returns first. `MaintenanceAction::Execute` does not call `InitEquipment`. For an alt bot it can still call `ApplyEnchantAndGemsNew` when `AltMaintenanceGemsEnchants` is set. That is gems and enchants, not the AutoGear command.

`RandomPlayerbotMgr` and `RandomBotLevelMgr` construct `PlayerbotFactory` on their own random-bot paths. This pass did not treat those as alt-bot entry points.
