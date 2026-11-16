#include "AutoGearExportReader.h"
#include "AutoGearSelectionPlan.h"
#include "AutoGearWholeSetScore.h"
#include <array>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace PlayerbotsAutoGearSelection;
using PlayerbotsAutoGearWeapons::Kind;

namespace
{
int g_checks = 0;

bool Check(bool condition, char const* text)
{
    ++g_checks;
    if (!condition)
    {
        std::cerr << "FAIL " << text << "\n";
        return false;
    }
    return true;
}

ItemFact Piece(std::uint32_t id, std::int32_t property, std::uint8_t slot)
{
    ItemFact fact;
    fact.id = id;
    fact.randomProperty = property;
    fact.itemLevel = 60;
    fact.requiredLevel = 1;
    fact.slot = slot;
    fact.armorSubtype = slot == 4 ? 1 : 0;
    fact.sourceKnown = true;
    fact.metadataKnown = true;
    fact.equipKnown = true;
    fact.equipAllowed = true;
    fact.setEvidenceKnown = true;
    fact.kind = Kind::Count;
    return fact;
}

PlanInput Mage()
{
    PlanInput in;
    in.cap = CapResult{false, 66};
    in.bot.levelKnown = true;
    in.bot.level = 60;
    in.bot.classKnown = true;
    in.bot.classId = 8;
    in.bot.specKnown = true;
    in.bot.specTab = 1;
    in.bot.roleKnown = true;
    in.bot.role = Role::Caster;
    in.bot.armorKnown = true;
    in.bot.armorMask = 1u << 1;
    in.bot.weaponsKnown = true;
    in.authorityValid = true;
    in.bags = BagSpace::Enough;
    in.candidateBudget = 128;
    in.senderEpoch = 1;
    in.senderIpRevision = 2;
    in.senderEquipmentRevision = 3;
    in.botEpoch = 4;
    in.botEquipmentRevision = 5;
    in.sessionIncarnation = 6;
    in.botIncarnation = 7;
    in.policyRevision = 8;
    return in;
}

std::uint32_t Score(std::uint32_t id, std::int32_t, std::uint32_t)
{
    return id;
}
}

