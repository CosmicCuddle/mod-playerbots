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

Sha256Digest DigestOf(char const* text)
{
    std::array<std::uint8_t, 16> bytes{};
    std::size_t length = 0;
    while (text[length] != 0 && length < bytes.size())
    {
        bytes[length] = static_cast<std::uint8_t>(text[length]);
        ++length;
    }
    return HashSha256(std::span<std::uint8_t const>(bytes.data(), length));
}

WorldManifest World()
{
    WorldManifest world;
    world.schemaNamed = true;
    world.versionKnown = true;
    world.digestKnown = true;
    world.digest = DigestOf("world");
    world.expectedDigest = world.digest;
    world.itemTemplateRows = 2;
    world.expectedItemRows = 2;
    world.validationBound = 8;
    return world;
}

SpellManifest Spells(std::span<std::int32_t const> ids, std::span<bool const> known)
{
    SpellManifest spells;
    spells.headerIsId = true;
    spells.versionKnown = true;
    spells.digestKnown = true;
    spells.digest = DigestOf("spell");
    spells.expectedDigest = spells.digest;
    spells.spellRows = static_cast<std::uint32_t>(ids.size());
    spells.expectedSpellRows = spells.spellRows;
    spells.spellIds = ids;
    spells.overrideKnown = known;
    spells.procKnown = known;
    spells.sourceKnown = known;
    spells.validationBound = 8;
    return spells;
}
}

int main()
{
    bool ok = true;
    Sha256Digest const empty = HashSha256({});
    std::uint8_t const emptyKnown[32] = {0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99,
        0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8,
        0x55};
    Sha256Digest expectedEmpty;
    for (int i = 0; i < 32; ++i)
        expectedEmpty.byte[i] = emptyKnown[i];
    ok = Check(empty == expectedEmpty, "SHA256 of an empty message matches the standard digest") && ok;

    std::array<std::int32_t, 2> ids{{100, 200}};
    std::array<bool, 2> known{{true, true}};
    std::array<std::int32_t, 1> refs{{100}};
    WorldManifest world = World();
    world.itemSpellIds = refs;
    ExportVerdict const agreed = CheckExport(world, Spells(ids, known));
    ok = Check(agreed == ExportVerdict::MetadataChecked, "separate manifests can agree") && ok;
    ok = Check(!ExportIsProductionReady(ExportVerdict::MetadataChecked),
            "checked metadata is not production ready") &&
         ok;

    world.digestKnown = false;
    ok = Check(CheckExport(world, Spells(ids, known)) == ExportVerdict::Unknown, "a missing world digest is unknown") &&
         ok;
    world.digestKnown = true;
    world.itemTemplateRows = 1;
    ExportVerdict const mismatch = CheckExport(world, Spells(ids, known));
    ok = Check(mismatch == ExportVerdict::Invalid, "a world row-count mismatch is invalid") && ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
