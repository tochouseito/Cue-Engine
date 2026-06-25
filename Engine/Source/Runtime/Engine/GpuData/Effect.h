// Effect 用 GPU データの役割と公開要素を定義する

#pragma once

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::GpuData {
constexpr uint32_t k_maxEffectSpriteCount = 512;
constexpr uint32_t k_maxEffectRibbonCount = 256;
constexpr uint32_t k_maxEffectRibbonSegmentCount = 64;

struct EffectFrameGpu final {
  float deltaTime = 0.0f;
  float time = 0.0f;
  uint32_t spriteCount = 0;
  uint32_t ribbonCount = 0;
};

struct EffectSpriteGpu final {
  Math::float3 position = Math::float3::zero();
  float rotation = 0.0f;
  Math::float2 size = Math::float2(1.0f, 1.0f);
  Math::float2 uvMin = Math::float2(0.0f, 0.0f);
  Math::float2 uvMax = Math::float2(1.0f, 1.0f);
  uint32_t textureId = 0;
  uint32_t useTexture = 0;
  uint32_t blendMode = 0;
  uint32_t padding0 = 0;
  Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
};

struct EffectRibbonGpu final {
  Math::float3 startPosition = Math::float3::zero();
  float startWidth = 0.1f;
  Math::float3 endPosition = Math::float3::zero();
  float endWidth = 0.02f;
  Math::float4 startColor = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
  Math::float4 endColor = Math::float4(1.0f, 1.0f, 1.0f, 0.0f);
  Math::float2 uvScaleOffset = Math::float2(1.0f, 0.0f);
  uint32_t segmentCount = 1;
  uint32_t textureId = 0;
  uint32_t useTexture = 0;
  uint32_t blendMode = 0;
  uint32_t randomSeed = 1;
  float jitter = 0.0f;
};
} // namespace Cue::GpuData
