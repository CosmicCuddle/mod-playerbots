/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_EXPORT_READER_H
#define PLAYERBOTS_AUTOGEAR_EXPORT_READER_H

#include "AutoGearExportCheck.h"
#include <openssl/evp.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <istream>
#include <span>
#include <string>
#include <vector>

namespace PlayerbotsAutoGearSelection
{
    // Official reader input. A mysqldump is converted into this text first.
    // The converter never executes SQL.
    enum class ExportFormat : std::uint8_t
    {
        Csv,
        Normalized,
        Dump
    };

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

    // Rejects overflow and any non-digit. It does not throw.
    inline bool ParseManifestUint(std::string const& text, std::uint32_t& value)
    {
        if (text.empty())
            return false;
        std::uint32_t parsed = 0;
        for (char const digit : text)
        {
            if (digit < '0' || digit > '9')
                return false;
            std::uint32_t const part = static_cast<std::uint32_t>(digit - '0');
            if (parsed > (0xFFFFFFFFu - part) / 10u)
                return false;
            parsed = parsed * 10u + part;
        }
        value = parsed;
        return true;
    }

    // The whole token is the id. Trailing junk is rejected.
    inline bool ParseWholeId(std::string const& token, std::int32_t& id)
    {
        if (token.empty() || token[0] < '0' || token[0] > '9')
            return false;
        std::uint32_t parsed = 0;
        for (char const digit : token)
        {
            if (digit < '0' || digit > '9')
                return false;
            std::uint32_t const part = static_cast<std::uint32_t>(digit - '0');
            if (parsed > (0x7FFFFFFFu - part) / 10u)
                return false;
            parsed = parsed * 10u + part;
        }
        if (parsed == 0)
            return false;
        id = static_cast<std::int32_t>(parsed);
        return true;
    }

    inline bool RememberId(std::vector<std::int32_t>& ids, std::int32_t id)
    {
        for (std::int32_t const existing : ids)
        {
            if (existing == id)
                return false;
        }
        ids.push_back(id);
        return true;
    }

    // Streaming SHA-256. This is the export path; it does not use the
    // in-header teaching implementation.
    class OpenSslSha256
    {
    public:
        OpenSslSha256()
        {
            context = EVP_MD_CTX_new();
            if (context == nullptr || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1)
                failed = true;
        }

        OpenSslSha256(OpenSslSha256 const&) = delete;
        OpenSslSha256& operator=(OpenSslSha256 const&) = delete;

        ~OpenSslSha256()
        {
            if (context != nullptr)
                EVP_MD_CTX_free(context);
        }

        bool Update(std::span<std::uint8_t const> chunk)
        {
            if (failed || chunk.empty())
                return !failed;
            if (EVP_DigestUpdate(context, chunk.data(), chunk.size()) != 1)
                failed = true;
            return !failed;
        }

        bool Finish(Sha256Digest& digest)
        {
            if (failed)
                return false;
            unsigned int length = 0;
            if (EVP_DigestFinal_ex(context, digest.byte, &length) != 1 || length != 32)
            {
                failed = true;
                return false;
            }
            return true;
        }

        bool ok() const
        {
            return !failed;
        }

    private:
        EVP_MD_CTX* context = nullptr;
        bool failed = false;
    };

    class ExportParser
    {
    public:
        explicit ExportParser(ExportFormat format) : format(format)
        {
        }

        void Feed(std::span<std::uint8_t const> chunk)
        {
            for (std::uint8_t const value : chunk)
                FeedByte(static_cast<char>(value));
        }

        bool Finish()
        {
            if (failed)
                return false;
            if (inQuote || inBlock)
            {
                failed = true;
                return false;
            }
            if (format == ExportFormat::Dump && inInsert)
                EndStatement();
            if (format != ExportFormat::Dump && (!field.empty() || fieldIndex != 0))
            {
                EndField();
                EndRecord();
            }
            return !failed && !duplicate;
        }

        bool Failed() const
        {
            return failed;
        }

        bool Duplicate() const
        {
            return duplicate;
        }

        std::vector<std::int32_t> const& Ids() const
        {
            return ids;
        }

    private:
        void Fail()
        {
            failed = true;
        }

        void FeedByte(char const value)
        {
            if (failed)
                return;
            if (format == ExportFormat::Dump)
                FeedDump(value);
            else if (format == ExportFormat::Normalized)
                FeedNormalized(value);
            else
                FeedCsv(value);
        }

        void FeedNormalized(char const value)
        {
            if (value == '\n')
            {
                if (!field.empty() && field[0] != '#')
                {
                    if (field != "Id" && field != "id")
                    {
                        std::int32_t id = 0;
                        if (!ParseWholeId(field, id) || !RememberId(ids, id))
                            Fail();
                    }
                }
                field.clear();
                return;
            }
            if (value != '\r')
                field.push_back(value);
            if (field.size() > 4096)
                Fail();
        }

