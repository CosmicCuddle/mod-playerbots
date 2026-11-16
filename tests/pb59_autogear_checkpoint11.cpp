#include "AutoGearExportReader.h"
#include "AutoGearWholeSetScore.h"
#include <iostream>
#include <sstream>
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

ExpectedManifest ManifestFor(std::string const& text, std::uint32_t rows)
{
    ExpectedManifest expected;
    expected.present = true;
    expected.rowsKnown = true;
    expected.rows = rows;
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    OpenSslSha256 hasher;
    hasher.Update(bytes);
    hasher.Finish(expected.digest);
    return expected;
}

ParsedExport ReadDump(std::string const& text, std::uint32_t rows, DumpContract const& contract)
{
    std::stringstream input(text);
    return ReadExportStream(input, ManifestFor(text, rows), text.size() + 64, ExportFormat::Dump, &contract);
}

LegalPiece Piece(std::uint32_t id, std::uint8_t slot, std::uint32_t score)
{
    LegalPiece piece;
    piece.id = id;
    piece.slot = slot;
    piece.baseScore = score;
    piece.evidenceKnown = true;
    piece.slotMaskKnown = true;
    piece.weaponKnown = true;
    piece.allowedSlots = std::uint32_t{1} << slot;
    return piece;
}
}

int main()
{
    bool ok = true;
    std::string const real = "USE `acore_world`;\n"
                             "CREATE TABLE `item_template` (\n"
                             "  `name` varchar(255) DEFAULT NULL,\n"
                             "  `entry` int(10) unsigned NOT NULL DEFAULT '0',\n"
                             "  PRIMARY KEY (`entry`)\n"
                             ") ENGINE=InnoDB;\n"
                             "INSERT INTO `item_template` VALUES ('alpha',200),('beta',300);\n"
                             "INSERT INTO `item_template` VALUES ('gamma',400);\n";
    DumpContract const items{"acore_world", "item_template", {"name", "entry"}, "entry"};
    ParsedExport const dumped = ReadDump(real, 3, items);
    ok = Check(dumped.verdict == ExportVerdict::MetadataChecked && dumped.ids.size() == 3 && dumped.ids[0] == 200 &&
               dumped.ids[2] == 400 && dumped.schema == "acore_world" && dumped.table == "item_template" &&
               dumped.digest == ManifestFor(real, 3).digest && !dumped.productionReady,
            "USE and a column-less INSERT follow the CREATE proof") &&
         ok;

    std::string create = "USE `acore_world`;\nCREATE TABLE `wide` (\n";
    std::string values;
    std::vector<std::string> wideColumns;
    for (int column = 0; column < 40; ++column)
    {
        if (column != 0)
        {
            create += ",\n";
            values += ",";
        }
        std::string const name = "c" + std::to_string(column);
        wideColumns.push_back(name);
        create += "  `" + name + "` int unsigned NOT NULL";
        values += column == 0 ? "7" : "0";
    }
    create += "\n);\nINSERT INTO `wide` VALUES (" + values + ");\n";
    DumpContract const wide{"acore_world", "wide", wideColumns, "c0"};
    ok = Check(ReadDump(create, 1, wide).verdict == ExportVerdict::MetadataChecked,
            "a table wider than 32 columns is read") &&
         ok;

    DumpContract const missing{"acore_world", "item_template", {"entry"}, "entry"};
    std::string const other = "USE `acore_world`;\nCREATE TABLE `spell` (`id` int unsigned NOT NULL);\n";
    ok = Check(ReadDump(other, 0, missing).verdict == ExportVerdict::Invalid,
            "a dump that never mentions the target table is invalid") &&
         ok;
    std::string const emptyTarget = "USE `acore_world`;\nCREATE TABLE `item_template` (\n"
                                    "  `entry` int unsigned NOT NULL\n);\n";
    DumpContract const entryOnly{"acore_world", "item_template", {"entry"}, "entry"};
    ok = Check(ReadDump(emptyTarget, 0, entryOnly).verdict == ExportVerdict::MetadataChecked,
            "a created target with no insert is evidence of zero rows") &&
         ok;

    std::string const truncated = "USE `acore_world`;\nCREATE TABLE `item_template` (\n"
                                  "  `name` varchar(255),\n  `entry` int unsigned NOT NULL\n);\n"
                                  "INSERT INTO `item_template` VALUES (200);\n";
    ok = Check(ReadDump(truncated, 1, items).verdict == ExportVerdict::Invalid, "a short tuple is rejected") && ok;
    std::string const wideRow = "USE `acore_world`;\nCREATE TABLE `item_template` (\n"
                                "  `name` varchar(255),\n  `entry` int unsigned NOT NULL\n);\n"
                                "INSERT INTO `item_template` VALUES ('a',200,1);\n";
    ok = Check(ReadDump(wideRow, 1, items).verdict == ExportVerdict::Invalid, "a long tuple is rejected") && ok;
    std::string const junk = "USE `acore_world`;\nCREATE TABLE `item_template` (\n"
                             "  `name` varchar(255),\n  `entry` int unsigned NOT NULL\n);\n"
                             "INSERT INTO `item_template` VALUES ('a',200) junk ('b',300);\n";
    ok = Check(ReadDump(junk, 2, items).verdict == ExportVerdict::Invalid, "junk between tuples is rejected") && ok;
    std::string const duplicate = "USE `acore_world`;\nCREATE TABLE `item_template` (\n"
                                  "  `entry` int unsigned NOT NULL,\n  `entry` int unsigned NOT NULL\n);\n";
    ok = Check(ReadDump(duplicate, 0, entryOnly).verdict == ExportVerdict::Invalid,
            "a duplicate create column is rejected") &&
         ok;
    std::string const mismatched = "USE `acore_world`;\nCREATE TABLE `item_template` (\n"
                                   "  `name` varchar(255),\n  `entry` int unsigned NOT NULL\n);\n"
                                   "INSERT INTO `item_template` (`entry`) VALUES (400);\n";
    ok = Check(ReadDump(mismatched, 1, items).verdict == ExportVerdict::Invalid,
            "a target insert with a different column list is not skipped") &&
         ok;

    LegalPiece forward[] = {Piece(2, 11, 10), Piece(1, 10, 10), Piece(4, 11, 10), Piece(3, 10, 10)};
    LegalPiece reverse[] = {Piece(4, 11, 10), Piece(3, 10, 10), Piece(2, 11, 10), Piece(1, 10, 10)};
    WholeSetOracle const first = SearchLegalSet(forward, {}, 4, 4);
    WholeSetOracle const second = SearchLegalSet(reverse, {}, 4, 4);
    ok = Check(first.status == WholeSetStatus::Scored && first.score == second.score && first.chosen[0] == 1 &&
               first.chosen[1] == 2 && second.chosen[0] == first.chosen[0] && second.chosen[1] == first.chosen[1],
            "slot order, not input order, breaks a tie") &&
         ok;

    LegalPiece conflict[] = {Piece(5, 10, 10), Piece(5, 11, 40)};
    conflict[0].setId = 3;
    conflict[1].setId = 4;
    ok = Check(SearchLegalSet(conflict, {}, 4, 4).status == WholeSetStatus::Bounded,
            "the same item cannot carry two set ids") &&
         ok;
    LegalPiece blocked = Piece(9, 10, 10);
    blocked.allowedSlots = 1u << 11;
    ok = Check(SearchLegalSet(std::span<LegalPiece const>(&blocked, 1), {}, 4, 4).status == WholeSetStatus::Bounded,
            "an item in a disallowed slot is rejected") &&
         ok;

    LegalPiece category[] = {Piece(5, 10, 100), Piece(6, 10, 10), Piece(7, 11, 90), Piece(8, 11, 30)};
    category[0].limitCategory = 9;
    category[0].categoryLimit = 1;
    category[2].limitCategory = 9;
    category[2].categoryLimit = 1;
    WholeSetOracle const limited = SearchLegalSet(category, {}, 4, 4);
    ok = Check(limited.status == WholeSetStatus::Scored && limited.score == 130 && limited.chosen[0] == 5,
            "a unique-equipped category keeps one of the category") &&
         ok;

    LegalPiece poison[] = {Piece(10, 15, 3000000000u), Piece(11, 15, 20), Piece(12, 16, 2000000000u)};
    poison[0].weapon = Kind::TwoHand;
    poison[2].slotOptional = true;
    WholeSetOracle const safe = SearchLegalSet(poison, {}, 4, 4);
    ok = Check(safe.status == WholeSetStatus::Scored && safe.score == 3000000000u && safe.chosen[1] == 0,
            "an illegal overflowing pair does not preserve a legal sum") &&
         ok;

    LegalPiece held = Piece(5, 10, 10);
    held.maxCount = 1;
    held.retained = 1;
    ok = Check(SearchLegalSet(std::span<LegalPiece const>(&held, 1), {}, 4, 4).status == WholeSetStatus::Infeasible,
            "a confirmed empty set stays infeasible") &&
         ok;

    LegalPiece shifted = Piece(1, 10, 10);
    shifted.slot = 32;
    ok = Check(SearchLegalSet(std::span<LegalPiece const>(&shifted, 1), {}, 4, 4).status == WholeSetStatus::Bounded,
            "slot 32 is rejected before a shift") &&
         ok;
    shifted.slot = 31;
    ok = Check(SearchLegalSet(std::span<LegalPiece const>(&shifted, 1), {}, 4, 4).status == WholeSetStatus::Bounded,
            "a slot outside the equipment domain is rejected") &&
         ok;

    LegalPiece variants[] = {Piece(5, 10, 10), Piece(5, 11, 10)};
    variants[0].randomProperty = 1;
    variants[1].randomProperty = 4;
    variants[1].maxCount = 2;
    ok = Check(SearchLegalSet(variants, {}, 4, 4).status == WholeSetStatus::Bounded,
            "template facts must agree across random properties") &&
         ok;

    LegalPiece identified[] = {Piece(5, 10, 10), Piece(5, 10, 40)};
    identified[0].randomProperty = 1;
    identified[1].randomProperty = 4;
    WholeSetOracle const identity = SearchLegalSet(identified, {}, 4, 4);
    ok = Check(identity.status == WholeSetStatus::Scored && identity.chosen[0] == 5 && identity.chosenProperty[0] == 4,
            "the chosen result keeps the random property") &&
         ok;

    LegalPiece retainedCategory = Piece(8, 10, 50);
    retainedCategory.limitCategory = 3;
    retainedCategory.categoryLimit = 1;
    retainedCategory.categoryRetained = 1;
    ok = Check(SearchLegalSet(std::span<LegalPiece const>(&retainedCategory, 1), {}, 4, 4).status ==
               WholeSetStatus::Infeasible,
            "a retained category copy leaves nothing legal") &&
         ok;

    LegalPiece onlyHead = Piece(3, 0, 10);
    std::uint32_t const shoulder = std::uint32_t{1} << 2;
    ok = Check(SearchLegalSet(std::span<LegalPiece const>(&onlyHead, 1), {}, 4, 4, false, shoulder).status ==
               WholeSetStatus::Unknown,
            "a missing required slot without completeness preserves") &&
         ok;
    ok = Check(SearchLegalSet(std::span<LegalPiece const>(&onlyHead, 1), {}, 4, 4, true, shoulder).status ==
               WholeSetStatus::Infeasible,
            "a proven empty required slot is infeasible") &&
         ok;
    ok = Check(SearchLegalSet({}, {}, 4, 4).status == WholeSetStatus::Unknown,
            "an unproven empty domain preserves") &&
         ok;
    LegalPiece missingFacts = Piece(4, 10, 10);
    missingFacts.weaponKnown = false;
    WholeSetOracle const noWeapon = SearchLegalSet(std::span<LegalPiece const>(&missingFacts, 1), {}, 4, 4);
    ok = Check(noWeapon.status == WholeSetStatus::Unknown, "a missing weapon fact preserves") && ok;
    ok = Check(SearchLegalSet(std::span<LegalPiece const>(&onlyHead, 1), {}, 1, 4, true, shoulder | 1u).status ==
               WholeSetStatus::Unknown,
            "a bound narrower than the required domain preserves") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
