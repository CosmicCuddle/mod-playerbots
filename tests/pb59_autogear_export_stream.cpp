#include "AutoGearExportReader.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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
}

int main()
{
    bool ok = true;
    std::uint32_t number = 0;
    ok = Check(ParseManifestUint("12", number) && number == 12, "a small manifest number is kept") && ok;
    ok = Check(!ParseManifestUint("4294967296", number), "a manifest number past 32 bits is rejected") && ok;
    ok = Check(!ParseManifestUint("12abc", number), "manifest junk is rejected") && ok;
    std::int32_t id = 0;
    ok = Check(ParseWholeId("100", id) && id == 100 && !ParseWholeId("100abc", id),
            "an id token rejects trailing junk") &&
         ok;

    std::string const csv = "Id,Note\n100,\"a,b\"\n200,\"line\nstill\"\n";
    ParsedExport const quoted = ReadText(csv, 2, 1024, ExportFormat::Csv);
    ok = Check(quoted.verdict == ExportVerdict::MetadataChecked && quoted.ids.size() == 2 && quoted.ids[0] == 100 &&
               quoted.ids[1] == 200,
            "quoted commas and newlines stay in the field") &&
         ok;
    ok = Check(ReadText("Id\n100abc\n", 1, 1024, ExportFormat::Csv).verdict == ExportVerdict::Invalid,
            "a malformed id delimiter is invalid") &&
         ok;
    ok = Check(ReadText("Id\n100\n100\n", 2, 1024, ExportFormat::Csv).verdict == ExportVerdict::Invalid,
            "a duplicate id is rejected") &&
         ok;
    ok = Check(ReadText("Id\n100\n200\n", 2, 4, ExportFormat::Csv).verdict == ExportVerdict::Bounded,
            "a tiny byte limit stops the read") &&
         ok;
    std::string twoChunks(80, 'x');
    twoChunks.replace(0, 3, "Id\n");
    ok = Check(ReadText(twoChunks, 0, 70, ExportFormat::Csv).verdict == ExportVerdict::Bounded,
            "a later chunk past the limit does not wrap the counter") &&
         ok;

    std::string const dump = "-- comment\nCREATE TABLE `spell` (id int);\n"
                             "INSERT INTO `other`.`spell` (`id`) VALUES (1);\n"
                             "INSERT INTO `acore_world`.`spell` (`id`,`name`) VALUES (100,'a,b'),(200,'c');\n";
    DumpContract const spell{"acore_world", "spell", {"id", "name"}, "id"};
    ParsedExport const inserted = ReadText(dump, 2, 1024, ExportFormat::Dump, &spell);
    ok = Check(inserted.verdict == ExportVerdict::MetadataChecked && inserted.ids.size() == 2 &&
               inserted.ids[0] == 100 && inserted.ids[1] == 200 && inserted.schema == "acore_world" &&
               inserted.table == "spell" && inserted.digest == ManifestFor(dump, 2).digest &&
               !inserted.productionReady,
            "only the named table is read and the source fingerprint is kept") &&
         ok;
    std::vector<std::uint8_t> const dumpBytes(dump.begin(), dump.end());
    ParsedExport const converted = ConvertDump(dumpBytes, spell, ManifestFor(dump, 2), 1024);
    ok = Check(converted.verdict == ExportVerdict::MetadataChecked && converted.ids.size() == 2 &&
               converted.table == "spell" && !converted.productionReady,
            "the dump converter is size-limited and does not execute SQL") &&
         ok;
    ok = Check(ConvertDump(dumpBytes, spell, ManifestFor(dump, 2), 8).verdict == ExportVerdict::Bounded,
            "a dump past the converter limit is not read") &&
         ok;
    ok = Check(ReadText("INSERT INTO `acore_world`.`spell` (`id`,`name`) VALUES (100junk,'a');\n", 1, 1024,
                   ExportFormat::Dump, &spell)
                   .verdict == ExportVerdict::Invalid,
            "junk after an insert id is invalid") &&
         ok;
    std::string const normalized = "# pb59-normalized 1\nId\n100\n200\n";
    ParsedExport const lines = ReadText(normalized, 2, 1024, ExportFormat::Normalized);
    ok = Check(lines.verdict == ExportVerdict::MetadataChecked && lines.ids.size() == 2 && lines.ids[1] == 200,
            "a normalized id list is accepted without executing SQL") &&
         ok;

    std::stringstream broken;
    broken.setstate(std::ios::badbit);
    ExpectedManifest expected;
    expected.present = true;
    expected.rowsKnown = true;
    expected.rows = 1;
    ParsedExport const failed = ReadExportStream(broken, expected, 100, ExportFormat::Csv);
    ok = Check(failed.verdict == ExportVerdict::ReadError, "a read error is not an end of file") && ok;

    std::stringstream empty;
    ParsedExport const ended = ReadExportStream(empty, expected, 100, ExportFormat::Csv);
    ok = Check(ended.verdict != ExportVerdict::ReadError, "a clean end of file is not a read error") && ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