        void FeedCsv(char const value)
        {
            if (inQuote)
            {
                if (value == '"')
                    inQuote = false;
                else
                    field.push_back(value);
                if (field.size() > 4096)
                    Fail();
                return;
            }
            if (value == '"')
            {
                if (!field.empty())
                    Fail();
                else
                    inQuote = true;
                return;
            }
            if (value == ',')
            {
                EndField();
                return;
            }
            if (value == '\n')
            {
                EndField();
                EndRecord();
                return;
            }
            if (value != '\r')
                field.push_back(value);
        }

        void EndField()
        {
            if (failed)
                return;
            if (fieldIndex == 0)
            {
                if (!headerDone)
                {
                    if (field != "Id" && field != "id")
                        Fail();
                    headerDone = true;
                }
                else if (!field.empty())
                {
                    std::int32_t id = 0;
                    if (!ParseWholeId(field, id) || !RememberId(ids, id))
                    {
                        if (!failed)
                            duplicate = !ParseWholeId(field, id);
                        if (duplicate)
                            Fail();
                        else
                            Fail();
                    }
                }
            }
            field.clear();
            ++fieldIndex;
        }

        void EndRecord()
        {
            fieldIndex = 0;
        }

        void FeedDump(char const value)
        {
            if (inLineComment)
            {
                if (value == '\n')
                    inLineComment = false;
                return;
            }
            if (inBlock)
            {
                if (sawStar && value == '/')
                    inBlock = false;
                sawStar = value == '*';
                return;
            }
            if (inQuote)
            {
                if (escaped)
                {
                    escaped = false;
                    return;
                }
                if (value == '\\')
                {
                    escaped = true;
                    return;
                }
                if (value == quote)
                    inQuote = false;
                return;
            }
            if (value == '-' && sawDash)
            {
                inLineComment = true;
                sawDash = false;
                return;
            }
            sawDash = value == '-';
            if (value == '#')
            {
                inLineComment = true;
                return;
            }
            if (value == '*' && sawSlash)
            {
                inBlock = true;
                sawStar = false;
                sawSlash = false;
                return;
            }
            sawSlash = value == '/';
            if (value == '\'' || value == '"')
            {
                inQuote = true;
                quote = value;
                return;
            }
            if (!inInsert)
            {
                word.push_back(value);
                if (word.size() > 16)
                    word.erase(word.begin());
                if (word.size() >= 6 && word.compare(word.size() - 6, 6, "VALUES") == 0)
                {
                    bool identifier = false;
                    if (word.size() > 6)
                    {
                        char const before = word[word.size() - 7];
                        identifier = (before >= 'A' && before <= 'Z') || (before >= 'a' && before <= 'z') ||
                                     (before >= '0' && before <= '9') || before == '_';
                    }
                    if (!identifier)
                        inInsert = true;
                }
                if (value == ';')
                    word.clear();
                return;
            }
            if (value == '(')
            {
                ++depth;
                if (depth == 1)
                    takingId = true;
                return;
            }
            if (takingId)
            {
                if (idToken.empty() && (value == ' ' || value == '\t' || value == '\n' || value == '\r'))
                    return;
                if (value >= '0' && value <= '9')
                {
                    idToken.push_back(value);
                    return;
                }
                if (value == ',' || value == ')')
                {
                    std::int32_t id = 0;
                    if (!ParseWholeId(idToken, id) || !RememberId(ids, id))
                        Fail();
                    idToken.clear();
                    takingId = false;
                    if (value == ')')
                        depth = 0;
                    return;
                }
                Fail();
                return;
            }
            if (value == ')' && depth > 0)
                --depth;
            if (value == ';' && depth == 0)
            {
                inInsert = false;
                word.clear();
            }
        }

        void EndStatement()
        {
            if (!idToken.empty() || inQuote || depth != 0)
                Fail();
        }

        ExportFormat format;
        bool failed = false;
        bool duplicate = false;
        bool inQuote = false;
        bool headerDone = false;
        bool inLineComment = false;
        bool inBlock = false;
        bool sawStar = false;
        bool sawDash = false;
        bool sawSlash = false;
        bool escaped = false;
        bool inInsert = false;
        bool takingId = false;
        char quote = '\'';
        int depth = 0;
        int fieldIndex = 0;
        std::string field;
        std::string word;
        std::string idToken;
        std::vector<std::int32_t> ids;
    };

