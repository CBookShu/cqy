#include "cqy_algo.h"

#include <ranges>

using namespace cqy;

std::vector<std::string_view> algo::split(std::string_view s,
                                          std::string_view div) {
  auto sp = s | std::views::split(div);
  std::vector<std::string_view> res;
  for (auto &&sub_range : sp) {
    res.emplace_back(sub_range.begin(), sub_range.end());
  }
  return res;
}

std::pair<std::string_view, std::string_view>
algo::split_one(std::string_view s, std::string_view div) {
  auto pos = s.find(div);
  if (pos == std::string_view::npos) {
    return std::make_pair(s, "");
  } else {
    return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
  }
}

std::default_random_engine& algo::random_engine() {
  static std::default_random_engine e(std::random_device{}());
  return (e);
}

bool algo::random_bernoulli(double percent) {
  percent = std::clamp(percent, 0.0, 100.0);
  std::bernoulli_distribution u(percent / 100.0);
  return u(random_engine());
}