int main()
{
    bool ok = true;
    std::array<std::uint8_t, 8> bytes{{1, 2, 3, 4, 5, 6, 7, 8}};
    std::array<DeclaredChunk, 5> five{};
    for (std::size_t i = 0; i < five.size(); ++i)
    {
        five[i].bytes = std::span<std::uint8_t const>(bytes.data() + i, 1);
        five[i].declaredBytes = 1;
    }
    FileChunks many;
    many.chunks = five;
    many.declaredCount = 5;
    ChunkHash const hashed = HashFile(many);
    Sha256Digest const whole = HashSha256(std::span<std::uint8_t const>(bytes.data(), 5));
    ok = Check(hashed.fault == ChunkFault::None && hashed.bytesRead == 5 && hashed.digest == whole,
            "more than four chunks are hashed") &&
         ok;
    many.declaredCount = 4;
    ok = Check(HashFile(many).fault == ChunkFault::Miscounted, "a short declared count is rejected") && ok;
    five[0].declaredBytes = 3;
    many.declaredCount = 5;
    ok = Check(HashFile(many).fault == ChunkFault::Truncated, "a short chunk is truncated") && ok;
    five[0].declaredBytes = 1;
    many.byteLimit = 3;
    ok = Check(HashFile(many).fault == ChunkFault::Oversized, "a file past the limit is oversized") && ok;

    std::string const csv = "Id,Name\n100,Alpha\n200,Beta\n";
    std::vector<std::uint8_t> csvBytes(csv.begin(), csv.end());
    ExpectedManifest expected;
    expected.present = true;
    expected.rowsKnown = true;
    expected.rows = 2;
    Sha256Stream csvHash;
    csvHash.Update(csvBytes);
    expected.digest = csvHash.Finish();
    ParsedExport const parsed = ReadHashedExport(csvBytes, expected, 1024);
    ok = Check(parsed.verdict == ExportVerdict::MetadataChecked && parsed.derivedRows == 2 && parsed.ids.size() == 2 &&
               parsed.ids[0] == 100 && parsed.ids[1] == 200 && !parsed.productionReady,
            "row counts and ids come from the hashed bytes") &&
         ok;
    expected.rows = 9;
    ok = Check(ReadHashedExport(csvBytes, expected, 1024).verdict == ExportVerdict::Invalid,
            "a manifest row count that disagrees with the bytes is invalid") &&
         ok;

    std::ofstream data("/tmp/pb59-synthetic.csv", std::ios::binary);
    data << csv;
    data.close();
    std::ofstream manifest("/tmp/pb59-synthetic.manifest");
    manifest << "rows 2\n";
    Sha256Digest const fileDigest = expected.digest;
    expected.rows = 2;
    manifest << "digest ";
    for (int i = 0; i < 32; ++i)
    {
        char const hex[] = "0123456789abcdef";
        manifest << hex[fileDigest.byte[i] >> 4] << hex[fileDigest.byte[i] & 0xf];
    }
    manifest << "\n";
    manifest.close();
    ParsedExport const fromDisk = ReadExportFile("/tmp/pb59-synthetic.csv", expected, 1024);
    ok = Check(fromDisk.verdict == ExportVerdict::MetadataChecked && fromDisk.derivedRows == 2 &&
               !fromDisk.productionReady,
            "the read-only reader checks a separate manifest") &&
         ok;
    ok = Check(ReadExportFile("/tmp/pb59-synthetic.csv", expected, 8).verdict == ExportVerdict::Bounded,
            "an oversized file is not parsed as a success") &&
         ok;

    PlanInput revisions = Mage();
    ItemFact chest = Piece(66, -2, 4);
    std::vector<ItemFact> one{chest};
    revisions.facts = one;
    revisions.eligibleMask = 1u << 4;
    revisions.requiredMask = 1u << 4;
    revisions.completeMask = 1u << 4;
    Plan const ready = SelectFromFacts(revisions, Score);
    ok = Check(ready.storage.ready && ready.storage.pieces[4].entryId == 66 &&
               ready.storage.pieces[4].randomProperty == -2 && ready.storage.pieces[4].slot == 4 &&
               ready.storage.policyRevision == 8 && ready.storage.sourceRevision == 0 &&
               !ready.storage.deletesRetainedGear,
            "a fresh request records the entry, property, slot, and revisions") &&
         ok;
    revisions.senderEpoch = 0;
    Plan const unknownRevision = SelectFromFacts(revisions, Score);
    ok = Check(!unknownRevision.storage.ready, "an unknown revision is not a ready plan") && ok;
    revisions.senderEpoch = 1;
    revisions.freshnessRequired = true;
    revisions.publishedSenderEpoch = 9;
    revisions.publishedBotEpoch = 4;
    revisions.publishedPolicyRevision = 8;
    revisions.publishedSourceRevision = 0;
    Plan const stale = SelectFromFacts(revisions, Score);
    ok = Check(!stale.storage.ready && stale.outcome.disposition == Disposition::Preserve,
            "a stale revision does not stay ready") &&
         ok;

    RetainedCount bags;
    bags.entryId = 4;
    bags.bagsKnown = true;
    bags.bankKnown = true;
    bags.equippedKnown = true;
    bags.gemsKnown = true;
    bags.equipped = 2;
    bags.bags = 1;
    ok = Check(MoveEquippedIntoBags(bags, 2) && bags.equipped == 0 && bags.bags == 3,
            "gear moved into bags is still retained") &&
         ok;

    ItemFact limited = Piece(9, 1, 4);
    limited.maxCount = 1;
    std::vector<ItemFact> limitedFacts{limited};
    PlanInput missingCount = Mage();
    missingCount.facts = limitedFacts;
    missingCount.eligibleMask = 1u << 4;
    missingCount.requiredMask = 1u << 4;
    missingCount.completeMask = 1u << 4;
    ok = Check(SelectFromFacts(missingCount, Score).outcome.disposition == Disposition::Preserve,
            "a missing retained row preserves") &&
         ok;
    RetainedCount first = bags;
    first.entryId = 9;
    RetainedCount second = first;
    std::array<RetainedCount, 2> duplicates{{first, second}};
    missingCount.retained = duplicates;
    ok = Check(SelectFromFacts(missingCount, Score).outcome.disposition == Disposition::Preserve,
            "a duplicate retained row preserves") &&
         ok;

    std::size_t total = static_cast<std::size_t>(-1) - 1;
    ok = Check(!AddProofCount(total, 2), "a proof count that overflows is rejected") && ok;
    PlanInput declared = Mage();
    declared.hasNativeSource = true;
    declared.sourceBudget = 4;
    declared.sourceRevision = 7;
    declared.eligibleMask = 1u << 4;
    declared.requiredMask = 1u << 4;
    declared.completeMask = 1u << 4;
    declared.nativeProofs[4].authoritative = true;
    declared.nativeProofs[4].rowsDeclared = true;
    declared.nativeProofs[4].declaredRows = 3;
    ok = Check(SelectFromFacts(declared, Score).outcome.disposition == Disposition::Preserve,
            "a proof count that does not match its rows preserves") &&
         ok;

    std::vector<ItemFact> crowd;
    crowd.reserve(40);
    for (std::uint32_t i = 0; i < 40; ++i)
        crowd.push_back(Piece(i + 1, 0, 4));
    PlanInput wide = Mage();
    wide.facts = crowd;
    wide.eligibleMask = 1u << 4;
    wide.requiredMask = 1u << 4;
    wide.completeMask = 1u << 4;
    wide.candidateBudget = 64;
    Plan const widePlan = SelectFromFacts(wide, Score);
    ok = Check(widePlan.factsAccepted == 40 && widePlan.solved.items[4] == 40,
            "a candidate list past the proof budget of 32 still selects") &&
         ok;

    BonusPiece bases[] = {{1, 0, 30, 4, 60}, {2, 1, 30, 4, 60}};
    BonusRule rule[] = {{4, 2, 15}};
    WholeSetScore const unknown = ScoreWholeSet(bases, rule, false, 4, 4);
    ok = Check(unknown.status == WholeSetStatus::Unknown && !unknown.additiveSolverIsSetOptimal,
            "unknown set evidence preserves rather than scoring") &&
         ok;
    WholeSetScore const known = ScoreWholeSet(bases, rule, true, 4, 4);
    ok = Check(known.status == WholeSetStatus::Scored && known.score == 75 && known.score <= known.upperBound &&
               !known.additiveSolverIsSetOptimal,
            "a known synthetic bonus stays inside its upper bound") &&
         ok;
    ItemFact hidden = Piece(66, 0, 4);
    hidden.setId = 4;
    hidden.setEvidenceKnown = false;
    std::vector<ItemFact> hiddenFacts{hidden};
    PlanInput hiddenPlan = Mage();
    hiddenPlan.facts = hiddenFacts;
    hiddenPlan.eligibleMask = 1u << 4;
    hiddenPlan.requiredMask = 1u << 4;
    hiddenPlan.completeMask = 1u << 4;
    ok = Check(SelectFromFacts(hiddenPlan, Score).outcome.disposition == Disposition::Preserve,
            "an accepted item with unknown set evidence does not commit") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
