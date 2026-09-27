#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "keyword_search/keyword_searcher.h"

int main()
{
    using keyword_search::KeywordSearcher;
    using keyword_search::Match;

    // Create() выделяет память через malloc и конструирует объект через placement new
    KeywordSearcher* searcher = KeywordSearcher::Create();
    if (searcher == nullptr)
    {
        std::fputs("Not enough memory\n", stderr);
        return EXIT_FAILURE;
    }

    const char buffer[] =
        R"({\rtf1{\info{\company Dr.Web}{\atnauthor Ivan}{\creatim\yr2024}{\generator Riched20}}})";
    const std::size_t size = std::strlen(buffer);

    constexpr std::size_t kCapacity = 32;
    Match matches[kCapacity];
    const std::size_t found = searcher->FindAll(buffer, size, matches, kCapacity);

    std::printf("Found %zu match(es)\n", found);
    for (std::size_t i = 0; i < found && i < kCapacity; ++i)
    {
        std::printf("  offset %3zu: %s\n", matches[i].offset, keyword_search::KeywordText(matches[i].keyword));
    }

    KeywordSearcher::Destroy(searcher);
    return EXIT_SUCCESS;
}
