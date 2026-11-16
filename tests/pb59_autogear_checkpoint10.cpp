#include "AutoGearExportReader.h"
#include "AutoGearVariantSolver.h"
#include "AutoGearWholeSetScore.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace PlayerbotsAutoGearSelection;
using PlayerbotsAutoGearWeapons::Bit;
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

ParsedExport ReadText(std::string const& text, std::uint32_t rows, std::size_t limit, ExportFormat format,
    DumpContract const* contract = nullptr)
{
    std::stringstream input(text);
    return ReadExportStream(input, ManifestFor(text, rows), limit, format, contract);
}

LegalPiece Piece(std::uint32_t id, std::uint8_t slot, std::uint32_t score)
{
    LegalPiece piece;
    piece.id = id;
    piece.slot = slot;
    piece.baseScore = score;
    piece.evidenceKnown = true;
    return piece;
}

bool IdsEqual(WholeSetOracle const& left, WholeSetOracle const& right)
{
    if (left.chosenSlots != right.chosenSlots || left.score != right.score || left.status != right.status)
        return false;
    for (std::uint8_t i = 0; i < left.chosenSlots; ++i)
    {
        if (left.chosen[i] != right.chosen[i])
            return false;
    }
    return true;
}

// Iterative odometer. Independent of SearchLegalSet's recursion.
WholeSetOracle BruteLegal(std::span<LegalPiece const> pieces, std::span<SetDelta const> deltas)
{
    WholeSetOracle oracle;
    std::vector<std::uint8_t> slots;
    std::vector<std::vector<LegalPiece>> grouped;
    for (LegalPiece const& piece : pieces)
    {
        int index = -1;
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            if (slots[i] == piece.slot)
                index = static_cast<int>(i);
        }
        if (index < 0)
        {
            slots.push_back(piece.slot);
            grouped.emplace_back();
            index = static_cast<int>(grouped.size() - 1);
        }
        grouped[static_cast<std::size_t>(index)].push_back(piece);
    }
    for (std::size_t slot = 0; slot < grouped.size(); ++slot)
    {
        if (!grouped[slot].empty() && grouped[slot].front().slotOptional)
        {
            LegalPiece empty;
            empty.slot = slots[slot];
            grouped[slot].push_back(empty);
        }
    }
    if (grouped.empty())
    {
        oracle.status = WholeSetStatus::Scored;
        return oracle;
    }
    std::vector<std::size_t> index(grouped.size(), 0);
    bool found = false;
    while (true)
    {
        std::vector<LegalPiece> picked;
        for (std::size_t slot = 0; slot < grouped.size(); ++slot)
            picked.push_back(grouped[slot][index[slot]]);
        bool twoHand = false;
        bool offHand = false;
        bool legal = true;
        std::uint32_t base = 0;
        for (LegalPiece const& piece : picked)
        {
            if (piece.id == 0)
                continue;
            if (piece.twoHand)
                twoHand = true;
            if (piece.offHand)
                offHand = true;
            std::uint32_t copies = 0;
            for (LegalPiece const& other : picked)
            {
                if (other.id == piece.id)
                    ++copies;
            }
            if (piece.uniqueEquipped && copies > 1)
                legal = false;
            if (piece.maxCount != 0 &&
                (copies > piece.maxCount || piece.retained > piece.maxCount - copies))
                legal = false;
            if (base > 0xFFFFFFFFu - piece.baseScore)
                legal = false;
            else
                base += piece.baseScore;
        }
        if (twoHand && offHand)
            legal = false;
        std::uint32_t bonus = 0;
        if (legal)
        {
            for (SetDelta const& delta : deltas)
            {
                std::uint32_t count = 0;
                for (LegalPiece const& piece : picked)
                {
                    if (piece.id != 0 && piece.setId == delta.setId)
                        ++count;
                }
                if (count >= delta.threshold && bonus <= 0xFFFFFFFFu - delta.bonus)
                    bonus += delta.bonus;
                else if (count >= delta.threshold)
                    legal = false;
            }
        }
        if (legal && base <= 0xFFFFFFFFu - bonus)
        {
            std::uint32_t const total = base + bonus;
            bool smaller = false;
            if (found && total == oracle.score)
            {
                for (std::size_t i = 0; i < picked.size(); ++i)
                {
                    if (picked[i].id != oracle.chosen[i])
                    {
                        smaller = picked[i].id < oracle.chosen[i];
                        break;
                    }
                }
            }
            if (!found || total > oracle.score || smaller)
            {
                found = true;
                oracle.score = total;
                oracle.additiveScore = base;
                oracle.chosenSlots = static_cast<std::uint8_t>(picked.size());
                for (std::size_t i = 0; i < picked.size(); ++i)
                    oracle.chosen[i] = picked[i].id;
            }
        }
        std::size_t slot = grouped.size();
        while (slot > 0)
        {
            --slot;
            ++index[slot];
            if (index[slot] < grouped[slot].size())
                break;
            index[slot] = 0;
            if (slot == 0)
            {
                slot = grouped.size() + 1;
                break;
            }
        }
        if (slot > grouped.size())
            break;
    }
    oracle.status = found ? WholeSetStatus::Scored : WholeSetStatus::Infeasible;
    oracle.additiveSolverIsSetOptimal = false;
    return oracle;
}
}

