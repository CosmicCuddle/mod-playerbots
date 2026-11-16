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
#include <unordered_set>
#include <vector>

namespace PlayerbotsAutoGearSelection
{
    // Callers name the format. There is no implicit CSV default.
    enum class ExportFormat : std::uint8_t
    {
        Csv,
        Normalized,
        Dump
    };

    inline constexpr std::size_t kExportFieldLimit = 256;
    inline constexpr std::size_t kExportColumnLimit = 32;
    inline constexpr std::uint32_t kExportRowLimit = 4096;

    // Only this table and this column list are read. Other VALUES clauses are ignored.
    // Nothing in the dump is executed.
    struct DumpContract
    {
        std::string schema;
        std::string table;
        std::vector<std::string> columns;
        std::string idColumn;
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
        std::string schema;
        std::string table;
        bool productionReady = false;
    };

    // Membership is hashed. Output order is the order the ids were accepted.
    class IdRegistry
    {
    public:
        bool Add(std::int32_t id)
        {
            if (ordered.size() >= kExportRowLimit)
            {
                overLimit = true;
                return false;
            }
            if (!seen.insert(id).second)
            {
                duplicate = true;
                return false;
            }
            ordered.push_back(id);
            return true;
        }

        bool Duplicate() const
        {
            return duplicate;
        }

        bool OverLimit() const
        {
            return overLimit;
        }

        std::vector<std::int32_t> const& Ids() const
        {
            return ordered;
        }

    private:
        std::vector<std::int32_t> ordered;
        std::unordered_set<std::int32_t> seen;
        bool duplicate = false;
        bool overLimit = false;
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
        ExportParser(ExportFormat format, DumpContract const* contract) : format(format), contract(contract)
        {
            if (format != ExportFormat::Dump)
                return;
            if (contract == nullptr || contract->schema.empty() || contract->table.empty() ||
                contract->columns.empty() || contract->columns.size() > kExportColumnLimit)
            {
                Fail();
                return;
            }
            int found = 0;
            for (std::size_t i = 0; i < contract->columns.size(); ++i)
            {
                if (contract->columns[i] != contract->idColumn)
                    continue;
                idIndex = static_cast<int>(i);
                ++found;
            }
            if (found != 1)
                Fail();
        }

        void Feed(std::span<std::uint8_t const> chunk)
        {
            for (std::uint8_t const value : chunk)
                FeedByte(static_cast<char>(value));
        }

        bool Finish()
        {
            if (failed || overLimit)
                return false;
            if (inQuote || inBlock)
            {
                failed = true;
                return false;
            }
            if (format == ExportFormat::Dump)
            {
                if (mode == DumpMode::InTuples || mode == DumpMode::WantValues || inBacktick)
                    Fail();
            }
            else if (format == ExportFormat::Normalized)
            {
                if (!field.empty())
                    EndNormalizedLine();
            }
            else if (!field.empty() || fieldIndex != 0 || afterQuote)
            {
                EndField();
                EndRecord();
            }
            return !failed && !registry.Duplicate() && !overLimit;
        }

        bool Failed() const
        {
            return failed;
        }

        bool Duplicate() const
        {
            return registry.Duplicate();
        }

        bool OverLimit() const
        {
            return overLimit || registry.OverLimit();
        }

        std::vector<std::int32_t> const& Ids() const
        {
            return registry.Ids();
        }

    private:
        enum class DumpMode : std::uint8_t
        {
            Idle,
            AfterInsert,
            AfterInto,
            AfterFirstName,
            WantTable,
            AfterTable,
            InColumns,
            AfterColumn,
            WantValues,
            InTuples
        };

        void Fail()
        {
            failed = true;
        }

        void MarkOver()
        {
            overLimit = true;
            failed = true;
        }

        bool PushField(char value)
        {
            if (field.size() >= kExportFieldLimit)
            {
                MarkOver();
                return false;
            }
            field.push_back(value);
            return true;
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
            if (value == '\r')
            {
                Fail();
                return;
            }
            if (value == '\n')
            {
                EndNormalizedLine();
                return;
            }
            PushField(value);
        }

        void EndNormalizedLine()
        {
            if (failed)
                return;
            if (field.empty())
            {
                Fail();
                return;
            }
            if (field[0] == '#')
            {
                field.clear();
                return;
            }
            if (!headerDone)
            {
                if (field != "Id" && field != "id")
                    Fail();
                headerDone = true;
                field.clear();
                return;
            }
            std::int32_t id = 0;
            if (!ParseWholeId(field, id) || !registry.Add(id))
            {
                if (registry.OverLimit())
                    MarkOver();
                else
                    Fail();
            }
            field.clear();
        }

