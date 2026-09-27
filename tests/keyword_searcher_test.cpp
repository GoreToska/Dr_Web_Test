#include <algorithm>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "../keyword_search/keyword_searcher.h"

namespace
{
    using keyword_search::Keyword;
    using keyword_search::KeywordSearcher;
    using keyword_search::Match;

    // Владеет объектом, созданным через KeywordSearcher::Create
    class SearcherTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            searcher_ = KeywordSearcher::Create();
            ASSERT_NE(searcher_, nullptr);
        }

        void TearDown() override
        {
            KeywordSearcher::Destroy(searcher_);
        }

        std::vector<Match> FindAll(const std::string& text) const
        {
            const std::size_t count = searcher_->FindAll(text.data(), text.size(), nullptr, 0);
            std::vector<Match> matches(count);
            EXPECT_EQ(searcher_->FindAll(text.data(), text.size(), matches.data(), matches.size()), count);
            return matches;
        }

        KeywordSearcher* searcher_ = nullptr;
    };

    // Наивный поиск каждого слова в каждой позиции.
    // Результат упорядочен так же, как у KeywordSearcher - по позиции конца совпадения
    std::vector<Match> NaiveFindAll(const std::string& text)
    {
        std::vector<Match> result;
        for (std::size_t end = 1; end <= text.size(); ++end)
        {
            // При одинаковом конце первым идет более длинное слово (как в цепочке outputLink_)
            std::vector<Match> endingHere;
            for (std::size_t index = 0; index < keyword_search::kKeywordCount; ++index)
            {
                const auto keyword = static_cast<Keyword>(index);
                const std::size_t length = keyword_search::KeywordLength(keyword);
                if (length <= end && text.compare(end - length, length, keyword_search::KeywordText(keyword)) == 0)
                {
                    endingHere.push_back({end - length, keyword});
                }
            }
            std::sort(endingHere.begin(), endingHere.end(),
                      [](const Match& a, const Match& b) { return a.offset < b.offset; });
            result.insert(result.end(), endingHere.begin(), endingHere.end());
        }
        return result;
    }

    void ExpectSameMatches(const std::vector<Match>& actual, const std::vector<Match>& expected)
    {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            EXPECT_EQ(actual[i].offset, expected[i].offset) << "match #" << i;
            EXPECT_EQ(actual[i].keyword, expected[i].keyword) << "match #" << i;
        }
    }

    TEST_F(SearcherTest, FindsEveryKeywordAlone)
    {
        for (std::size_t index = 0; index < keyword_search::kKeywordCount; ++index)
        {
            const auto keyword = static_cast<Keyword>(index);
            const std::string text = keyword_search::KeywordText(keyword);
            const std::vector<Match> matches = FindAll(text);

            // Слово "g" дополнительно находится внутри "generator" и "gridtbl"
            const bool startsWithG = keyword != Keyword::G && text[0] == 'g';
            ASSERT_EQ(matches.size(), startsWithG ? 2u : 1u) << text;
            EXPECT_EQ(matches.back().keyword, keyword) << text;
            EXPECT_EQ(matches.back().offset, 0u) << text;
        }
    }

    TEST_F(SearcherTest, EmptyAndNullBuffers)
    {
        EXPECT_EQ(searcher_->FindAll(nullptr, 100, nullptr, 0), 0u);
        EXPECT_EQ(searcher_->FindAll("", 0, nullptr, 0), 0u);
        EXPECT_FALSE(searcher_->ContainsAny(nullptr, 100));
        EXPECT_FALSE(searcher_->ContainsAny("", 0));
    }

    TEST_F(SearcherTest, NoMatches)
    {
        const std::string text = "The quick brown fox jumps over the lazy dot";
        EXPECT_TRUE(FindAll(text).empty());
        EXPECT_FALSE(searcher_->ContainsAny(text.data(), text.size()));
    }

    TEST_F(SearcherTest, CaseSensitive)
    {
        const std::string text = "COMPANY Company GENERATOR";
        EXPECT_TRUE(FindAll(text).empty());
    }

    TEST_F(SearcherTest, OverlappingMatches)
    {
        // "atnid" и "atndate" имеют общий префикс, "g" встречается внутри других слов
        ExpectSameMatches(FindAll("atnidatndategridtbl"),
                          {
                              {0, Keyword::AtnId},
                              {5, Keyword::AtnDate},
                              {12, Keyword::G},
                              {12, Keyword::GridTbl},
                          });
    }

    TEST_F(SearcherTest, RtfLikeBuffer)
    {
        const std::string text = R"({\info{\company Dr.Web}{\atnauthor Ivan}{\creatim\yr2024}})";
        const std::vector<Match> matches = FindAll(text);

        ASSERT_EQ(matches.size(), 3u);
        EXPECT_EQ(matches[0].keyword, Keyword::Company);
        EXPECT_EQ(matches[0].offset, text.find("company"));
        EXPECT_EQ(matches[1].keyword, Keyword::AtnAuthor);
        EXPECT_EQ(matches[1].offset, text.find("atnauthor"));
        EXPECT_EQ(matches[2].keyword, Keyword::CreaTim);
        EXPECT_EQ(matches[2].offset, text.find("creatim"));
    }

    TEST_F(SearcherTest, BufferIsNotNullTerminated)
    {
        // Поиск идет ровно по size байт - обрезанное слово не находится
        const char text[] = {'f', 't', 'n', 's', 'e', 'p', 'c'};
        EXPECT_EQ(searcher_->FindAll(text, sizeof(text), nullptr, 0), 1u);
        EXPECT_EQ(searcher_->FindAll(text, sizeof(text) - 1, nullptr, 0), 0u);
    }

    TEST_F(SearcherTest, BinaryDataWithZeroesAndHighBytes)
    {
        const std::string text("\x00\xFF" "datafield" "\x00\x80" "generator\xFE", 24);
        ExpectSameMatches(FindAll(text),
                          {
                              {2, Keyword::DataField},
                              {13, Keyword::G},
                              {13, Keyword::Generator},
                          });
    }

    TEST_F(SearcherTest, CapacitySmallerThanMatchCount)
    {
        const std::string text = "g g g g g";
        Match matches[2];
        EXPECT_EQ(searcher_->FindAll(text.data(), text.size(), matches, 2), 5u);
        EXPECT_EQ(matches[0].offset, 0u);
        EXPECT_EQ(matches[1].offset, 2u);

        // nullptr с ненулевой емкостью трактуется как "только подсчет"
        EXPECT_EQ(searcher_->FindAll(text.data(), text.size(), nullptr, 10), 5u);
    }

    TEST_F(SearcherTest, MatchesNaiveSearchOnRandomBuffers)
    {
        // Алфавит из букв ключевых слов, чтобы совпадения и частичные совпадения были частыми
        const std::string alphabet = "acdefgilmnoprstuyb";
        std::mt19937 random(12345);
        std::uniform_int_distribution<std::size_t> pick(0, alphabet.size() - 1);


        for (int round = 0; round < 200; ++round)
        {
            std::string text;
            for (int i = 0; i < 500; ++i)
            {
                text += alphabet[pick(random)];
                if (pick(random) == 0)
                {
                    // Иногда вставляем целое слово
                    text += keyword_search::KeywordText(static_cast<Keyword>(pick(random) % keyword_search::kKeywordCount));
                }
            }

            const std::vector<Match> expected = NaiveFindAll(text);
            ExpectSameMatches(FindAll(text), expected);
            EXPECT_EQ(searcher_->ContainsAny(text.data(), text.size()), !expected.empty());
        }
    }

    TEST(KeywordTest, TextAndLength)
    {
        EXPECT_STREQ(keyword_search::KeywordText(Keyword::Company), "company");
        EXPECT_STREQ(keyword_search::KeywordText(Keyword::FtnSepC), "ftnsepc");
        EXPECT_EQ(keyword_search::KeywordLength(Keyword::AtnAuthor), 9u);
        EXPECT_EQ(keyword_search::KeywordLength(Keyword::G), 1u);
        EXPECT_EQ(keyword_search::KeywordText(Keyword::Count), nullptr);
        EXPECT_EQ(keyword_search::KeywordLength(Keyword::Count), 0u);
    }

    TEST(KeywordSearcherLifetime, DestroyNullIsSafe)
    {
        KeywordSearcher::Destroy(nullptr);
    }
} // namespace
