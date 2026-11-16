/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_EXPORT_READER_H
#define PLAYERBOTS_AUTOGEAR_EXPORT_READER_H

#include "AutoGearExportCheck.h"
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace PlayerbotsAutoGearSelection
{
    // The expected digest and row count come from a different file than the bytes.
    struct ExpectedManifest
    {
        bool present = false;
        bool rowsKnown = false;
        std::uint32_t rows = 0;
        Sha256Digest digest{};
    };

    struct ParsedExport
    {
        ExportVerdict verdict = ExportVerdict::Unknown;
        Sha256Digest digest{};
        std::uint32_t derivedRows = 0;
        std::vector<std::int32_t> ids{};
        bool productionReady = false;
    };

    inline bool ParseLeadingId(std::string const& line, std::int32_t& id)
    {
        std::size_t index = 0;
        if (line.size() >= 6 && line.compare(0, 6, "INSERT") == 0)
        {
            index = line.find('(');
            if (index == std::string::npos)
                return false;
            ++index;
        }
        if (index >= line.size() || line[index] < '0' || line[index] > '9')
            return false;
        std::uint32_t value = 0;
        while (index < line.size() && line[index] >= '0' && line[index] <= '9')
        {
            std::uint32_t const digit = static_cast<std::uint32_t>(line[index] - '0');
            if (value > (0x7FFFFFFFu - digit) / 10)
                return false;
            value = value * 10 + digit;
            ++index;
        }
        if (value == 0)
            return false;
        id = static_cast<std::int32_t>(value);
        return true;
    }

    // Counts and ids are parsed from the same bytes that were hashed.
    inline ParsedExport ReadHashedExport(std::span<std::uint8_t const> bytes, ExpectedManifest const& expected,
        std::size_t byteLimit)
    {
        ParsedExport parsed;
        parsed.productionReady = false;
        if (!expected.present || !expected.rowsKnown || byteLimit == 0)
            return parsed;
        if (bytes.size() > byteLimit)
        {
            parsed.verdict = ExportVerdict::Bounded;
            return parsed;
        }
        Sha256Stream stream;
        stream.Update(bytes);
        parsed.digest = stream.Finish();
        if (!(parsed.digest == expected.digest))
        {
            parsed.verdict = ExportVerdict::Invalid;
            return parsed;
        }
        std::string line;
        bool header = false;
        for (std::uint8_t value : bytes)
        {
            if (value == '\n')
            {
                if (!line.empty() && line[0] != '#')
                {
                    if (!header && (line.compare(0, 2, "Id") == 0 || line.compare(0, 3, "id,") == 0))
                        header = true;
                    else
                    {
                        std::int32_t id = 0;
                        if (!ParseLeadingId(line, id))
                        {
                            parsed.verdict = ExportVerdict::Invalid;
                            parsed.ids.clear();
                            parsed.derivedRows = 0;
                            return parsed;
                        }
                        parsed.ids.push_back(id);
                        ++parsed.derivedRows;
                    }
                }
                line.clear();
            }
            else if (value != '\r')
                line.push_back(static_cast<char>(value));
        }
        if (!line.empty())
        {
            std::int32_t id = 0;
            if (!ParseLeadingId(line, id))
            {
                parsed.verdict = ExportVerdict::Invalid;
                return parsed;
            }
            parsed.ids.push_back(id);
            ++parsed.derivedRows;
        }
        if (parsed.derivedRows != expected.rows)
        {
            parsed.verdict = ExportVerdict::Invalid;
            return parsed;
        }
        parsed.verdict = ExportVerdict::MetadataChecked;
        return parsed;
    }

    inline ParsedExport ReadExportFile(std::string const& path, ExpectedManifest const& expected, std::size_t byteLimit)
    {
        ParsedExport parsed;
        parsed.productionReady = false;
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return parsed;
        std::vector<std::uint8_t> bytes;
        char buffer[64];
        while (input)
        {
            input.read(buffer, sizeof buffer);
            std::streamsize const got = input.gcount();
            if (got < 0)
            {
                parsed.verdict = ExportVerdict::Invalid;
                return parsed;
            }
            if (got == 0)
                break;
            if (bytes.size() > byteLimit - static_cast<std::size_t>(got))
            {
                parsed.verdict = ExportVerdict::Bounded;
                return parsed;
            }
            bytes.insert(bytes.end(), buffer, buffer + got);
        }
        return ReadHashedExport(bytes, expected, byteLimit);
    }
}

#endif
