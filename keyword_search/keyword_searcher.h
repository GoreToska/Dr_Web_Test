#pragma once

#include <cstddef>
#include <cstdint>

namespace keyword_search
{
    // Искомые строки
    enum class Keyword : uint8_t
    {
        Company,
        AtnAuthor,
        DataField,
        Generator,
        AtnDate,
        GridTbl,
        AtnId,
        G,
        CreaTim,
        FtnSepC,
        Count,
    };

    inline constexpr std::size_t kKeywordCount = static_cast<std::size_t>(Keyword::Count);

    // Текст ключевого слова. Для Keyword::Count - nullptr
    [[nodiscard]] const char* KeywordText(Keyword keyword) noexcept;

    // Длина ключевого слова без нуль терминатора. Для Keyword::Count - 0
    [[nodiscard]] std::size_t KeywordLength(Keyword keyword) noexcept;

    struct Match
    {
        std::size_t offset = 0; // смещение начала совпадения в буфере
        Keyword keyword = Keyword::Count;
    };

    // Поиск всех вхождений ключевых слов в буфере за один проход (алгоритм Ахо-Корасик)
    // https://ru.wikipedia.org/wiki/%D0%90%D0%BB%D0%B3%D0%BE%D1%80%D0%B8%D1%82%D0%BC_%D0%90%D1%85%D0%BE_%E2%80%94_%D0%9A%D0%BE%D1%80%D0%B0%D1%81%D0%B8%D0%BA
    // https://cp-algorithms.com/string/aho_corasick.html
    // Автомат строится в конструкторе и хранится внутри объекта в массивах фиксированного
    // размера, поэтому ни конструктор, ни поиск не выделяют память и следовательно не бросают исключений
    //
    // Объект создается только динамически через Create(). Память выделяется вручную,
    // объект конструируется в ней через placement new. Уничтожать через Destroy()
    class KeywordSearcher
    {
    public:
        // Выделяет память и конструирует объект. При нехватке памяти возвращает nullptr
        [[nodiscard]] static KeywordSearcher* Create() noexcept;

        // Вызывает деструктор и освобождает память. nullptr допустим
        static void Destroy(KeywordSearcher* searcher) noexcept;

        KeywordSearcher(const KeywordSearcher&) = delete;

        KeywordSearcher& operator=(const KeywordSearcher&) = delete;

        // Ищет все вхождения, включая перекрывающиеся (например "g" внутри "generator").
        // Сравнение побайтовое, с учетом регистра. Буфер не обязан заканчиваться нулем.
        //
        // В matches записывается не более capacity совпадений, упорядоченных по позиции
        // конца совпадения. Возвращает общее число найденных совпадений - оно может быть
        // больше capacity, тогда можно повторить вызов с массивом нужного размера.
        // matches может быть nullptr при capacity == 0
        [[nodiscard]] std::size_t FindAll(const char* buffer, std::size_t size,
                                          Match* matches, std::size_t capacity) const noexcept;

        // Есть ли в буфере хотя бы одно ключевое слово
        [[nodiscard]] bool ContainsAny(const char* buffer, std::size_t size) const noexcept;

    private:
        using State = uint8_t;

        // Состояний не больше, чем символов во всех словах, плюс корень
        static constexpr std::size_t kMaxStates = 69;
        static constexpr std::size_t kAlphabetSize = 256;
        static constexpr State kNoState = 0xFF;
        static_assert(kMaxStates < kNoState, "State не вмещает все состояния автомата");

        KeywordSearcher() noexcept;

        ~KeywordSearcher() = default;

        // Номер состояния, в котором заканчивается слово, или kNoState
        [[nodiscard]] State FirstOutput(State state) const noexcept;

        // Полная таблица переходов из любого состояния по любому байту
        State transitions_[kMaxStates][kAlphabetSize] = {};
        // Слово, которое заканчивается в состоянии, или Keyword::Count
        Keyword output_[kMaxStates] = {};
        // Ближайшее по цепочке суффиксных ссылок состояние, где заканчивается слово
        State outputLink_[kMaxStates] = {};
    };
} // namespace keyword_search
