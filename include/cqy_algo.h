#pragma once
#include <vector>
#include <optional>
#include <string_view>
#include <system_error>
#include <charconv>
#include <random>
#include <algorithm>

namespace cqy {


  struct algo {
    static std::vector<std::string_view> split(std::string_view s,
                                               std::string_view div = " ");
  
    static std::pair<std::string_view, std::string_view>
    split_one(std::string_view s, std::string_view div = " ");
  
    template <typename T> static std::optional<T> to_n(std::string_view s) {
      T n{};
      auto r = std::from_chars(s.data(), s.data() + s.size(), n);
      auto ec = std::make_error_code(r.ec);
      if (ec) {
        return std::nullopt;
      }
      return std::make_optional(n);
    }
  
    template <typename E> static constexpr auto to_underlying(E e) {
      return static_cast<std::underlying_type_t<E>>(e);
    }
  
    template <typename T> static void deleter(T *t) { delete t; }
  
    static std::default_random_engine& random_engine();

    static bool random_bernoulli(double percent);
  };
}