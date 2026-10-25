// Plan 0067 M1 (Spec 0067 R1, ADR-0134 D1): the lit shaders' cofactor normal
// transform. Two GPU-free checks:
// - the drift test: the helper is repeated verbatim in all eleven lit vertex
//   shaders (no shader includes another), so each copy -- the text between
//   its BEGIN and END markers -- must be byte-identical to the reference text
//   beside this file, as the generated bindings are checked for staleness;
// - the CPU mirror: the reference formula, written out in C++, against an
//   independently computed inverse-transpose for random invertible matrices,
//   and against M * n for conformal ones.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace {

constexpr std::array<const char*, 11> kLitShaders{
    "lit_textured",       "pbr_direct_lit",       "pbr_direct_lit_normal_map",   "pbr_ibl",
    "pbr_ibl_normal_map", "pbr_clearcoat_ibl",    "pbr_clearcoat_ibl_normal_map", "pbr_sheen_ibl",
    "pbr_sheen_ibl_normal_map", "pbr_anisotropic_ibl", "pbr_anisotropic_ibl_normal_map"};

// The file's text with CRLF read as LF (a Windows checkout converts line endings).
[[nodiscard]] std::string readText(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  REQUIRE(file.good());
  std::ostringstream buffer;
  buffer << file.rdbuf();
  std::string text = buffer.str();
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
    out.push_back(text[i]);
  }
  return out;
}

// From the BEGIN marker's line through the END marker's line, newline included.
[[nodiscard]] std::string markedBlock(const std::string& text) {
  const std::size_t begin = text.find("// BEGIN cofactorNormal");
  const std::size_t endMarker = text.find("// END cofactorNormal");
  if (begin == std::string::npos || endMarker == std::string::npos || endMarker < begin) return {};
  const std::size_t end = text.find('\n', endMarker);
  return text.substr(begin, (end == std::string::npos ? text.size() : end + 1) - begin);
}

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<Vec3, 3>;  // columns

[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
[[nodiscard]] double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
[[nodiscard]] Vec3 normalize(const Vec3& v) {
  const double length = std::sqrt(dot(v, v));
  return {v[0] / length, v[1] / length, v[2] / length};
}
[[nodiscard]] Vec3 multiply(const Mat3& m, const Vec3& v) {  // m * v, m by columns
  Vec3 out{};
  for (int row = 0; row < 3; ++row) out[row] = m[0][row] * v[0] + m[1][row] * v[1] + m[2][row] * v[2];
  return out;
}

// The reference formula (cofactor_normal_reference.slang), in C++.
[[nodiscard]] Vec3 cofactorNormal(const Mat3& m, const Vec3& n) {
  const Vec3 c1xc2 = cross(m[1], m[2]);
  const Vec3 c2xc0 = cross(m[2], m[0]);
  const Vec3 c0xc1 = cross(m[0], m[1]);
  Vec3 normal{};
  for (int k = 0; k < 3; ++k) normal[k] = c1xc2[k] * n[0] + c2xc0[k] * n[1] + c0xc1[k] * n[2];
  if (dot(m[0], c1xc2) < 0.0) {
    for (double& x : normal) x = -x;
  }
  return normal;
}

// transpose(inverse(m)) * n by Gauss-Jordan elimination -- a different
// computation from the cofactor formula.
[[nodiscard]] Vec3 inverseTransposeNormal(const Mat3& m, const Vec3& n) {
  // Solve for inverse(m) row by row: augment the row-major matrix with I.
  double a[3][6] = {};
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) a[row][col] = m[col][row];
    a[row][3 + row] = 1.0;
  }
  for (int pivot = 0; pivot < 3; ++pivot) {
    int best = pivot;
    for (int row = pivot + 1; row < 3; ++row) {
      if (std::abs(a[row][pivot]) > std::abs(a[best][pivot])) best = row;
    }
    for (int col = 0; col < 6; ++col) std::swap(a[pivot][col], a[best][col]);
    const double divisor = a[pivot][pivot];
    for (int col = 0; col < 6; ++col) a[pivot][col] /= divisor;
    for (int row = 0; row < 3; ++row) {
      if (row == pivot) continue;
      const double factor = a[row][pivot];
      for (int col = 0; col < 6; ++col) a[row][col] -= factor * a[pivot][col];
    }
  }
  // inverse(m)[row][col] = a[row][3 + col]; transpose(inverse(m)) * n.
  Vec3 out{};
  for (int row = 0; row < 3; ++row) {
    for (int k = 0; k < 3; ++k) out[row] += a[k][3 + row] * n[k];
  }
  return out;
}

