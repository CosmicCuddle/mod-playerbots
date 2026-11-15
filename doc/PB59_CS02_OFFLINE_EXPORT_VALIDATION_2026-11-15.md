# PB59 CS02 offline export validation

Owner: CosmicCuddle. Coordination #59 work order `6902772558`, task 6.
The owner will supply `acore_world` and `Spell.csv` later. This spec checks those files offline. It does not query a live database, and it does not hardcode a Darkmoon best-in-slot list.

`AutoGearExportCheck.h` implements the verdicts below on a caller-filled snapshot. Tests use synthetic ids only.

## Provenance the export must carry

- Schema name present. A missing name is Unknown.
- Checksum known and equal to the checksum published with the file. A mismatch is Invalid. Unknown checksum is Unknown.
- Version known. Unknown version is Unknown.
- Row counts for `item_template` and the spell file, both expected and observed. A count of zero expected is Unknown. A mismatch is Invalid.

## Tables and joins

| Source | What is checked | Missing evidence |
| --- | --- | --- |
| `acore_world.item_template` | Row count. Entry, item level, required level, Quality, InventoryType, class and subclass, bonding, MaxCount, spellid_1..5, spelltrigger_1..5, itemset, stat and resistance columns. | Any absent column or row the selector needs is Unknown, not a default item. |
| `acore_world` item limit category | Category id, HAVE count, EQUIP count. | A single undifferentiated limit is not accepted as both. |
| `Spell.csv` | Header begins with `Id`. Ids are non-zero and unique. Row count matches the header's file. | A referenced item spell id that is not in this file is Unknown. |
| Item spell join | Each non-zero `spellid_N` on a selected item exists in `Spell.csv`. | Unknown. Do not invent a proc chance, amount, or cooldown. |
| Proc and on-equip text | Taken only from the spell row once it is present. | Unknown until the spell row exists. Tooltip text is not a substitute. |
| Override and source eligibility | Owner columns for availability and source, when the export includes them. | Unknown. A loaded template is not availability. |

## Verdicts

- `Unknown`: checksum, version, schema, expected counts, spell header, or a referenced spell is missing. Selection preserves.
- `Invalid`: row count mismatch, checksum mismatch, zero spell id, or a duplicate spell id. Selection preserves.
- `Accepted`: the snapshot's counts, checksum, header, and joins agree. This still does not score an item. Scoring waits for the existing calculator and for installed evidence of procs.

Darkmoon cards stay Unknown for availability and procs until those spell rows are in an accepted export. Item level 66, required level 60, and unique trinket may be recorded as base fields from coordination #64 comment `6900198011`. They are not a forced set.

## What this spec will not do tomorrow

It will not import the file, write SQL, or turn the feature on. A human compares the published checksum with the file that arrived, fills `ExportSnapshot`, and runs `tests/pb59_autogear_export_check.cpp` against a synthetic stand-in first. The real file is checked with the same function only after the owner hands it over.
