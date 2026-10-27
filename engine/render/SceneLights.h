#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

enum class LightType : unsigned { Directional, Point, Spot };
enum class LightRenderingMode : unsigned { Forward, Tiled };
struct LightParameters {
  std::array<float, 3> color{1, 1, 1};  // linear radiance chromaticity
  float intensity = 2, range = 5, attenuation = .1f;
  float innerAngle = 20, outerAngle = 30;  // half angles in degrees
  bool enabled = true, castShadows = false;
  int shadowResolution = 1024;
  float shadowBias = .002f, shadowStrength = 1;
  bool shadowPcf = true;
  bool operator==(const LightParameters&) const = default;
};
struct SceneLightDescription {
  LightType type = LightType::Point;
  LightParameters parameters;
  bool primary = false;
};
inline LightParameters SanitizeLight(LightParameters p) {
  auto safe = [](float x, float fallback, float lo, float hi) {
    return std::clamp(std::isfinite(x) ? x : fallback, lo, hi);
  };
  for (auto& c : p.color) c = safe(c, 1, 0, 10000);
  p.intensity = safe(p.intensity, 0, 0, 100000);
  p.range = safe(p.range, 5, .01f, 10000);
  p.attenuation =
      safe(p.attenuation, .1f, .01f, 1000);  // near-source softening radius
  p.outerAngle = safe(p.outerAngle, 30, .1f, 89);
  p.innerAngle = safe(p.innerAngle, 20, 0, p.outerAngle);
  if(p.shadowResolution!=512&&p.shadowResolution!=1024&&p.shadowResolution!=2048)p.shadowResolution=1024;
  p.shadowBias=safe(p.shadowBias,.002f,0,.1f);
  p.shadowStrength=safe(p.shadowStrength,1,0,1);
  return p;
}
inline float LocalLightAttenuation(float distance, const LightParameters& raw) {
  const auto p = SanitizeLight(raw);
  if (!std::isfinite(distance) || distance < 0 || distance >= p.range) return 0;
  float x = distance / p.range, window = std::max(0.f, 1 - x * x * x * x);
  return window * window /
         std::max(distance * distance, p.attenuation * p.attenuation);
}
inline float SpotLightAttenuation(float cosine, const LightParameters& raw) {
  const auto p = SanitizeLight(raw);
  const float a = std::cos(p.innerAngle * .0174532925199433f),
              b = std::cos(p.outerAngle * .0174532925199433f);
  if (!std::isfinite(cosine)) return 0;
  if (a - b < 1e-6f) return cosine >= b ? 1.f : 0.f;
  float t = std::clamp((cosine - b) / (a - b), 0.f, 1.f);
  return t * t * (3 - 2 * t);
}
// Exact CPU/HLSL StructuredBuffer contract, five float4s; no ECS pointers/IDs
// on GPU.
struct alignas(16) GpuSceneLight {
  std::array<float, 4> positionRange{}, directionType{}, colorIntensity{},
      coneAttenuation{}, screenBounds{};
};
static_assert(sizeof(GpuSceneLight) == 80);
struct LightCamera {
  std::array<float, 16> view{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  std::array<float, 16> projection{1, 0, 0, 0, 0, 1, 0, 0,
                                   0, 0, 1, 0, 0, 0, 0, 1};
  float nearPlane = .01f, farPlane = 100;
};
struct SceneLightDiagnostics {
  unsigned total = 0, enabled = 0, directional = 0, point = 0, spot = 0,
           influencing = 0;
  unsigned tiles = 0, indices = 0, maxPerTile = 0, overflow = 0, capacity = 0;
  std::uint64_t uploadBytes = 0, gpuBytes = 0;
  double gatherMs = 0, cullingCpuMs = 0, cullingGpuMs = 0, averagePerTile = 0;
  bool primaryEcs = false, optimized = false;
  unsigned primaryIndex = UINT32_MAX, primaryGeneration = 0;
  std::string error;
};
// Conservative view-space sphere AABB projection. Near-crossing spheres cover
// all XY. No previous-frame depth. Spot spheres deliberately overestimate their
// cone.
inline std::array<float, 4> ProjectLightBounds(const GpuSceneLight& l,
                                               const LightCamera& c) {
  if (l.directionType[3] < .5f) return {0, 0, 1, 1};
  const auto& m = c.view;
  float x = l.positionRange[0], y = l.positionRange[1], z = l.positionRange[2],
        r = l.positionRange[3];
  float v[3] = {m[0] * x + m[4] * y + m[8] * z + m[12],
                m[1] * x + m[5] * y + m[9] * z + m[13],
                m[2] * x + m[6] * y + m[10] * z + m[14]};
  if (v[2] + r < c.nearPlane || v[2] - r > c.farPlane) return {2, 2, -1, -1};
  if (v[2] - r <= c.nearPlane) return {0, 0, 1, 1};
  std::array<float, 4> b{2, 2, -1, -1};
  for (int i = 0; i < 8; ++i) {
    float a = v[0] + (i & 1 ? r : -r), d = v[1] + (i & 2 ? r : -r),
          e = v[2] + (i & 4 ? r : -r);
    const auto& p = c.projection;
    float w = p[3] * a + p[7] * d + p[11] * e + p[15];
    if (!std::isfinite(w) || w <= 0) return {0, 0, 1, 1};
    float u = .5f + .5f * (p[0] * a + p[4] * d + p[8] * e + p[12]) / w;
    float t = .5f - .5f * (p[1] * a + p[5] * d + p[9] * e + p[13]) / w;
    b[0] = std::min(b[0], u);
    b[1] = std::min(b[1], t);
    b[2] = std::max(b[2], u);
    b[3] = std::max(b[3], t);
  }
  // Expand for raster numerical error at tile edges.
  b[0] -= 1e-5f;
  b[1] -= 1e-5f;
  b[2] += 1e-5f;
  b[3] += 1e-5f;
  return b;
}
