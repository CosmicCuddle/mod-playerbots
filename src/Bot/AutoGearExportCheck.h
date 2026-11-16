/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_EXPORT_CHECK_H
#define PLAYERBOTS_AUTOGEAR_EXPORT_CHECK_H

#include <cstddef>
#include <cstdint>
#include <span>

namespace PlayerbotsAutoGearSelection
{
    // Metadata agreement is not permission to gear bots from the file.
    enum class ExportVerdict : std::uint8_t
    {
        Unknown,
        Invalid,
        Bounded,
        MetadataChecked
    };

    struct Sha256Digest
    {
        std::uint8_t byte[32]{};

        constexpr bool operator==(Sha256Digest const& other) const
        {
            for (int i = 0; i < 32; ++i)
            {
                if (byte[i] != other.byte[i])
                    return false;
            }
            return true;
        }
    };

    inline Sha256Digest HashSha256(std::span<std::uint8_t const> message)
    {
        constexpr std::uint32_t k[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
            0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
            0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
            0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c,
            0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        auto rotr = [](std::uint32_t value, std::uint32_t bits)
        { return (value >> bits) | (value << (32 - bits)); };
        std::uint32_t hash[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
            0x5be0cd19};
        auto compress = [&](std::uint8_t const block[64])
        {
            std::uint32_t word[64]{};
            for (int i = 0; i < 16; ++i)
            {
                word[i] = (std::uint32_t{block[i * 4]} << 24) | (std::uint32_t{block[i * 4 + 1]} << 16) |
                          (std::uint32_t{block[i * 4 + 2]} << 8) | std::uint32_t{block[i * 4 + 3]};
            }
            for (int i = 16; i < 64; ++i)
            {
                std::uint32_t const s0 = rotr(word[i - 15], 7) ^ rotr(word[i - 15], 18) ^ (word[i - 15] >> 3);
                std::uint32_t const s1 = rotr(word[i - 2], 17) ^ rotr(word[i - 2], 19) ^ (word[i - 2] >> 10);
                word[i] = word[i - 16] + s0 + word[i - 7] + s1;
            }
            std::uint32_t a = hash[0];
            std::uint32_t b = hash[1];
            std::uint32_t c = hash[2];
            std::uint32_t d = hash[3];
            std::uint32_t e = hash[4];
            std::uint32_t f = hash[5];
            std::uint32_t g = hash[6];
            std::uint32_t h = hash[7];
            for (int i = 0; i < 64; ++i)
            {
                std::uint32_t const s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                std::uint32_t const ch = (e & f) ^ ((~e) & g);
                std::uint32_t const temp1 = h + s1 + ch + k[i] + word[i];
                std::uint32_t const s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                std::uint32_t const maj = (a & b) ^ (a & c) ^ (b & c);
                std::uint32_t const temp2 = s0 + maj;
                h = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
            }
            hash[0] += a;
            hash[1] += b;
            hash[2] += c;
            hash[3] += d;
            hash[4] += e;
            hash[5] += f;
            hash[6] += g;
            hash[7] += h;
        };
        std::uint8_t block[64]{};
        std::size_t filled = 0;
        std::uint64_t bits = 0;
        auto feed = [&](std::uint8_t value)
        {
            block[filled++] = value;
            if (filled == 64)
            {
                compress(block);
                filled = 0;
            }
        };
        auto absorb = [&](std::span<std::uint8_t const> chunk)
        {
            for (std::uint8_t value : chunk)
            {
                feed(value);
                bits += 8;
            }
        };
        absorb(message);
        feed(0x80);
        while (filled != 56)
            feed(0);
        for (int shift = 56; shift >= 0; shift -= 8)
            feed(static_cast<std::uint8_t>((bits >> shift) & 0xff));
        Sha256Digest digest;
        for (int i = 0; i < 8; ++i)
        {
            digest.byte[i * 4] = static_cast<std::uint8_t>(hash[i] >> 24);
            digest.byte[i * 4 + 1] = static_cast<std::uint8_t>(hash[i] >> 16);
            digest.byte[i * 4 + 2] = static_cast<std::uint8_t>(hash[i] >> 8);
            digest.byte[i * 4 + 3] = static_cast<std::uint8_t>(hash[i]);
        }
        return digest;
    }

    // Offline importer. Each chunk is compressed as it arrives.
    struct Sha256Stream
    {
        void Update(std::span<std::uint8_t const> chunk)
        {
            for (std::uint8_t value : chunk)
            {
                block[filled++] = value;
                bits += 8;
                if (filled == 64)
                {
                    Compress(block);
                    filled = 0;
                }
            }
        }

        Sha256Digest Finish()
        {
            std::uint64_t const messageBits = bits;
            std::uint8_t const marker = 0x80;
            Update(std::span<std::uint8_t const>(&marker, 1));
            std::uint8_t const zero = 0;
            while (filled != 56)
                Update(std::span<std::uint8_t const>(&zero, 1));
            for (int shift = 56; shift >= 0; shift -= 8)
            {
                std::uint8_t const lengthByte = static_cast<std::uint8_t>((messageBits >> shift) & 0xff);
                Update(std::span<std::uint8_t const>(&lengthByte, 1));
            }
            Sha256Digest digest;
            for (int i = 0; i < 8; ++i)
            {
                digest.byte[i * 4] = static_cast<std::uint8_t>(state[i] >> 24);
                digest.byte[i * 4 + 1] = static_cast<std::uint8_t>(state[i] >> 16);
                digest.byte[i * 4 + 2] = static_cast<std::uint8_t>(state[i] >> 8);
                digest.byte[i * 4 + 3] = static_cast<std::uint8_t>(state[i]);
            }
            return digest;
        }

        void Compress(std::uint8_t const block[64])
        {
            constexpr std::uint32_t k[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
                0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
                0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
                0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
                0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb,
                0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624,
                0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
                0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb,
                0xbef9a3f7, 0xc67178f2};
            auto rotr = [](std::uint32_t value, std::uint32_t bits)
            { return (value >> bits) | (value << (32 - bits)); };
            std::uint32_t word[64]{};
            for (int i = 0; i < 16; ++i)
            {
                word[i] = (std::uint32_t{block[i * 4]} << 24) | (std::uint32_t{block[i * 4 + 1]} << 16) |
                          (std::uint32_t{block[i * 4 + 2]} << 8) | std::uint32_t{block[i * 4 + 3]};
            }
            for (int i = 16; i < 64; ++i)
            {
                std::uint32_t const s0 = rotr(word[i - 15], 7) ^ rotr(word[i - 15], 18) ^ (word[i - 15] >> 3);
                std::uint32_t const s1 = rotr(word[i - 2], 17) ^ rotr(word[i - 2], 19) ^ (word[i - 2] >> 10);
                word[i] = word[i - 16] + s0 + word[i - 7] + s1;
            }
            std::uint32_t a = state[0];
            std::uint32_t b = state[1];
            std::uint32_t c = state[2];
            std::uint32_t d = state[3];
            std::uint32_t e = state[4];
            std::uint32_t f = state[5];
            std::uint32_t g = state[6];
            std::uint32_t h = state[7];
            for (int i = 0; i < 64; ++i)
            {
                std::uint32_t const s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                std::uint32_t const ch = (e & f) ^ ((~e) & g);
                std::uint32_t const temp1 = h + s1 + ch + k[i] + word[i];
                std::uint32_t const s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                std::uint32_t const maj = (a & b) ^ (a & c) ^ (b & c);
                std::uint32_t const temp2 = s0 + maj;
                h = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }

        std::uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
            0x5be0cd19};
        std::uint8_t block[64]{};
        std::size_t filled = 0;
        std::uint64_t bits = 0;
    };

    struct FileChunks
    {
        std::span<std::uint8_t const> parts[4]{};
        std::uint8_t count = 0;
    };

    inline Sha256Digest HashFile(FileChunks const& file)
    {
        Sha256Stream stream;
        for (std::uint8_t i = 0; i < file.count && i < 4; ++i)
            stream.Update(file.parts[i]);
        return stream.Finish();
    };

    // A checked manifest is still not a production gear source.
    inline constexpr bool ExportIsProductionReady(ExportVerdict)
    {
        return false;
    }

    struct WorldManifest
    {
        bool schemaNamed = false;
        bool versionKnown = false;
        bool digestKnown = false;
        Sha256Digest digest{};
        Sha256Digest expectedDigest{};
        std::uint32_t itemTemplateRows = 0;
        std::uint32_t expectedItemRows = 0;
        bool itemSpellsRequired = false;
        std::span<std::int32_t const> itemSpellIds{};
        std::size_t validationBound = 0;
    };

    struct SpellManifest
    {
        bool headerIsId = false;
        bool versionKnown = false;
        bool digestKnown = false;
        Sha256Digest digest{};
        Sha256Digest expectedDigest{};
        std::uint32_t spellRows = 0;
        std::uint32_t expectedSpellRows = 0;
        std::span<std::int32_t const> spellIds{};
        std::span<bool const> overrideKnown{};
        std::span<bool const> procKnown{};
        std::span<bool const> sourceKnown{};
        std::size_t validationBound = 0;
    };

    inline bool IndexedIds(std::span<std::int32_t const> ids, std::size_t bound, bool& overBound)
    {
        if (ids.size() > bound)
        {
            overBound = true;
            return false;
        }
        for (std::size_t i = 1; i < ids.size(); ++i)
        {
            if (ids[i] <= ids[i - 1])
                return false;
        }
        return true;
    }

    inline bool FindSorted(std::span<std::int32_t const> ids, std::int32_t id, std::size_t& index)
    {
        std::size_t low = 0;
        std::size_t high = ids.size();
        while (low < high)
        {
            std::size_t const mid = low + (high - low) / 2;
            if (ids[mid] == id)
            {
                index = mid;
                return true;
            }
            if (ids[mid] < id)
                low = mid + 1;
            else
                high = mid;
        }
        return false;
    }

    // World and spell files are checked as separate manifests. The id list
    // must already be sorted; this walk is linear and stops at the bound.
    inline ExportVerdict CheckExport(WorldManifest const& world, FileChunks const& worldFile,
        SpellManifest const& spells, FileChunks const& spellFile)
    {
        if (!world.schemaNamed || !world.versionKnown || !world.digestKnown || world.expectedItemRows == 0 ||
            !spells.headerIsId || !spells.versionKnown || !spells.digestKnown || spells.expectedSpellRows == 0 ||
            world.validationBound == 0 || spells.validationBound == 0)
            return ExportVerdict::Unknown;
        Sha256Digest const worldHash = HashFile(worldFile);
        Sha256Digest const spellHash = HashFile(spellFile);
        if (!(worldHash == world.expectedDigest))
            return ExportVerdict::Invalid;
        if (!(spellHash == spells.expectedDigest))
            return ExportVerdict::Invalid;
        if (world.itemTemplateRows != world.expectedItemRows || spells.spellRows != spells.expectedSpellRows)
            return ExportVerdict::Invalid;
        if (world.itemSpellsRequired && world.itemSpellIds.empty())
            return ExportVerdict::Invalid;
        bool overBound = false;
        if (!IndexedIds(spells.spellIds, spells.validationBound, overBound))
            return overBound ? ExportVerdict::Bounded : ExportVerdict::Invalid;
        for (std::int32_t spellId : spells.spellIds)
        {
            if (spellId <= 0)
                return ExportVerdict::Invalid;
        }
        if (spells.spellIds.size() != spells.spellRows || spells.overrideKnown.size() != spells.spellIds.size() ||
            spells.procKnown.size() != spells.spellIds.size() || spells.sourceKnown.size() != spells.spellIds.size())
            return ExportVerdict::Invalid;
        if (world.itemSpellIds.size() > world.validationBound)
            return ExportVerdict::Bounded;
        for (std::int32_t referenced : world.itemSpellIds)
        {
            if (referenced == 0)
                continue;
            if (referenced < 0)
                return ExportVerdict::Unknown;
            std::size_t index = 0;
            if (!FindSorted(spells.spellIds, referenced, index))
                return ExportVerdict::Unknown;
            if (!spells.overrideKnown[index] || !spells.procKnown[index] || !spells.sourceKnown[index])
                return ExportVerdict::Unknown;
        }
        return ExportVerdict::MetadataChecked;
    }
}

#endif
