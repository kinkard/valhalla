#include "mjolnir/tokenizer.h"

#include <gtest/gtest.h>

#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

using valhalla::mjolnir::Tokenizer;
using namespace std::string_view_literals;
using strings = std::vector<std::string>;

static_assert(std::ranges::forward_range<Tokenizer<char>>);
static_assert(std::ranges::forward_range<Tokenizer<std::string_view>>);
static_assert(std::forward_iterator<Tokenizer<char>::iterator>);

// the delimiter type picks the split flavor at compile time
static_assert(std::is_same_v<decltype(Tokenizer("a;b", ';')), Tokenizer<char>>);
static_assert(std::is_same_v<decltype(Tokenizer("a - b", " - "sv)), Tokenizer<std::string_view>>);

template <typename Delim> strings collect(const Tokenizer<Delim>& tokens) {
  strings out;
  for (std::string_view token : tokens) {
    out.emplace_back(token);
  }
  return out;
}

TEST(Tokenizer, CharDelimiter) {
  EXPECT_EQ(collect(Tokenizer("", ';')), (strings{""}));
  EXPECT_EQ(collect(Tokenizer("a", ';')), (strings{"a"}));
  EXPECT_EQ(collect(Tokenizer("a;b", ';')), (strings{"a", "b"}));
  EXPECT_EQ(collect(Tokenizer(";", ';')), (strings{"", ""}));
  EXPECT_EQ(collect(Tokenizer("a;", ';')), (strings{"a", ""}));
  EXPECT_EQ(collect(Tokenizer(";a", ';')), (strings{"", "a"}));
  EXPECT_EQ(collect(Tokenizer("a;;b", ';')), (strings{"a", "", "b"}));
  EXPECT_EQ(collect(Tokenizer("a|b;c", '|')), (strings{"a", "b;c"}));
}

TEST(Tokenizer, StringDelimiter) {
  EXPECT_EQ(collect(Tokenizer("a - b", " - "sv)), (strings{"a", "b"}));
  EXPECT_EQ(collect(Tokenizer("a - ", " - "sv)), (strings{"a", ""}));
  EXPECT_EQ(collect(Tokenizer(" - a", " - "sv)), (strings{"", "a"}));
  EXPECT_EQ(collect(Tokenizer("a -  - b", " - "sv)), (strings{"a", "", "b"}));
  EXPECT_EQ(collect(Tokenizer("a-b", " - "sv)), (strings{"a-b"}));
  EXPECT_EQ(collect(Tokenizer("", " - "sv)), (strings{""}));
}

TEST(Tokenizer, DelimiterOfAnyLength) {
  EXPECT_EQ(collect(Tokenizer("a<--separator-->b<--separator-->c", "<--separator-->"sv)),
            (strings{"a", "b", "c"}));
}

TEST(Tokenizer, SelfOverlappingDelimiter) {
  // matches are found left to right without overlap, like std::string_view::find
  EXPECT_EQ(collect(Tokenizer("aaa", "aa"sv)), (strings{"", "a"}));
  EXPECT_EQ(collect(Tokenizer("aaaa", "aa"sv)), (strings{"", "", ""}));
}

TEST(Tokenizer, TokensViewTheInput) {
  const std::string value = "a;b";
  auto it = Tokenizer(value, ';').begin();
  EXPECT_EQ((*it).data(), value.data());
}

TEST(Tokenizer, MultiPass) {
  // a forward range supports several independent passes over the same input
  Tokenizer tokens("a;b;c", ';');
  size_t pairs = 0;
  for (std::string_view outer : tokens) {
    for (std::string_view inner : tokens) {
      pairs += outer == inner;
    }
  }
  EXPECT_EQ(pairs, 3);
}

TEST(Tokenizer, PostIncrement) {
  Tokenizer tokens("a;b", ';');
  auto it = tokens.begin();
  EXPECT_EQ(*it++, "a");
  EXPECT_EQ(*it++, "b");
  EXPECT_EQ(it, tokens.end());
}

TEST(Tokenizer, RangesInterop) {
  EXPECT_EQ(std::ranges::distance(Tokenizer("a;b;c", ';')), 3);
  EXPECT_EQ(std::ranges::count_if(Tokenizer("a;;b;;c", ';'),
                                  [](std::string_view t) { return t.empty(); }),
            2);
}

TEST(Tokenizer, Count) {
  EXPECT_EQ(Tokenizer("", ';').count(), 1); // empty input is one empty token
  EXPECT_EQ(Tokenizer("a", ';').count(), 1);
  EXPECT_EQ(Tokenizer("a;b;c", ';').count(), 3);
  EXPECT_EQ(Tokenizer("a;;", ';').count(), 3);
  EXPECT_EQ(Tokenizer("a - b", " - "sv).count(), 2);

  // count() re-scans, so it agrees with a fresh iteration
  Tokenizer tokens("a;b;c;d", ';');
  EXPECT_EQ(tokens.count(), std::ranges::distance(tokens));
}

} // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
