#include "archive_parser.h"

#include <algorithm>
#include <span>
#include <vector>

namespace archive
{
    namespace
    {
        // Раскладка заголовка (little-endian)
        //
        //  смещение  размер  поле
        //  0x00      2       сигнатура (этой функцией не проверяется)
        //  0x02      2       неизвестное поле (в оригинале читалось в Count и не использовалось)
        //  0x04      1       флаги: младшие 4 бита - версия (0, 1 или 2)
        //  0x05      4       размер сжатых данных
        //  0x09      4       размер распакованных данных
        //  0x0D      8       не используется
        //  0x15      ≤ 256   имя файла, ASCIIZ
        //            ≤ 256   комментарий, ASCIIZ
        //            1       N - длина дополнительного поля
        //            N       дополнительное поле (пропускается)
        //            ...     сжатые данные, packedSize байт

        constexpr uint32_t kFlagsOffset = 0x04;
        constexpr uint32_t kPackedSizeOffset = 0x05;
        constexpr uint32_t kUnpackedSizeOffset = 0x09;
        constexpr uint32_t kFixedFieldsEnd = 0x0D; // конец полей, которые парсер реально читает
        constexpr uint32_t kFileNameOffset = 0x15;

        constexpr uint8_t kVersionMask = 0x0F;
        constexpr uint8_t kMaxVersion = 2;
        constexpr uint8_t kStoredVersion = 0; // для версии 0 проверка энтропии не выполняется

        // Максимальный размер заголовка до дополнительного поля: фиксированная часть,
        // две строки с '\0' и байт длины. Весь заголовок читается одним вызовом
        constexpr uint32_t kMaxHeaderSize = kFileNameOffset + 2 * (kMaxStringLength + 1) + 1;

        // Эвристика "данные похожи на сжатые"
        constexpr uint32_t kEntropySampleOffset = 0x05; // как в оригинале - выборка начинается сразу после байта флагов
        constexpr uint32_t kEntropySampleSize = 0x8000; // 32 КБ
        constexpr std::size_t kByteValueCount = 256;
        constexpr uint32_t kFixedPointMax = 0xFFFFFFFE; // "1.0" для частот в fixed-point
        // При равномерном распределении каждый байт встречается ~ N/256 раз
        // Окно "почти равномерной" частоты: (N/266 − 1, N/236), то есть примерно от −4% до +8% от N/256
        constexpr uint32_t kUpperFrequencyDivisor = 236;
        constexpr uint32_t kLowerFrequencyDivisor = 266;
        // На меньшей выборке статистика бессмысленна, а нижняя граница
        // (N * scale / 266 − scale) уходит ниже нуля и переполняется
        constexpr uint32_t kMinSampleSize = kLowerFrequencyDivisor;
        // Сколько значений байта должны попасть в окно, чтобы данные считались сжатыми
        constexpr uint32_t kMinNearUniformValues = 15;

        using ByteHistogram = std::array<uint32_t, kByteValueCount>;

        [[nodiscard]] uint32_t ReadLe32(std::span<const uint8_t> bytes, uint32_t offset)
        {
            return static_cast<uint32_t>(bytes[offset])
                   | static_cast<uint32_t>(bytes[offset + 1]) << 8
                   | static_cast<uint32_t>(bytes[offset + 2]) << 16
                   | static_cast<uint32_t>(bytes[offset + 3]) << 24;
        }

        // Версия и размеры - cжатые данные не могут быть больше распакованных
        [[nodiscard]] bool ParseFixedFields(std::span<const uint8_t> bytes, ArchiveHeader& header)
        {
            if (bytes.size() < kFixedFieldsEnd)
            {
                return false;
            }

            header.version = static_cast<uint8_t>(bytes[kFlagsOffset] & kVersionMask);
            if (header.version > kMaxVersion)
            {
                return false;
            }

            header.packedSize = ReadLe32(bytes, kPackedSizeOffset);
            header.unpackedSize = ReadLe32(bytes, kUnpackedSizeOffset);
            return header.packedSize <= header.unpackedSize;
        }

        // Читает ASCIIZ-строку длиной до kMaxStringLength символов, начиная с offset
        // При успехе переводит offset за завершающий '\0'
        [[nodiscard]] bool ReadCString(std::span<const uint8_t> bytes, uint32_t& offset, std::string& out)
        {
            if (offset >= bytes.size())
            {
                return false;
            }

            const std::size_t windowSize = std::min(bytes.size() - offset, kMaxStringLength + 1);
            const auto window = bytes.subspan(offset, windowSize);
            const auto terminator = std::find(window.begin(), window.end(), uint8_t{0});
            if (terminator == window.end())
            {
                return false; // строка длиннее допустимого или файл оборвался
            }

            const auto length = static_cast<std::size_t>(terminator - window.begin());
            out.assign(reinterpret_cast<const char*>(window.data()), length);
            offset += static_cast<uint32_t>(length + 1);
            return true;
        }

