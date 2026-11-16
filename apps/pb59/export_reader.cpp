#include "AutoGearExportReader.h"
#include <fstream>
#include <iostream>
#include <string>

namespace
{
int HexValue(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

bool LoadManifest(std::string const& path, PlayerbotsAutoGearSelection::ExpectedManifest& manifest)
{
    std::ifstream input(path);
    if (!input)
        return false;
    std::string key;
    std::string value;
    while (input >> key >> value)
    {
        if (key == "rows")
        {
            manifest.rows = static_cast<std::uint32_t>(std::stoul(value));
            manifest.rowsKnown = true;
        }
        else if (key == "digest" && value.size() == 64)
        {
            for (int i = 0; i < 32; ++i)
            {
                int const high = HexValue(value[static_cast<std::size_t>(i * 2)]);
                int const low = HexValue(value[static_cast<std::size_t>(i * 2 + 1)]);
                if (high < 0 || low < 0)
                    return false;
                manifest.digest.byte[i] = static_cast<std::uint8_t>((high << 4) | low);
            }
            manifest.present = true;
        }
    }
    return manifest.present && manifest.rowsKnown;
}
}

// Read-only. The manifest is a separate file. Exit status is never "production ready".
int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: export_reader <data> <manifest> <byte-limit>\n";
        return 2;
    }
    PlayerbotsAutoGearSelection::ExpectedManifest expected;
    if (!LoadManifest(argv[2], expected))
    {
        std::cout << "verdict=Unknown productionReady=false\n";
        return 1;
    }
    std::size_t const limit = static_cast<std::size_t>(std::stoull(argv[3]));
    PlayerbotsAutoGearSelection::ParsedExport const parsed =
        PlayerbotsAutoGearSelection::ReadExportFile(argv[1], expected, limit);
    char const* name = "Unknown";
    if (parsed.verdict == PlayerbotsAutoGearSelection::ExportVerdict::Invalid)
        name = "Invalid";
    else if (parsed.verdict == PlayerbotsAutoGearSelection::ExportVerdict::Bounded)
        name = "Bounded";
    else if (parsed.verdict == PlayerbotsAutoGearSelection::ExportVerdict::MetadataChecked)
        name = "MetadataChecked";
    std::cout << "verdict=" << name << " rows=" << parsed.derivedRows << " productionReady=false\n";
    return parsed.verdict == PlayerbotsAutoGearSelection::ExportVerdict::MetadataChecked ? 0 : 1;
}
