#include "hann.h"
#include <array>
#include <algorithm>
#include <stdint.h>
#include <math.h>

using namespace bleeptools;

static constexpr uint32_t kHannCurveSize = 192;

static std::array<float, kHannCurveSize> hannCurve() {
  static float PI2 = 3.1415927410125732421875f * .5f;
  std::array<float, kHannCurveSize> slope { 0 };
  for (uint32_t i = 0; i < kHannCurveSize; i++) {
    auto sin = std::sin(PI2 * static_cast<float>(i) / static_cast<float>(kHannCurveSize - 1));
    slope[i] = std::clamp(sin * sin, 0.f, 1.f);
  }
  return slope;
}

static auto curve = hannCurve();

float Hann::point(const float norm_pos) const {
  auto pos = (kHannCurveSize - 1) * norm_pos;
  auto int_pos = static_cast<uint32_t>(pos);
  auto frac = pos - static_cast<float>(int_pos);
  auto n_pos = int_pos + 1;
  if (n_pos >= kHannCurveSize) n_pos = kHannCurveSize - 1;
  auto v = curve[int_pos];
  auto n = curve[n_pos];
  return std::clamp(v + frac * (n - v), 0.f, 1.f);
};
