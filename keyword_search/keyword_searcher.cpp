#include "keyword_searcher.h"

#include <cstdlib>
#include <new>

namespace keyword_search
{
    namespace
    {
        constexpr const char* kKeywords[kKeywordCount] = {
            "company",
            "atnauthor",
            "datafield",
            "generator",
            "atndate",
            "gridtbl",
            "atnid",
            "g",
            "creatim",
            "ftnsepc",
        };

        constexpr std::size_t Length(const char* text)
        {
            std::size_t length = 0;
            while (text[length] != '\0')
            {
                ++length;
            }
            return length;
        }

        constexpr std::size_t TotalKeywordLength()
        {
            std::size_t total = 0;
            for (const char* keyword: kKeywords)
            {
                total += Length(keyword);
            }
            return total;
        }

        // Длины считаются при компиляции, чтобы не вызывать strlen на каждое совпадение
        constexpr auto kKeywordLengths = []
        {
            struct Lengths
            {
                std::size_t values[kKeywordCount] = {};
            } lengths;
            for (std::size_t index = 0; index < kKeywordCount; ++index)
            {
                lengths.values[index] = Length(kKeywords[index]);
            }
            return lengths;
        }();
    } // namespace

    const char* KeywordText(Keyword keyword) noexcept
    {
        const auto index = static_cast<std::size_t>(keyword);
        return index < kKeywordCount ? kKeywords[index] : nullptr;
    }

    std::size_t KeywordLength(Keyword keyword) noexcept
    {
        const auto index = static_cast<std::size_t>(keyword);
        return index < kKeywordCount ? kKeywordLengths.values[index] : 0;
    }

    KeywordSearcher* KeywordSearcher::Create() noexcept
    {
        static_assert(alignof(KeywordSearcher) <= alignof(std::max_align_t),
                      "malloc не гарантирует нужное выравнивание");

        void* memory = std::malloc(sizeof(KeywordSearcher));
        if (memory == nullptr)
        {
            return nullptr;
        }
        return new(memory) KeywordSearcher();
    }

    void KeywordSearcher::Destroy(KeywordSearcher* searcher) noexcept
    {
        if (searcher == nullptr)
        {
            return;
        }
        searcher->~KeywordSearcher();
        std::free(searcher);
    }

    KeywordSearcher::KeywordSearcher() noexcept
    {
        // В худшем случае (у слов нет общих префиксов) каждый символ дает новое состояние
        static_assert(TotalKeywordLength() + 1 <= kMaxStates,
                      "kMaxStates меньше, чем нужно для списка ключевых слов");

        constexpr State kRoot = 0;

        for (auto& row: transitions_)
        {
            for (State& next: row)
            {
                next = kNoState;
            }
        }
        for (std::size_t state = 0; state < kMaxStates; ++state)
        {
            output_[state] = Keyword::Count;
            outputLink_[state] = kNoState;
        }

        // Бор из ключевых слов. kNoState в transitions_ значит "ребра нет"
        std::size_t stateCount = 1;
        for (std::size_t index = 0; index < kKeywordCount; ++index)
        {
            State state = kRoot;
            for (const char* ch = kKeywords[index]; *ch != '\0'; ++ch)
            {
                State& next = transitions_[state][static_cast<unsigned char>(*ch)];
                if (next == kNoState)
                {
                    next = static_cast<State>(stateCount++);
                }
                state = next;
            }
            output_[state] = static_cast<Keyword>(index);
        }

        // Обход в ширину: суффиксные ссылки и достройка переходов до полного автомата.
        // Суффиксные ссылки нужны только здесь, в объекте остаются переходы и outputLink_
        State failure[kMaxStates] = {};
        State queue[kMaxStates] = {};
        std::size_t head = 0;
        std::size_t tail = 0;

        for (std::size_t symbol = 0; symbol < kAlphabetSize; ++symbol)
        {
            State& next = transitions_[kRoot][symbol];
            if (next == kNoState)
            {
                next = kRoot;
            }
            else
            {
                failure[next] = kRoot;
                queue[tail++] = next;
            }
        }

        while (head < tail)
        {
            const State state = queue[head++];
            for (std::size_t symbol = 0; symbol < kAlphabetSize; ++symbol)
            {
                State& next = transitions_[state][symbol];
                const State fallback = transitions_[failure[state]][symbol];
                if (next == kNoState)
                {
                    next = fallback;
                    continue;
                }

                failure[next] = fallback;
                outputLink_[next] = output_[fallback] != Keyword::Count ? fallback : outputLink_[fallback];
                queue[tail++] = next;
            }
        }
    }

    KeywordSearcher::State KeywordSearcher::FirstOutput(State state) const noexcept
    {
        return output_[state] != Keyword::Count ? state : outputLink_[state];
    }

    std::size_t KeywordSearcher::FindAll(const char* buffer, std::size_t size,
                                         Match* matches, std::size_t capacity) const noexcept
    {
        if (buffer == nullptr)
        {
            return 0;
        }
        if (matches == nullptr)
        {
            capacity = 0;
        }

        std::size_t found = 0;
        State state = 0;
        for (std::size_t position = 0; position < size; ++position)
        {
            state = transitions_[state][static_cast<unsigned char>(buffer[position])];
            for (State hit = FirstOutput(state); hit != kNoState; hit = outputLink_[hit])
            {
                if (found < capacity)
                {
                    const Keyword keyword = output_[hit];
                    matches[found].offset = position + 1 - KeywordLength(keyword);
                    matches[found].keyword = keyword;
                }
                ++found;
            }
        }
        return found;
    }

    bool KeywordSearcher::ContainsAny(const char* buffer, std::size_t size) const noexcept
    {
        if (buffer == nullptr)
        {
            return false;
        }

        State state = 0;
        for (std::size_t position = 0; position < size; ++position)
        {
            state = transitions_[state][static_cast<unsigned char>(buffer[position])];
            if (FirstOutput(state) != kNoState)
            {
                return true;
            }
        }
        return false;
    }
} // namespace keyword_search
