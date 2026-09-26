#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "../arc_parse/archive_parser.h"

namespace archive
{
    // Чтобы при падении теста GoogleTest печатал код результата
    void PrintTo(ParseResult result, std::ostream* os)
    {
        *os << "0x" << std::hex << static_cast<uint32_t>(result) << std::dec;
    }
} // namespace archive

namespace
{
    using archive::ParseResult;

    struct ArchiveSpec
        uint8_t flags = 0x00;
        uint32_t packedSize = 0;
        uint32_t unpackedSize = 0;
        std::string fileName = "file.txt";
        std::string comment = "comment";
        std::vector<uint8_t> extraField;
        std::vector<uint8_t> payload;
        bool terminateComment = true;
    };

    void AppendLe32(std::vector<uint8_t>& out, uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            out.push_back(static_cast<uint8_t>(value >> shift));
        }
    }

    // Собирает архив по раскладке из archive_parser.cpp
    std::vector<uint8_t> BuildArchive(const ArchiveSpec& spec)
    {
        std::vector<uint8_t> out = {'X', 'X', 0, 0, spec.flags};
        AppendLe32(out, spec.packedSize);
        AppendLe32(out, spec.unpackedSize);
        out.insert(out.end(), 8, 0);

        out.insert(out.end(), spec.fileName.begin(), spec.fileName.end());
        out.push_back(0);
        out.insert(out.end(), spec.comment.begin(), spec.comment.end());

        if (!spec.terminateComment)
        {
            return out; // файл обрывается посреди комментария
        }

        out.push_back(0);

        out.push_back(static_cast<uint8_t>(spec.extraField.size()));
        out.insert(out.end(), spec.extraField.begin(), spec.extraField.end());
        out.insert(out.end(), spec.payload.begin(), spec.payload.end());
        return out;
    }

    // Псевдослучайные байты вместо настоящих сжатых данных
    std::vector<uint8_t> RandomBytes(std::size_t size)
    {
        std::vector<uint8_t> bytes(size);
        uint32_t state = 0x12345678;
        for (uint8_t& byte: bytes)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            byte = static_cast<uint8_t>(state);
        }
        return bytes;
    }

    ArchiveSpec WithPayload(uint8_t flags, std::vector<uint8_t> payload)
    {
        ArchiveSpec spec;
        spec.flags = flags;
        spec.packedSize = static_cast<uint32_t>(payload.size());
        spec.unpackedSize = spec.packedSize * 2;
        spec.payload = std::move(payload);
        return spec;
    }

    struct ParseOutput
    {
        ParseResult result = ParseResult::NotRecognized;
        archive::ArchiveHeader header;
        archive::ArcInfo arcInfo;
    };

    ParseOutput Parse(std::vector<uint8_t> bytes)
    {
        ArchiveStream stream(std::move(bytes));
        ParseOutput output;
        output.result = archive::ParseArchiveHeader(stream, output.header, output.arcInfo);
        return output;
    }

    ParseOutput Parse(const ArchiveSpec& spec)
    {
        return Parse(BuildArchive(spec));
    }
} // namespace

TEST(ArchiveParser, ParsesStoredArchiveHeader)
{
    ArchiveSpec spec = WithPayload(0x00, {1, 2, 3, 4});
    spec.extraField = {9, 9, 9};
    const auto archiveSize = static_cast<uint32_t>(BuildArchive(spec).size());

    const ParseOutput output = Parse(spec);

    ASSERT_EQ(output.result, ParseResult::Recognized);
    EXPECT_EQ(output.header.version, 0);
    EXPECT_EQ(output.header.packedSize, 4u);
    EXPECT_EQ(output.header.unpackedSize, 8u);
    EXPECT_EQ(output.header.fileName, "file.txt");
    EXPECT_EQ(output.header.comment, "comment");
    EXPECT_EQ(output.header.dataOffset, archiveSize - 4);
}

