#pragma once

#include <cassert>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <type_traits>

namespace valhalla {
namespace mjolnir {

/**
 * A lazy tokenizer over a delimited string, splitting on a single character or a substring.
 * Yields std::string_view tokens without allocating: empty tokens are preserved and an empty
 * input yields a single empty token. Both the viewed string and a string delimiter must
 * outlive the iteration.
 *
 * The delimiter type picks the split flavor at compile time: a char delimiter deduces
 * naturally, a substring one is passed as std::string_view.
 *
 *   for (std::string_view token : Tokenizer(value, ';')) { ... }
 *   for (std::string_view token : Tokenizer(value, std::string_view(" - "))) { ... }
 */
template <typename Delim> class Tokenizer {
public:
  static_assert(std::is_same_v<Delim, char> || std::is_same_v<Delim, std::string_view>,
                "the delimiter is either a single char or a std::string_view");

  Tokenizer(std::string_view value, Delim delim) : value_(value), delim_(delim) {
    if constexpr (!std::is_same_v<Delim, char>) {
      assert(!delim.empty());
    }
  }

  class iterator {
  public:
    using value_type = std::string_view;
    using difference_type = std::ptrdiff_t;
    // operator* returns a value, so the legacy category stays input while the c++20 concept
    // allows multi-pass use with std::ranges
    using iterator_category = std::input_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    iterator() = default;

    std::string_view operator*() const {
      return value_.substr(token_start_, token_end_ - token_start_);
    }

    iterator& operator++() {
      if (token_end_ == value_.size()) {
        token_start_ = std::string_view::npos; // exhausted, matches the end() iterator
      } else {
        token_start_ = token_end_ + delim_size();
        find_token_end();
      }
      return *this;
    }

    iterator operator++(int) {
      auto copy = *this;
      ++*this;
      return copy;
    }

    bool operator==(const iterator& other) const {
      return token_start_ == other.token_start_;
    }
    bool operator!=(const iterator& other) const {
      return !(*this == other);
    }

  private:
    friend class Tokenizer;

    iterator(std::string_view value, Delim delim) : value_(value), delim_(delim), token_start_(0) {
      find_token_end();
    }

    constexpr size_t delim_size() const {
      if constexpr (std::is_same_v<Delim, char>) {
        return 1;
      } else {
        return delim_.size();
      }
    }

    void find_token_end() {
      auto pos = value_.find(delim_, token_start_);
      token_end_ = pos == std::string_view::npos ? value_.size() : pos;
    }

    std::string_view value_;
    Delim delim_{};
    size_t token_start_ = std::string_view::npos;
    size_t token_end_ = 0;
  };

  iterator begin() const {
    return {value_, delim_};
  }

  iterator end() const {
    return {};
  }

  // Number of tokens. O(n): the tokens aren't stored, so this re-scans the input. Deliberately not
  // named size() to keep this off the std::ranges::size path (that would advertise a cheap size).
  size_t count() const {
    size_t n = 0;
    for (auto it = begin(), stop = end(); it != stop; ++it) {
      ++n;
    }
    return n;
  }

private:
  std::string_view value_;
  Delim delim_;
};

} // namespace mjolnir
} // namespace valhalla
