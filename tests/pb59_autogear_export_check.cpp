#include "AutoGearExportCheck.h"
#include <array>
#include <iostream>

using namespace PlayerbotsAutoGearSelection;

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

ExportSnapshot Synthetic()
{
    ExportSnapshot snapshot;
    snapshot.schemaNamed = true;
    snapshot.checksumKnown = true;
    snapshot.versionKnown = true;
    snapshot.checksum = 11;
    snapshot.expectedChecksum = 11;
    snapshot.itemTemplateRows = 2;
    snapshot.expectedItemRows = 2;
    snapshot.spellRows = 2;
    snapshot.expectedSpellRows = 2;
    snapshot.spellHeaderIsId = true;
    return snapshot;
}
}

int main()
{
    bool ok = true;
    std::array<std::uint32_t, 2> spells{{100, 200}};
    std::array<std::uint32_t, 1> refs{{100}};
    ExportSnapshot snapshot = Synthetic();
    snapshot.spellIds = spells;
    snapshot.itemSpellIds = refs;
    ok = Check(CheckExport(snapshot) == ExportVerdict::Accepted, "a complete synthetic export is accepted") && ok;

    snapshot.checksumKnown = false;
    ok = Check(CheckExport(snapshot) == ExportVerdict::Unknown, "a missing checksum is unknown") && ok;
    snapshot.checksumKnown = true;
    snapshot.spellHeaderIsId = false;
    ok = Check(CheckExport(snapshot) == ExportVerdict::Unknown, "a missing spell header is unknown") && ok;
    snapshot.spellHeaderIsId = true;
    snapshot.itemTemplateRows = 1;
    ok = Check(CheckExport(snapshot) == ExportVerdict::Invalid, "a row-count mismatch is invalid") && ok;
    snapshot.itemTemplateRows = 2;
    spells[1] = 100;
    ok = Check(CheckExport(snapshot) == ExportVerdict::Invalid, "a duplicate spell id is invalid") && ok;
    spells[1] = 200;
    std::array<std::uint32_t, 1> missing{{300}};
    snapshot.itemSpellIds = missing;
    ok = Check(CheckExport(snapshot) == ExportVerdict::Unknown, "a spell that is not in the export stays unknown") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
