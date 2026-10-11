#pragma once

#include <cstddef>
#include <span>

// Spec 0059 / ADR-0114 D6: the one definition of the benchmark's statistics.
// Percentiles are nearest rank on the ascending sorted samples:
// index = ceil(p * n / 100) - 1. Pure functions; safe for concurrent use.
namespace atlantis::profile {

struct Stats {
  std::size_t n = 0;  // 0: every other field is 0 and means "no samples"
  double avg = 0.0;
  double p50 = 0.0;
  double p95 = 0.0;
  double p99 = 0.0;
  double min = 0.0;
  double max = 0.0;
};

// `sortedAscending` must be sorted and non-empty; `percent` in [1, 100].
[[nodiscard]] double nearestRank(std::span<const double> sortedAscending, unsigned percent);

// Sorts a copy of `samples`.
[[nodiscard]] Stats computeStats(std::span<const double> samples);

}  // namespace atlantis::profile
