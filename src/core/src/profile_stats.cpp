#include <atlantis/profile_stats.h>

#include <atlantis/assert.h>

#include <algorithm>
#include <vector>

namespace atlantis::profile {

double nearestRank(std::span<const double> sortedAscending, unsigned percent) {
  ATLANTIS_CHECK_MSG(!sortedAscending.empty(), "nearestRank() needs at least one sample");
  ATLANTIS_CHECK_MSG(percent >= 1 && percent <= 100, "nearestRank(): percent must be in [1, 100]");
  const std::size_t n = sortedAscending.size();
  // ceil(percent * n / 100) in integers, so no floating-point rounding decides a rank.
  const std::size_t rank = (static_cast<std::size_t>(percent) * n + 99) / 100;
  return sortedAscending[(rank == 0 ? 1 : rank) - 1];
}

Stats computeStats(std::span<const double> samples) {
  Stats stats;
  if (samples.empty()) return stats;
  std::vector<double> sorted(samples.begin(), samples.end());
  std::sort(sorted.begin(), sorted.end());
  double sum = 0.0;
  for (const double v : sorted) sum += v;
  stats.n = sorted.size();
  stats.avg = sum / static_cast<double>(stats.n);
  stats.p50 = nearestRank(sorted, 50);
  stats.p95 = nearestRank(sorted, 95);
  stats.p99 = nearestRank(sorted, 99);
  stats.min = sorted.front();
  stats.max = sorted.back();
  return stats;
}

double median(std::span<const double> samples) {
  if (samples.empty()) return 0.0;
  std::vector<double> sorted(samples.begin(), samples.end());
  std::sort(sorted.begin(), sorted.end());
  const std::size_t n = sorted.size();
  return n % 2 == 1 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
}

double medianAbsoluteDeviation(std::span<const double> samples) {
  if (samples.empty()) return 0.0;
  const double center = median(samples);
  std::vector<double> deviations;
  deviations.reserve(samples.size());
  for (const double v : samples) deviations.push_back(v < center ? center - v : v - center);
  return median(deviations);
}

double robustSigma(std::span<const double> samples) { return 1.4826 * medianAbsoluteDeviation(samples); }

}  // namespace atlantis::profile