int main()
{
    bool ok = true;
    ok = Check(ReadText("Id\n\n", 1, 1024, ExportFormat::Csv).verdict == ExportVerdict::Invalid,
            "a blank id is rejected") &&
         ok;
    ok = Check(ReadText("Id,Name\n100\n", 1, 1024, ExportFormat::Csv).verdict == ExportVerdict::Invalid,
            "a short row is rejected") &&
         ok;
    ok = Check(ReadText("Id\n\"1\"2\n", 1, 1024, ExportFormat::Csv).verdict == ExportVerdict::Invalid,
            "a quoted id with a suffix is rejected") &&
         ok;
    ok = Check(ReadText("Id\n100\r\n", 1, 1024, ExportFormat::Csv).verdict == ExportVerdict::Invalid,
            "a carriage return is rejected") &&
         ok;
    ok = Check(ReadText("Id,Note\n100,\"a\"\"b\"\n", 1, 1024, ExportFormat::Csv).verdict ==
               ExportVerdict::MetadataChecked,
            "an escaped quote stays inside the field") &&
         ok;
    std::string wide = "Id,Note\n100,";
    wide.append(257, 'a');
    wide.push_back('\n');
    ok = Check(ReadText(wide, 1, 4096, ExportFormat::Csv).verdict == ExportVerdict::Bounded,
            "a field past the limit is bounded") &&
         ok;
    std::string many = "Id\n";
    for (int row = 1; row <= 4097; ++row)
        many += std::to_string(row) + "\n";
    ok = Check(ReadText(many, 4097, many.size() + 10, ExportFormat::Csv).verdict == ExportVerdict::Bounded,
            "a row past the limit is bounded") &&
         ok;
    ok = Check(ReadText("# pb59-normalized 1\nId\n100\n200", 2, 1024, ExportFormat::Normalized).verdict ==
               ExportVerdict::MetadataChecked,
            "a normalized file may end without a newline") &&
         ok;
    ok = Check(ReadText("# pb59-normalized 1\nId\n100\n\n", 1, 1024, ExportFormat::Normalized).verdict ==
               ExportVerdict::Invalid,
            "a blank normalized line is rejected") &&
         ok;

    std::string const dump = "INSERT INTO `acore_world`.`spell_dbc` (`Id`) VALUES (100);\n"
                             "INSERT INTO `acore_world`.`item_template` (`name`,`entry`) VALUES "
                             "('alpha',200),('beta',300);\n"
                             "INSERT INTO `acore_world`.`item_template` (`entry`) VALUES (400);\n";
    DumpContract const items{"acore_world", "item_template", {"name", "entry"}, "entry"};
    ParsedExport const selected = ReadText(dump, 2, 1024, ExportFormat::Dump, &items);
    ok = Check(selected.verdict == ExportVerdict::MetadataChecked && selected.ids.size() == 2 &&
               selected.ids[0] == 200 && selected.ids[1] == 300 && selected.schema == "acore_world" &&
               selected.table == "item_template" && selected.digest == ManifestFor(dump, 2).digest &&
               !selected.productionReady,
            "the id column need not be first, and other tables are ignored") &&
         ok;

    LegalPiece ordered[] = {Piece(2, 10, 1), Piece(1, 10, 50)};
    WholeSetOracle const orderedScore = SearchLegalSet(ordered, {}, 4, 4);
    ok = Check(orderedScore.status == WholeSetStatus::Scored && orderedScore.score == 50 && orderedScore.chosen[0] == 1,
            "a later higher score is chosen") &&
         ok;

    LegalPiece overflow[] = {Piece(1, 10, 3000000000u), Piece(2, 10, 2000000000u), Piece(3, 11, 3000000000u),
        Piece(4, 11, 2000000000u)};
    WholeSetOracle const kept = SearchLegalSet(overflow, {}, 4, 4);
    ok = Check(kept.status == WholeSetStatus::Scored && kept.score == 4000000000u && kept.chosen[0] == 2 &&
               kept.chosen[1] == 4,
            "overflow keeps searching and keeps the later legal sum") &&
         ok;
    LegalPiece tooBig[] = {Piece(1, 10, 3000000000u), Piece(3, 11, 3000000000u)};
    ok = Check(SearchLegalSet(tooBig, {}, 4, 4).status == WholeSetStatus::Bounded,
            "a domain whose every sum overflows is bounded") &&
         ok;

    LegalPiece shared[] = {Piece(5, 10, 10), Piece(5, 11, 10)};
    WholeSetOracle const repeated = SearchLegalSet(shared, {}, 4, 4);
    ok = Check(repeated.score == 20, "the same item may fill two slots when it is not unique") && ok;
    shared[0].uniqueEquipped = true;
    shared[1].uniqueEquipped = true;
    shared[0].baseScore = 100;
    shared[1].baseScore = 90;
    LegalPiece alternatives[] = {shared[0], Piece(6, 10, 40), shared[1], Piece(8, 11, 30)};
    alternatives[2].uniqueEquipped = true;
    WholeSetOracle const unique = SearchLegalSet(alternatives, {}, 4, 4);
    ok = Check(unique.status == WholeSetStatus::Scored && unique.score == 130 && unique.chosen[0] == 5 &&
               unique.chosen[1] == 8,
            "a unique item is equipped once") &&
         ok;
    alternatives[0].maxCount = 1;
    alternatives[0].retained = 1;
    alternatives[2].maxCount = 1;
    alternatives[2].retained = 1;
    alternatives[0].uniqueEquipped = false;
    alternatives[2].uniqueEquipped = false;
    WholeSetOracle const retained = SearchLegalSet(alternatives, {}, 4, 4);
    ok = Check(retained.status == WholeSetStatus::Scored && retained.score == 70 && retained.chosen[0] == 6,
            "a retained copy uses up MaxCount") &&
         ok;

    LegalPiece hands[] = {Piece(10, 15, 100), Piece(11, 15, 30), Piece(12, 16, 40)};
    hands[0].twoHand = true;
    hands[2].offHand = true;
    hands[2].slotOptional = true;
    WholeSetOracle const weapons = SearchLegalSet(hands, {}, 4, 4);
    ok = Check(weapons.status == WholeSetStatus::Scored && weapons.score == 100 && weapons.chosen[0] == 10 &&
               weapons.chosen[1] == 0,
            "a two-hand weapon leaves the off hand empty") &&
         ok;

    LegalPiece tied[] = {Piece(2, 10, 10), Piece(1, 10, 10), Piece(4, 11, 10), Piece(3, 11, 10)};
    WholeSetOracle const tie = SearchLegalSet(tied, {}, 4, 4);
    ok = Check(tie.score == 20 && tie.chosen[0] == 1 && tie.chosen[1] == 3, "equal scores take the lower ids") && ok;

    SetDelta zero{7, 0, 5};
    ok = Check(SearchLegalSet(ordered, std::span<SetDelta const>(&zero, 1), 4, 4).status == WholeSetStatus::Bounded,
            "a zero set threshold is rejected") &&
         ok;
    SetDelta twice[] = {{7, 2, 5}, {7, 2, 9}};
    ok = Check(SearchLegalSet(ordered, twice, 4, 4).status == WholeSetStatus::Bounded,
            "a duplicate set threshold is rejected") &&
         ok;

    LegalPiece outside = Piece(9, 10, 10);
    outside.evidenceKnown = false;
    ok = Check(SearchLegalSet(std::span<LegalPiece const>(&outside, 1), {}, 4, 4).status == WholeSetStatus::Unknown,
            "missing evidence outside a set does not score") &&
         ok;

    LegalPiece bonusPieces[] = {Piece(1, 10, 10), Piece(2, 10, 50), Piece(3, 11, 10), Piece(4, 11, 50)};
    bonusPieces[0].setId = 7;
    bonusPieces[2].setId = 7;
    SetDelta delta{7, 2, 100};
    WholeSetOracle const withBonus = SearchLegalSet(bonusPieces, std::span<SetDelta const>(&delta, 1), 4, 4);
    WholeSetOracle const bruteBonus = BruteLegal(bonusPieces, std::span<SetDelta const>(&delta, 1));
    ok = Check(withBonus.status == WholeSetStatus::Scored && IdsEqual(withBonus, bruteBonus),
            "the set-bonus search matches the independent solver") &&
         ok;
    WholeSetOracle const bruteUnique = BruteLegal(alternatives, {});
    ok = Check(IdsEqual(retained, bruteUnique), "the retained-count search matches the independent solver") && ok;
    WholeSetOracle const bruteHands = BruteLegal(hands, {});
    ok = Check(IdsEqual(weapons, bruteHands), "the weapon search matches the independent solver") && ok;

    std::vector<PlayerbotsAutoGearVariant::Candidate> low{[]
        {
            PlayerbotsAutoGearVariant::Candidate row;
            row.entryId = 5;
            row.itemLevel = 60;
            row.score = 100;
            row.kind = Kind::Count;
            row.uniqueEquipped = true;
            row.metadataKnown = true;
            row.eligible = true;
            return row;
        }(),
        []
        {
            PlayerbotsAutoGearVariant::Candidate row;
            row.entryId = 6;
            row.itemLevel = 60;
            row.score = 40;
            row.kind = Kind::Count;
            row.metadataKnown = true;
            row.eligible = true;
            return row;
        }()};
    std::vector<PlayerbotsAutoGearVariant::Candidate> high{[]
        {
            PlayerbotsAutoGearVariant::Candidate row;
            row.entryId = 5;
            row.itemLevel = 60;
            row.score = 90;
            row.kind = Kind::Count;
            row.uniqueEquipped = true;
            row.metadataKnown = true;
            row.eligible = true;
            return row;
        }(),
        []
        {
            PlayerbotsAutoGearVariant::Candidate row;
            row.entryId = 8;
            row.itemLevel = 60;
            row.score = 30;
            row.kind = Kind::Count;
            row.metadataKnown = true;
            row.eligible = true;
            return row;
        }()};
    std::array<PlayerbotsAutoGearVariant::Domain, PlayerbotsAutoGearSet::Slots> domains{};
    domains[10].candidates = low;
    domains[10].complete = true;
    domains[11].candidates = high;
    domains[11].complete = true;
    PlayerbotsAutoGearVariant::Policy policy;
    policy.cap = 66;
    policy.eligibleMask = (1u << 10) | (1u << 11);
    policy.requiredMask = policy.eligibleMask;
    policy.known = true;
    PlayerbotsAutoGearVariant::Result const variant =
        PlayerbotsAutoGearVariant::Solve(domains, policy, 1000, 100000);
    LegalPiece uniquePieces[] = {Piece(5, 10, 100), Piece(6, 10, 40), Piece(5, 11, 90), Piece(8, 11, 30)};
    uniquePieces[0].uniqueEquipped = true;
    uniquePieces[2].uniqueEquipped = true;
    WholeSetOracle const uniqueOracle = SearchLegalSet(uniquePieces, {}, 4, 4);
    ok = Check(uniqueOracle.score == variant.score && variant.score == 130, "unique gear matches the variant solver") &&
         ok;

    std::vector<PlayerbotsAutoGearVariant::Candidate> mains{[]
        {
            PlayerbotsAutoGearVariant::Candidate row;
            row.entryId = 10;
            row.itemLevel = 60;
            row.score = 100;
            row.kind = Kind::TwoHand;
            row.exclusiveMainhand = true;
            row.metadataKnown = true;
            row.eligible = true;
            return row;
        }(),
        []
        {
            PlayerbotsAutoGearVariant::Candidate row;
            row.entryId = 11;
            row.itemLevel = 60;
            row.score = 30;
            row.kind = Kind::OneHand;
            row.metadataKnown = true;
            row.eligible = true;
            return row;
        }()};
    std::vector<PlayerbotsAutoGearVariant::Candidate> offs{[]
        {
            PlayerbotsAutoGearVariant::Candidate row;
            row.entryId = 12;
            row.itemLevel = 60;
            row.score = 40;
            row.kind = Kind::Shield;
            row.metadataKnown = true;
            row.eligible = true;
            return row;
        }()};
    std::array<PlayerbotsAutoGearVariant::Domain, PlayerbotsAutoGearSet::Slots> weaponDomains{};
    weaponDomains[15].candidates = mains;
    weaponDomains[15].complete = true;
    weaponDomains[16].candidates = offs;
    weaponDomains[16].complete = true;
    PlayerbotsAutoGearVariant::Policy weaponPolicy;
    weaponPolicy.cap = 66;
    weaponPolicy.eligibleMask = (1u << 15) | (1u << 16);
    weaponPolicy.weapons.mainhand = Bit(Kind::OneHand) | Bit(Kind::TwoHand);
    weaponPolicy.weapons.offhand = Bit(Kind::Shield);
    weaponPolicy.weapons.mainhandRequired = true;
    weaponPolicy.known = true;
    PlayerbotsAutoGearVariant::Result const weaponVariant =
        PlayerbotsAutoGearVariant::Solve(weaponDomains, weaponPolicy, 1000, 100000);
    ok = Check(weapons.score == weaponVariant.score && weaponVariant.score == 100,
            "two-hand exclusion matches the variant solver") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