    inline ParsedExport FinishParsed(OpenSslSha256& hasher, ExportParser& parser, ExpectedManifest const& expected)
    {
        ParsedExport parsed;
        parsed.productionReady = false;
        if (!hasher.Finish(parsed.digest))
        {
            parsed.verdict = ExportVerdict::ReadError;
            return parsed;
        }
        if (!(parsed.digest == expected.digest))
        {
            parsed.verdict = ExportVerdict::Invalid;
            return parsed;
        }
        if (!parser.Finish() || parser.Failed() || parser.Duplicate())
        {
            parsed.verdict = ExportVerdict::Invalid;
            return parsed;
        }
        parsed.ids = parser.Ids();
        parsed.derivedRows = static_cast<std::uint32_t>(parsed.ids.size());
        if (parsed.derivedRows != expected.rows)
            parsed.verdict = ExportVerdict::Invalid;
        else
            parsed.verdict = ExportVerdict::MetadataChecked;
        return parsed;
    }

    inline bool RoomFor(std::size_t seen, std::size_t incoming, std::size_t limit)
    {
        if (incoming > limit)
            return false;
        return seen <= limit - incoming;
    }

    inline ParsedExport ReadExportStream(std::istream& input, ExpectedManifest const& expected, std::size_t byteLimit,
        ExportFormat format)
    {
        ParsedExport parsed;
        parsed.productionReady = false;
        if (!expected.present || !expected.rowsKnown || byteLimit == 0)
            return parsed;
        if (input.bad())
        {
            parsed.verdict = ExportVerdict::ReadError;
            return parsed;
        }
        OpenSslSha256 hasher;
        ExportParser parser(format);
        std::size_t seen = 0;
        char buffer[64];
        while (true)
        {
            if (input.bad())
            {
                parsed.verdict = ExportVerdict::ReadError;
                return parsed;
            }
            input.read(buffer, sizeof buffer);
            std::streamsize const got = input.gcount();
            if (input.bad())
            {
                parsed.verdict = ExportVerdict::ReadError;
                return parsed;
            }
            if (got == 0)
            {
                if (input.eof())
                    break;
                parsed.verdict = ExportVerdict::ReadError;
                return parsed;
            }
            std::size_t const count = static_cast<std::size_t>(got);
            if (!RoomFor(seen, count, byteLimit))
            {
                parsed.verdict = ExportVerdict::Bounded;
                return parsed;
            }
            std::span<std::uint8_t const> const chunk(reinterpret_cast<std::uint8_t const*>(buffer), count);
            if (!hasher.Update(chunk))
            {
                parsed.verdict = ExportVerdict::ReadError;
                return parsed;
            }
            parser.Feed(chunk);
            seen += count;
        }
        return FinishParsed(hasher, parser, expected);
    }

    inline ParsedExport ReadExportFile(std::string const& path, ExpectedManifest const& expected, std::size_t byteLimit,
        ExportFormat format = ExportFormat::Csv)
    {
        ParsedExport parsed;
        parsed.productionReady = false;
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return parsed;
        return ReadExportStream(input, expected, byteLimit, format);
    }

    // Read-only. Statements are recognized and skipped. Nothing is executed.
    inline ParsedExport ConvertDump(std::span<std::uint8_t const> dump, ExpectedManifest const& expected)
    {
        ParsedExport parsed;
        parsed.productionReady = false;
        if (!expected.present || !expected.rowsKnown)
            return parsed;
        OpenSslSha256 hasher;
        ExportParser parser(ExportFormat::Dump);
        std::size_t offset = 0;
        while (offset < dump.size())
        {
            std::size_t const count = std::min<std::size_t>(64, dump.size() - offset);
            std::span<std::uint8_t const> const chunk = dump.subspan(offset, count);
            if (!hasher.Update(chunk))
            {
                parsed.verdict = ExportVerdict::ReadError;
                return parsed;
            }
            parser.Feed(chunk);
            offset += count;
        }
        return FinishParsed(hasher, parser, expected);
    }

    inline ParsedExport ReadHashedExport(std::span<std::uint8_t const> bytes, ExpectedManifest const& expected,
        std::size_t byteLimit, ExportFormat format = ExportFormat::Csv)
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
        OpenSslSha256 hasher;
        ExportParser parser(format);
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            std::size_t const count = std::min<std::size_t>(64, bytes.size() - offset);
            if (!RoomFor(offset, count, byteLimit))
            {
                parsed.verdict = ExportVerdict::Bounded;
                return parsed;
            }
            std::span<std::uint8_t const> const chunk = bytes.subspan(offset, count);
            if (!hasher.Update(chunk))
            {
                parsed.verdict = ExportVerdict::ReadError;
                return parsed;
            }
            parser.Feed(chunk);
            offset += count;
        }
        return FinishParsed(hasher, parser, expected);
    }
}

#endif
