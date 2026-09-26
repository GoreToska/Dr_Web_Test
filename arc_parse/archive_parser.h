#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "archive_stream.h"

namespace archive
{
    // Коды возврата. Числовые значения совпадают с исходником ParseXXX
    enum class ParseResult : uint32_t
    {
        NotRecognized = 0x0000, // не тот формат или повреждённый заголовок
        BrokenComment = 0x0020, // имя файла прочитано, но комментарий поврежден
        Recognized = 0x1000, // архив распознан
    };

    // Максимальная длина строкового поля заголовка без нуль терминатора
    inline constexpr std::size_t kMaxStringLength = 255;

    // Сведения об архиве (в оригинале lastArcInfo)
    // Полный размер массива в оригинале неизвестен, парсеру нужны индексы 6 и 10.
    inline constexpr std::size_t kArcInfoFieldCount = 16;
    inline constexpr std::size_t kArcInfoFormatIdIndex = 6; // lastArcInfo.data[6]
    inline constexpr std::size_t kArcInfoArchiveSizeIndex = 10; // lastArcInfo.data[10]
    inline constexpr uint32_t kArchiveFormatId = 4; // значение, которое пишется в data[6]

    struct ArcInfo
    {
        std::array<uint32_t, kArcInfoFieldCount> data{};
    };

    // Разобранный заголовок архива
    struct ArchiveHeader
    {
        uint8_t version = 0; // младшие 4 бита байта флагов: 0, 1 или 2
        uint32_t packedSize = 0; // в оригинале cSize
        uint32_t unpackedSize = 0; // в оригинале uSize
        std::string fileName; // в оригинале xxxFileName
        std::string comment; // второе ASCIIZ-поле
        uint32_t dataOffset = 0; // смещение начала сжатых данных
    };

    // Разбирает заголовок архива и проверяет, что он согласован с размером файла
    // Для сжатых версий (1 и 2) дополнительно проверяет, что данные похожи на сжатые
    [[nodiscard]] ParseResult ParseArchiveHeader(ArchiveStream& archive, ArchiveHeader& header, ArcInfo& arcInfo);
} // namespace archive