        void FeedCsv(char const value)
        {
            if (value == '\r')
            {
                Fail();
                return;
            }
            if (inQuote)
            {
                if (value == '"')
                {
                    inQuote = false;
                    afterQuote = true;
                    return;
                }
                PushField(value);
                return;
            }
            if (afterQuote)
            {
                if (value == '"')
                {
                    PushField('"');
                    inQuote = true;
                    afterQuote = false;
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
            PushField(value);
        }

        void EndField()
        {
            if (failed)
                return;
            if (!headerDone)
            {
                if (field.empty())
                    Fail();
                else if (fieldIndex == 0 && field != "Id" && field != "id")
                    Fail();
            }
            else if (fieldIndex == 0)
            {
                std::int32_t id = 0;
                if (field.empty() || !ParseWholeId(field, id) || !registry.Add(id))
                {
                    if (registry.OverLimit())
                        MarkOver();
                    else
                        Fail();
                }
            }
            field.clear();
            afterQuote = false;
            ++fieldIndex;
        }

        void EndRecord()
        {
            if (failed)
                return;
            if (fieldIndex == 0 || fieldIndex > static_cast<int>(kExportColumnLimit))
            {
                if (fieldIndex > static_cast<int>(kExportColumnLimit))
                    MarkOver();
                else
                    Fail();
                return;
            }
            if (!headerDone)
            {
                headerColumns = fieldIndex;
                headerDone = true;
            }
            else if (fieldIndex != headerColumns)
                Fail();
            fieldIndex = 0;
        }

        void FeedDump(char const value)
        {
            bool replay = true;
            for (int guard = 0; replay && guard < 4 && !failed; ++guard)
                replay = TakeDump(value);
            if (replay && !failed)
                Fail();
        }

        bool TakeDump(char const value)
        {
            if (inLineComment)
            {
                if (value == '\n')
                    inLineComment = false;
                return false;
            }
            if (inBlock)
            {
                if (sawStar && value == '/')
                    inBlock = false;
                sawStar = value == '*';
                return false;
            }
            if (inQuote)
            {
                if (escaped)
                {
                    escaped = false;
                    return Capture(value);
                }
                if (value == '\\')
                {
                    escaped = true;
                    return Capture(value);
                }
                if (value == quoteChar)
                    inQuote = false;
                return Capture(value);
            }
            if (inBacktick)
            {
                if (value == '`')
                {
                    inBacktick = false;
                    if (!skipping)
                        ApplyIdent();
                    return false;
                }
                if (!skipping && ident.size() >= 64)
                {
                    MarkOver();
                    return false;
                }
                if (!skipping)
                    ident.push_back(value);
                return false;
            }
            if (readingIdent)
            {
                if (IsIdentChar(value))
                {
                    if (ident.size() >= 64)
                    {
                        MarkOver();
                        return false;
                    }
                    ident.push_back(value);
                    return false;
                }
                readingIdent = false;
                ApplyIdent();
                return true;
            }
            if (value == '-' && sawDash)
            {
                inLineComment = true;
                sawDash = false;
                return false;
            }
            sawDash = value == '-';
            if (value == '#')
            {
                inLineComment = true;
                return false;
            }
            if (value == '*' && sawSlash)
            {
                inBlock = true;
                sawStar = false;
                sawSlash = false;
                return false;
            }
            sawSlash = value == '/';
            if (value == '\'' || value == '"')
            {
                inQuote = true;
                quoteChar = value;
                return Capture(value);
            }
            if (value == '`')
            {
                inBacktick = true;
                ident.clear();
                return false;
            }
            if (skipping)
            {
                if (value == ';')
                {
                    skipping = false;
                    ResetStatement();
                }
                return false;
            }
            if (mode == DumpMode::InTuples && depth > 0)
                return TakePunct(value);
            if (IsIdentStart(value))
            {
                readingIdent = true;
                ident.assign(1, value);
                return false;
            }
            return TakePunct(value);
        }

        bool Capture(char const value)
        {
            if (!capturing)
            {
                if (fieldLength >= kExportFieldLimit)
                    MarkOver();
                else
                    ++fieldLength;
                return false;
            }
            if (token.size() >= kExportFieldLimit)
            {
                MarkOver();
                return false;
            }
            token.push_back(value);
            return false;
        }

        static bool IsIdentStart(char const value)
        {
            return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_';
        }

        static bool IsIdentChar(char const value)
        {
            return IsIdentStart(value) || (value >= '0' && value <= '9');
        }

        void ApplyIdent()
        {
            std::string const word = ident;
            ident.clear();
            if (mode == DumpMode::Idle)
            {
                if (word == "INSERT")
                    mode = DumpMode::AfterInsert;
                return;
            }
            if (mode == DumpMode::AfterInsert)
            {
                mode = word == "INTO" ? DumpMode::AfterInto : DumpMode::Idle;
                if (mode == DumpMode::Idle)
                    skipping = true;
                return;
            }
            if (mode == DumpMode::AfterInto)
            {
                firstName = word;
                mode = DumpMode::AfterFirstName;
                return;
            }
            if (mode == DumpMode::WantTable)
            {
                tableName = word;
                mode = DumpMode::AfterTable;
                return;
            }
            if (mode == DumpMode::InColumns)
            {
                statementColumns.push_back(word);
                if (statementColumns.size() > kExportColumnLimit)
                    MarkOver();
                mode = DumpMode::AfterColumn;
                return;
            }
            if (mode == DumpMode::WantValues)
            {
                if (word == "VALUES")
                    mode = DumpMode::InTuples;
                else
                    BeginSkip();
            }
        }

        bool TakePunct(char const value)
        {
            if (value == ' ' || value == '\t' || value == '\n' || value == '\r')
                return false;
            if (skipping)
            {
                if (value == ';')
                {
                    skipping = false;
                    ResetStatement();
                }
                return false;
            }
            if (value == ';')
            {
                if (mode == DumpMode::InTuples && depth == 0)
                    ResetStatement();
                else if (mode == DumpMode::InTuples || mode == DumpMode::WantValues)
                    Fail();
                else
                    ResetStatement();
                return false;
            }
            if (mode == DumpMode::AfterFirstName && value == '.')
            {
                mode = DumpMode::WantTable;
                return false;
            }
            if (mode == DumpMode::AfterFirstName && value == '(')
            {
                BeginSkip();
                return false;
            }
            if (mode == DumpMode::AfterTable && value == '(')
            {
                if (firstName == contract->schema && tableName == contract->table)
                    mode = DumpMode::InColumns;
                else
                    BeginSkip();
                return false;
            }
            if (mode == DumpMode::AfterColumn && value == ',')
            {
                mode = DumpMode::InColumns;
                return false;
            }
            if (mode == DumpMode::AfterColumn && value == ')')
            {
                if (statementColumns == contract->columns)
                    mode = DumpMode::WantValues;
                else
                    BeginSkip();
                return false;
            }
            if (mode != DumpMode::InTuples)
                return false;
            if (value == '(')
            {
                ++depth;
                if (depth == 1)
                    BeginTuple();
                return false;
            }
            if (depth == 0)
                return false;
            if (value == ',' && depth == 1)
            {
                EndTupleField();
                ++fieldIndex;
                BeginField();
                return false;
            }
            if (value == ')' && depth == 1)
            {
                EndTupleField();
                if (!tupleSawId)
                    Fail();
                depth = 0;
                return false;
            }
            if (value == ')' && depth > 1)
                --depth;
            else
                Capture(value);
            return false;
        }

        void BeginSkip()
        {
            skipping = true;
            mode = DumpMode::Idle;
            statementColumns.clear();
            capturing = false;
            depth = 0;
        }

        void ResetStatement()
        {
            mode = DumpMode::Idle;
            skipping = false;
            statementColumns.clear();
            firstName.clear();
            tableName.clear();
            depth = 0;
            capturing = false;
            token.clear();
        }

        void BeginTuple()
        {
            fieldIndex = 0;
            tupleSawId = false;
            BeginField();
        }

        void BeginField()
        {
            capturing = fieldIndex == idIndex;
            token.clear();
            fieldLength = 0;
        }

        void EndTupleField()
        {
            if (!capturing)
                return;
            std::int32_t id = 0;
            if (!ParseDumpId(token, id) || !registry.Add(id))
            {
                if (registry.OverLimit())
                    MarkOver();
                else
                    Fail();
            }
            tupleSawId = true;
            capturing = false;
            token.clear();
        }

        static bool ParseDumpId(std::string const& raw, std::int32_t& id)
        {
            std::size_t begin = 0;
            std::size_t end = raw.size();
            while (begin < end && (raw[begin] == ' ' || raw[begin] == '\t' || raw[begin] == '\n'))
                ++begin;
            while (end > begin && (raw[end - 1] == ' ' || raw[end - 1] == '\t' || raw[end - 1] == '\n'))
                --end;
            if (end - begin >= 2 && raw[begin] == '\'' && raw[end - 1] == '\'')
            {
                ++begin;
                --end;
            }
            return ParseWholeId(raw.substr(begin, end - begin), id);
        }

        ExportFormat format;
        DumpContract const* contract = nullptr;
        IdRegistry registry;
        bool failed = false;
        bool overLimit = false;
        bool inQuote = false;
        bool afterQuote = false;
        bool headerDone = false;
        bool inLineComment = false;
        bool inBlock = false;
        bool sawStar = false;
        bool sawDash = false;
        bool sawSlash = false;
        bool escaped = false;
        bool inBacktick = false;
        bool readingIdent = false;
        bool skipping = false;
        bool capturing = false;
        bool tupleSawId = false;
        char quoteChar = '\'';
        DumpMode mode = DumpMode::Idle;
        int depth = 0;
        int idIndex = -1;
        int fieldIndex = 0;
        int headerColumns = 0;
        std::size_t fieldLength = 0;
        std::string field;
        std::string ident;
        std::string token;
        std::string firstName;
        std::string tableName;
        std::vector<std::string> statementColumns;
    };

    inline void NoteSelection(ParsedExport& parsed, DumpContract const* contract)
    {
        if (contract == nullptr)
            return;
        parsed.schema = contract->schema;
        parsed.table = contract->table;
    }

    inline ParsedExport FinishParsed(OpenSslSha256& hasher, ExportParser& parser, ExpectedManifest const& expected,
        DumpContract const* contract)
    {
        ParsedExport parsed;
        parsed.productionReady = false;
        NoteSelection(parsed, contract);
        if (parser.OverLimit())
        {
            parsed.verdict = ExportVerdict::Bounded;
            return parsed;
        }
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

    inline bool ContractReady(ExportFormat format, DumpContract const* contract)
    {
        return format != ExportFormat::Dump || contract != nullptr;
    }

    inline ParsedExport ReadExportStream(std::istream& input, ExpectedManifest const& expected, std::size_t byteLimit,
        ExportFormat format, DumpContract const* contract = nullptr)
    {
        ParsedExport parsed;
        parsed.productionReady = false;
        NoteSelection(parsed, contract);
        if (!expected.present || !expected.rowsKnown || byteLimit == 0 || !ContractReady(format, contract))
            return parsed;
        if (input.bad())
        {
            parsed.verdict = ExportVerdict::ReadError;
            return parsed;
        }
        OpenSslSha256 hasher;
        ExportParser parser(format, contract);
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
            if (parser.OverLimit())
            {
                parsed.verdict = ExportVerdict::Bounded;
                return parsed;
            }
            seen += count;
        }
        return FinishParsed(hasher, parser, expected, contract);
    }

    inline ParsedExport ReadExportFile(std::string const& path, ExpectedManifest const& expected, std::size_t byteLimit,
        ExportFormat format, DumpContract const* contract = nullptr)
    {
        ParsedExport parsed;
        parsed.productionReady = false;
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return parsed;
        return ReadExportStream(input, expected, byteLimit, format, contract);
    }

    // Size-limited streaming reader for a caller-held dump. The span is walked
    // in 64-byte steps and refused past byteLimit. Statements are not executed.
    inline ParsedExport ConvertDump(std::span<std::uint8_t const> dump, DumpContract const& contract,
        ExpectedManifest const& expected, std::size_t byteLimit)
    {
        ParsedExport parsed;
        parsed.productionReady = false;
        NoteSelection(parsed, &contract);
        if (!expected.present || !expected.rowsKnown || byteLimit == 0)
            return parsed;
        if (dump.size() > byteLimit)
        {
            parsed.verdict = ExportVerdict::Bounded;
            return parsed;
        }
        OpenSslSha256 hasher;
        ExportParser parser(ExportFormat::Dump, &contract);
        std::size_t offset = 0;
        while (offset < dump.size())
        {
            std::size_t const count = std::min<std::size_t>(64, dump.size() - offset);
            if (!RoomFor(offset, count, byteLimit))
            {
                parsed.verdict = ExportVerdict::Bounded;
                return parsed;
            }
            std::span<std::uint8_t const> const chunk = dump.subspan(offset, count);
            if (!hasher.Update(chunk))
            {
                parsed.verdict = ExportVerdict::ReadError;
                return parsed;
            }
            parser.Feed(chunk);
            if (parser.OverLimit())
            {
                parsed.verdict = ExportVerdict::Bounded;
                return parsed;
            }
            offset += count;
        }
        return FinishParsed(hasher, parser, expected, &contract);
    }

    inline ParsedExport ReadHashedExport(std::span<std::uint8_t const> bytes, ExpectedManifest const& expected,
        std::size_t byteLimit, ExportFormat format, DumpContract const* contract = nullptr)
    {
        ParsedExport parsed;
        parsed.productionReady = false;
        NoteSelection(parsed, contract);
        if (!expected.present || !expected.rowsKnown || byteLimit == 0 || !ContractReady(format, contract))
            return parsed;
        if (bytes.size() > byteLimit)
        {
            parsed.verdict = ExportVerdict::Bounded;
            return parsed;
        }
        OpenSslSha256 hasher;
        ExportParser parser(format, contract);
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
        return FinishParsed(hasher, parser, expected, contract);
    }
}

#endif