        // Пропускает дополнительное поле - байт длины N и N байт данных
        [[nodiscard]] bool SkipExtraField(std::span<const uint8_t> bytes, uint32_t& offset)
        {
            if (offset >= bytes.size())
            {
                return false;
            }

            offset += 1 + static_cast<uint32_t>(bytes[offset]);
            return true;
        }

        [[nodiscard]] ByteHistogram BuildHistogram(std::span<const uint8_t> sample)
        {
            ByteHistogram histogram{};
            for (const uint8_t value: sample)
            {
                ++histogram[value];
            }
            return histogram;
        }

        // Считает значения байта, частота которых близка к равномерной (~ N/256)
        [[nodiscard]] uint32_t CountNearUniformValues(const ByteHistogram& histogram, uint32_t sampleSize)
        {
            // Частоты переводятся в fixed-point, где sampleSize * scale ~ 2^32, что соответствует 1.0
            // Так границы считаются в целых числах - count * scale ≤ kFixedPointMax, нет переполнения
            const uint32_t scale = kFixedPointMax / sampleSize;
            const uint32_t total = sampleSize * scale;
            const uint32_t upperBound = total / kUpperFrequencyDivisor;
            const uint32_t lowerBound = total / kLowerFrequencyDivisor - scale;

            const auto nearUniform = std::count_if(histogram.begin(), histogram.end(), [&](uint32_t count)
            {
                const uint32_t frequency = count * scale;
                return frequency > lowerBound && frequency < upperBound;
            });
            return static_cast<uint32_t>(nearUniform);
        }

        // Хорошо сжатый поток выглядит почти случайным: все значения байта встречаются
        // примерно одинаково часто. Если таких значений слишком мало, то это не сжатые данные
        bool LooksLikeCompressedData(ArchiveStream& archive)
        {
            std::vector<uint8_t> sample(kEntropySampleSize);
            archive.Seek(kEntropySampleOffset);
            const uint32_t sampleSize = archive.Read(sample);

            if (sampleSize <= kMinSampleSize)
            {
                return true; // данных слишком мало для статистики, нельзя точно сказать, что данные сжаты или нет
            }

            const ByteHistogram histogram = BuildHistogram(std::span(sample).first(sampleSize));
            return CountNearUniformValues(histogram, sampleSize) >= kMinNearUniformValues;
        }
    } // namespace

    // Весь заголовок читается одним вызовом вместо побайтового чтения строк
    // и отдельных Seek/Read на каждое поле. Если файл короче, прочитается
    // меньше байт, и проверки границ ниже вернут ошибку, как в оригинале
    ParseResult ParseArchiveHeader(ArchiveStream& archive, ArchiveHeader& header, ArcInfo& arcInfo)
    {
        std::array<uint8_t, kMaxHeaderSize> headerBuffer{};
        archive.Seek(0);
        const uint32_t bytesRead = archive.Read(headerBuffer);
        const auto bytes = std::span<const uint8_t>(headerBuffer).first(bytesRead);

        if (!ParseFixedFields(bytes, header))
        {
            return ParseResult::NotRecognized;
        }

        uint32_t offset = kFileNameOffset;
        if (!ReadCString(bytes, offset, header.fileName))
        {
            return ParseResult::NotRecognized;
        }

        // В оригинале комментарий читался в тот же буфер xxxFileName и затирал
        // имя файла. Здесь это два разных поля
        if (!ReadCString(bytes, offset, header.comment))
        {
            return ParseResult::BrokenComment;
        }

        if (!SkipExtraField(bytes, offset))
        {
            return ParseResult::NotRecognized;
        }

        header.dataOffset = offset;

        // Сжатые данные должны целиком помещаться в файл. Сумма считается в 64 битах -
        // в оригинале pFile + cSize могли переполниться и пропустить битый заголовок
        if (uint64_t{header.dataOffset} + header.packedSize > archive.Size())
        {
            return ParseResult::NotRecognized;
        }

        arcInfo.data[kArcInfoFormatIdIndex] = kArchiveFormatId;
        arcInfo.data[kArcInfoArchiveSizeIndex] = archive.Size();

        if (header.version != kStoredVersion && !LooksLikeCompressedData(archive))
        {
            return ParseResult::NotRecognized;
        }

        return ParseResult::Recognized;
    }
} // namespace archive
