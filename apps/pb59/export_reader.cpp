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
            if (!PlayerbotsAutoGearSelection::ParseManifestUint(value, manifest.rows))
                return false;
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

// Read-only. The format is required. A dump also names its schema, table, id column,
// and column list. Exit status is never "production ready". Nothing is executed.
int main(int argc, char** argv)
{
    if (argc != 5 && argc != 9)
    {
        std::cerr << "usage: export_reader <data> <manifest> <byte-limit> <csv|normalized|dump> "
                     "[schema table id-column columns]\n";
        return 2;
    }
    PlayerbotsAutoGearSelection::ExpectedManifest expected;
    if (!LoadManifest(argv[2], expected))
    {
        std::cout << "verdict=Unknown productionReady=false\n";
        return 1;
    }
    std::uint32_t limitValue = 0;
    if (!PlayerbotsAutoGearSelection::ParseManifestUint(argv[3], limitValue) || limitValue == 0)
    {
        std::cout << "verdict=Unknown productionReady=false\n";
        return 1;
    }
    std::size_t const limit = limitValue;
    std::string const formatName = argv[4];
    PlayerbotsAutoGearSelection::ExportFormat format;
    if (formatName == "csv")
        format = PlayerbotsAutoGearSelection::ExportFormat::Csv;
    else if (formatName == "normalized")
        format = PlayerbotsAutoGearSelection::ExportFormat::Normalized;
    else if (formatName == "dump")
        format = PlayerbotsAutoGearSelection::ExportFormat::Dump;
    else
    {
        std::cout << "verdict=Unknown productionReady=false\n";
        return 1;
    }
    if (format == PlayerbotsAutoGearSelection::ExportFormat::Dump && argc != 9)
    {
        std::cout << "verdict=Unknown productionReady=false\n";
        return 1;
    }
    if (format != PlayerbotsAutoGearSelection::ExportFormat::Dump && argc != 5)
    {
        std::cout << "verdict=Unknown productionReady=false\n";
        return 1;
    }
    PlayerbotsAutoGearSelection::DumpContract contract;
    if (format == PlayerbotsAutoGearSelection::ExportFormat::Dump)
    {
        contract.schema = argv[5];
        contract.table = argv[6];
        contract.idColumn = argv[7];
        std::string const list = argv[8];
        std::string column;
        for (char const value : list)
        {
            if (value == ',')
            {
                if (column.empty())
                {
                    std::cout << "verdict=Unknown productionReady=false\n";
                    return 1;
                }
                contract.columns.push_back(column);
                column.clear();
            }
            else
                column.push_back(value);
        }
        if (column.empty())
        {
            std::cout << "verdict=Unknown productionReady=false\n";
            return 1;
        }
        contract.columns.push_back(column);
    }
    PlayerbotsAutoGearSelection::ParsedExport const parsed = PlayerbotsAutoGearSelection::ReadExportFile(argv[1],
        expected, limit, format, format == PlayerbotsAutoGearSelection::ExportFormat::Dump ? &contract : nullptr);
    char const* name = "Unknown";
    if (parsed.verdict == PlayerbotsAutoGearSelection::ExportVerdict::Invalid)
        name = "Invalid";
    else if (parsed.verdict == PlayerbotsAutoGearSelection::ExportVerdict::Bounded)
        name = "Bounded";
    else if (parsed.verdict == PlayerbotsAutoGearSelection::ExportVerdict::MetadataChecked)
        name = "MetadataChecked";
    else if (parsed.verdict == PlayerbotsAutoGearSelection::ExportVerdict::ReadError)
        name = "ReadError";
    std::cout << "verdict=" << name << " rows=" << parsed.derivedRows;
    if (!parsed.table.empty())
        std::cout << " schema=" << parsed.schema << " table=" << parsed.table;
    std::cout << " productionReady=false\n";
    return parsed.verdict == PlayerbotsAutoGearSelection::ExportVerdict::MetadataChecked ? 0 : 1;
}
