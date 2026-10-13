#include <atlantis/profile_stats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <vector>

// Spec 0059 / ADR-0114 D6: nearest-rank percentiles, checked against ranks
// worked out by hand.

using atlantis::profile::computeStats;
using atlantis::profile::nearestRank;
using Catch::Approx;

TEST_CASE("nearest rank on 1..100 is the value equal to the percentile", "[profile][stats]") {
  std::vector<double> samples(100);
  std::iota(samples.begin(), samples.end(), 1.0);
  const auto stats = computeStats(samples);
  CHECK(stats.n == 100);
  CHECK(stats.p50 == 50.0);
  CHECK(stats.p95 == 95.0);
  CHECK(stats.p99 == 99.0);
  CHECK(stats.min == 1.0);
  CHECK(stats.max == 100.0);
  CHECK(stats.avg == Approx(50.5));
}

TEST_CASE("nearest rank rounds the rank up, on an unsorted input", "[profile][stats]") {
  const std::vector<double> samples{3.0, 1.0, 2.0};  // sorted: 1 2 3
  const auto stats = computeStats(samples);
  CHECK(stats.p50 == 2.0);  // ceil(0.5 * 3) = 2
  CHECK(stats.p95 == 3.0);  // ceil(0.95 * 3) = 3
  CHECK(stats.p99 == 3.0);
  CHECK(samples[0] == 3.0);  // the caller's samples are untouched
}

TEST_CASE("nearest rank on 300 samples picks ranks 150, 285 and 297", "[profile][stats]") {
  std::vector<double> samples(300);
  std::iota(samples.begin(), samples.end(), 1.0);
  const auto stats = computeStats(samples);
  CHECK(stats.p50 == 150.0);
  CHECK(stats.p95 == 285.0);
  CHECK(stats.p99 == 297.0);
}

TEST_CASE("one sample is every percentile; no sample is n == 0", "[profile][stats]") {
  const std::vector<double> one{7.5};
  const auto stats = computeStats(one);
  CHECK(stats.n == 1);
  CHECK(stats.p50 == 7.5);
  CHECK(stats.p99 == 7.5);
  CHECK(stats.avg == 7.5);

  const auto none = computeStats({});
  CHECK(none.n == 0);
  CHECK(none.avg == 0.0);
  CHECK(none.p50 == 0.0);
}

TEST_CASE("nearestRank takes a sorted span directly", "[profile][stats]") {
  const std::vector<double> sorted{10.0, 20.0, 30.0, 40.0};
  CHECK(nearestRank(sorted, 50) == 20.0);  // ceil(2.0) = 2
  CHECK(nearestRank(sorted, 51) == 30.0);  // ceil(2.04) = 3
  CHECK(nearestRank(sorted, 100) == 40.0);
  CHECK(nearestRank(sorted, 1) == 10.0);
}

// Spec 0060 / ADR-0119 D7: the robust statistics, against values worked out by hand.

using atlantis::profile::median;
using atlantis::profile::medianAbsoluteDeviation;
using atlantis::profile::robustSigma;

TEST_CASE("the median of an odd count is the middle value, of an even count the mean of the middle two",
          "[profile][stats][robust]") {
  CHECK(median(std::vector<double>{5.0, 1.0, 3.0}) == 3.0);
  CHECK(median(std::vector<double>{4.0, 1.0, 3.0, 2.0}) == 2.5);
  CHECK(median(std::vector<double>{7.0}) == 7.0);
  CHECK(median(std::vector<double>{}) == 0.0);
}

TEST_CASE("the median ignores ties and leaves the caller's samples untouched", "[profile][stats][robust]") {
  const std::vector<double> samples{2.0, 2.0, 2.0, 9.0, 2.0};
  CHECK(median(samples) == 2.0);
  CHECK(samples[3] == 9.0);
}

TEST_CASE("the median absolute deviation is the median of the distances to the median", "[profile][stats][robust]") {
  // 1 2 3 4 100: median 3, deviations 2 1 0 1 97 -> sorted 0 1 1 2 97 -> MAD 1.
  const std::vector<double> samples{1.0, 2.0, 3.0, 4.0, 100.0};
  CHECK(medianAbsoluteDeviation(samples) == 1.0);
  CHECK(robustSigma(samples) == Approx(1.4826));
}

TEST_CASE("identical samples have a zero MAD and a zero robust sigma", "[profile][stats][robust]") {
  const std::vector<double> samples{4.0, 4.0, 4.0, 4.0};
  CHECK(medianAbsoluteDeviation(samples) == 0.0);
  CHECK(robustSigma(samples) == 0.0);
  CHECK(robustSigma(std::vector<double>{3.0}) == 0.0);
  CHECK(robustSigma(std::vector<double>{}) == 0.0);
}

TEST_CASE("one outlier barely moves the median or the MAD", "[profile][stats][robust]") {
  const std::vector<double> clean{10.0, 10.2, 9.8, 10.1, 9.9};
  std::vector<double> dirty = clean;
  dirty.push_back(1000.0);
  CHECK(median(dirty) == Approx(10.05));
  CHECK(medianAbsoluteDeviation(dirty) < 0.3);
}
