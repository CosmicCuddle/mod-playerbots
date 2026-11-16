# PB59 CS02 offline export validation

Owner: CosmicCuddle. Coordination #59 work order `6902772558`, task 6.
The owner will supply `acore_world` and `Spell.csv` later. This spec checks those files offline. It does not query a live database, and it does not hardcode a Darkmoon best-in-slot list.

`AutoGearExportCheck.h` implements the verdicts below on a caller-filled snapshot. Tests use synthetic ids only.

## Provenance the export must carry

- Schema name present. A missing name is Unknown.
- SHA-256 of the file bytes, hashed as a stream of chunks, equal to the digest published with that file. The world file and the spell file are hashed separately. A digest copied from the other file does not pass. A nonpositive spell record id is Invalid. A negative item-spell reference is not interpreted as a spell id.
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

- `Unknown`: digest, version, schema, expected counts, spell header, override, proc, source evidence, or a referenced spell is missing. Selection preserves.
- `Invalid`: row count mismatch, digest mismatch, a negative spell reference, an empty item-spell list when spells are required, or an unsorted id index. Selection preserves.
- `Bounded`: the id window is larger than the stated bound. It is not scanned.
- `MetadataChecked`: the two manifests agree. `ExportIsProductionReady` is always false. This is not a production gear source.
- `ReadError`: the stream failed. A clean end of file is not this verdict.

## Reader

`AutoGearExportReader.h` and `apps/pb59/export_reader.cpp` read a file in 64-byte steps. Each step is hashed with OpenSSL `EVP_sha256` and parsed before the next step. The byte limit is checked before the addition, so a short read cannot wrap an unsigned counter. The CLI parses manifest numbers without `std::stoul`.

Accepted text is CSV with a header, quoted commas, and quoted newlines; a normalized id list beginning `# pb59-normalized 1`; or a mysqldump fragment. `ConvertDump` recognizes statements and does not execute them. Schema, `CREATE`, and comments are skipped. An extended `INSERT` contributes every tuple. Trailing junk on an id, a repeated id, or a manifest value that is not a whole 32-bit number is rejected.

Darkmoon cards stay Unknown for availability and procs until those spell rows are in an accepted export. Item level 66, required level 60, and unique trinket may be recorded as base fields from coordination #64 comment `6900198011`. They are not a forced set.

## What this spec will not do tomorrow

It will not import the file, write SQL, or turn the feature on. A human compares the published checksum with the file that arrived, fills `ExportSnapshot`, and runs `tests/pb59_autogear_export_check.cpp` against a synthetic stand-in first. The real file is checked with the same function only after the owner hands it over.
