#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

// Входной поток архива. Заменяет глобальный arcIn и функций fSeek и fRead
// из исходного кода. Чтение происходит из памяти,
// чтобы парсер можно было собрать и протестировать "в вакууме"
class ArchiveStream
{
public:
    explicit ArchiveStream(std::vector<uint8_t> data)
        : data_(std::move(data))
    {
    }

    // Полный размер архива в байтах (бывший arcLen)
    [[nodiscard]] uint32_t Size() const
    {
        return static_cast<uint32_t>(data_.size());
    }

    void Seek(uint32_t offset)
    {
        position_ = offset;
    }

    // Читает не более destination.size() байт и возвращает сколько
    // прочитано по факту. Как fRead, если до конца файла осталось меньше,
    // чем просили, возвращает количество оставшихся байт
    [[nodiscard]] uint32_t Read(std::span<uint8_t> destination)
    {
        if (position_ >= data_.size())
        {
            return 0;
        }

        const std::size_t available = data_.size() - position_;
        const std::size_t count = std::min(destination.size(), available);
        std::memcpy(destination.data(), data_.data() + position_, count);
        position_ += count;
        return static_cast<uint32_t>(count);
    }

private:
    std::vector<uint8_t> data_ = {};
    std::size_t position_ = 0;
};