// Radians. acos() near 1 resolves only about 1.5e-8 in double precision, so
// agreement is judged well above that floor and far below any shading effect.
constexpr double kAngleTolerance = 1e-6;

[[nodiscard]] double angleBetween(const Vec3& a, const Vec3& b) {
  return std::acos(std::fmin(1.0, std::fmax(-1.0, dot(normalize(a), normalize(b)))));
}

}  // namespace

TEST_CASE("Each lit shader's cofactorNormal copy is byte-identical to the reference text",
          "[renderer][cofactor_normal]") {
  const std::string reference = readText(ATLANTIS_COFACTOR_NORMAL_REFERENCE);
  REQUIRE(markedBlock(reference) == reference);  // the reference is exactly one marked block
  for (const char* name : kLitShaders) {
    INFO("shader " << name);
    const std::filesystem::path path =
        std::filesystem::path(ATLANTIS_SHADER_SOURCE_DIR) / name / (std::string(name) + ".slang");
    const std::string text = readText(path);
    CHECK(markedBlock(text) == reference);
    // The vertex shader uses it, and the old direct transform is gone.
    CHECK(text.find("output.worldNormal = cofactorNormal(pushConstants.objectToWorld, input.normal);") !=
          std::string::npos);
    CHECK(text.find("mul((float3x3)pushConstants.objectToWorld, input.normal)") == std::string::npos);
  }
}

TEST_CASE("cofactorNormal has the inverse-transpose direction for invertible matrices",
          "[renderer][cofactor_normal]") {
  std::mt19937 random(0x0067u);
  std::uniform_real_distribution<double> entry(-3.0, 3.0);
  int tested = 0;
  while (tested < 2000) {
    Mat3 m{};
    for (auto& column : m) {
      for (double& x : column) x = entry(random);
    }
    // Well-conditioned enough that the double-precision reference is exact.
    const double volume = std::abs(dot(m[0], cross(m[1], m[2])));
    const double lengths = std::sqrt(dot(m[0], m[0]) * dot(m[1], m[1]) * dot(m[2], m[2]));
    if (volume < 0.05 * lengths) continue;
    const Vec3 n = normalize({entry(random), entry(random), entry(random)});
    CHECK(angleBetween(cofactorNormal(m, n), inverseTransposeNormal(m, n)) < kAngleTolerance);
    ++tested;
  }
  // Bistro's measured case, (s, s, 1) with s = 1.0244, and a strong shear.
  const Mat3 bistro{{{1.0244, 0.0, 0.0}, {0.0, 1.0244, 0.0}, {0.0, 0.0, 1.0}}};
  const Mat3 shear{{{1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
  const Vec3 n = normalize({1.0, 1.0, 1.0});
  CHECK(angleBetween(cofactorNormal(bistro, n), inverseTransposeNormal(bistro, n)) < kAngleTolerance);
  CHECK(angleBetween(cofactorNormal(shear, n), inverseTransposeNormal(shear, n)) < kAngleTolerance);
}

TEST_CASE("cofactorNormal has the direction of M * n for conformal matrices, of either sign",
          "[renderer][cofactor_normal]") {
  std::mt19937 random(0x0134u);
  std::uniform_real_distribution<double> unit(-1.0, 1.0);
  std::uniform_real_distribution<double> magnitude(0.01, 100.0);
  for (int i = 0; i < 2000; ++i) {
    // A random rotation from a random unit quaternion, times a random uniform scale of either sign.
    std::array<double, 4> q{unit(random), unit(random), unit(random), unit(random)};
    const double qLength = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (qLength < 1e-3) continue;
    for (double& x : q) x /= qLength;
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double s = magnitude(random) * (i % 2 == 0 ? 1.0 : -1.0);
    const Mat3 m{{{s * (1 - 2 * (y * y + z * z)), s * (2 * (x * y + z * w)), s * (2 * (x * z - y * w))},
                  {s * (2 * (x * y - z * w)), s * (1 - 2 * (x * x + z * z)), s * (2 * (y * z + x * w))},
                  {s * (2 * (x * z + y * w)), s * (2 * (y * z - x * w)), s * (1 - 2 * (x * x + y * y))}}};
    const Vec3 n = normalize({unit(random), unit(random), unit(random)});
    CHECK(angleBetween(cofactorNormal(m, n), multiply(m, n)) < kAngleTolerance);
  }
}