TEST(ArchiveParser, FillsArcInfo)
{
    const ArchiveSpec spec = WithPayload(0x00, {1, 2, 3, 4});
    const auto archiveSize = static_cast<uint32_t>(BuildArchive(spec).size());

    const ParseOutput output = Parse(spec);

    ASSERT_EQ(output.result, ParseResult::Recognized);
    EXPECT_EQ(output.arcInfo.data[archive::kArcInfoFormatIdIndex], archive::kArchiveFormatId);
    EXPECT_EQ(output.arcInfo.data[archive::kArcInfoArchiveSizeIndex], archiveSize);
}

TEST(ArchiveParser, AcceptsRandomLookingCompressedData)
{
    EXPECT_EQ(Parse(WithPayload(0x01, RandomBytes(4096))).result, ParseResult::Recognized);
}

TEST(ArchiveParser, IgnoresUpperFlagBits)
{
    const ParseOutput output = Parse(WithPayload(0xF2, RandomBytes(4096)));

    ASSERT_EQ(output.result, ParseResult::Recognized);
    EXPECT_EQ(output.header.version, 2);
}

TEST(ArchiveParser, RejectsUniformCompressedData)
{
    EXPECT_EQ(Parse(WithPayload(0x01, std::vector<uint8_t>(4096, 'A'))).result, ParseResult::NotRecognized);
}

TEST(ArchiveParser, SkipsEntropyCheckOnSmallSample)
{
    EXPECT_EQ(Parse(WithPayload(0x01, std::vector<uint8_t>(100, 'A'))).result, ParseResult::Recognized);
}

TEST(ArchiveParser, RejectsUnknownVersion)
{
    EXPECT_EQ(Parse(WithPayload(0x03, {1, 2})).result, ParseResult::NotRecognized);
}

TEST(ArchiveParser, RejectsPackedSizeGreaterThanUnpacked)
{
    ArchiveSpec spec = WithPayload(0x00, {1, 2, 3, 4});
    spec.unpackedSize = 3;

    EXPECT_EQ(Parse(spec).result, ParseResult::NotRecognized);
}

TEST(ArchiveParser, RejectsDataBeyondEndOfFile)
{
    ArchiveSpec spec = WithPayload(0x00, {1, 2, 3, 4});
    spec.packedSize = 5;

    EXPECT_EQ(Parse(spec).result, ParseResult::NotRecognized);
}

// В 32 битах смещение + размер переполнилось бы и проверка прошла бы
TEST(ArchiveParser, RejectsOffsetPlusSizeOverflow)
{
    ArchiveSpec spec = WithPayload(0x00, {1, 2, 3, 4});
    spec.packedSize = 0xFFFFFFF0;
    spec.unpackedSize = 0xFFFFFFFF;

    EXPECT_EQ(Parse(spec).result, ParseResult::NotRecognized);
}

TEST(ArchiveParser, AcceptsMaxLengthFileName)
{
    ArchiveSpec spec = WithPayload(0x00, {});
    spec.fileName = std::string(archive::kMaxStringLength, 'n');

    const ParseOutput output = Parse(spec);

    ASSERT_EQ(output.result, ParseResult::Recognized);
    EXPECT_EQ(output.header.fileName, spec.fileName);
}

TEST(ArchiveParser, RejectsTooLongFileName)
{
    ArchiveSpec spec = WithPayload(0x00, {});
    spec.fileName = std::string(archive::kMaxStringLength + 1, 'n');

    EXPECT_EQ(Parse(spec).result, ParseResult::NotRecognized);
}

TEST(ArchiveParser, ReportsBrokenComment)
{
    ArchiveSpec spec = WithPayload(0x00, {});
    spec.terminateComment = false;

    EXPECT_EQ(Parse(spec).result, ParseResult::BrokenComment);
}

TEST(ArchiveParser, RejectsFileShorterThanFixedFields)
{
    EXPECT_EQ(Parse(std::vector<uint8_t>{'X', 'X', 0, 0, 0, 1, 0, 0}).result, ParseResult::NotRecognized);
}
