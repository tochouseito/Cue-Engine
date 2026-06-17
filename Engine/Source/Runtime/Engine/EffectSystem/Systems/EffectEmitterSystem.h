// EffectEmitterSystem の役割と公開要素を定義する

#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <Asset/AssetManager.h>
#include <EffectSystem/EffectAsset.h>
#include <EffectSystem/EffectPrimitiveScene.h>
#include <GameCore/Components.h>
#include <GpuData/Effect.h>
#include <GpuData/Particle.h>
#include <ParticleSystem/ParticleRangeAllocator.h>
#include <ParticleSystem/ParticleScene.h>

// === C++ includes ===
#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace Cue::ECS {
class EffectEmitterSystem final
    : public ECSManager::System<WorldTransformComponent,
                                EffectEmitterComponent> {
public:
  explicit EffectEmitterSystem(
      AssetManager *a_assetManager, MaterialHandle a_defaultMaterialHandle,
      ParticleSystem::ParticleRangeAllocator &a_particleAllocator,
      ParticleSystem::ParticleScene &a_scene,
      EffectSystem::EffectPrimitiveScene &a_primitiveScene)
      : ECSManager::System<WorldTransformComponent, EffectEmitterComponent>(
            [this](Entity a_entity, WorldTransformComponent &a_transform,
                   EffectEmitterComponent &a_effect,
                   const UpdateContext &a_context) {
              collect_effect(a_entity, a_transform, a_effect, a_context);
            }),
        m_assetManager(a_assetManager),
        m_defaultMaterialHandle(a_defaultMaterialHandle),
        m_particleAllocator(a_particleAllocator), m_scene(a_scene),
        m_primitiveScene(a_primitiveScene) {}

  void update(const UpdateContext &a_context) override {
    m_currentBufferIndex = a_context.bufferIndex;
    ECSManager::System<WorldTransformComponent, EffectEmitterComponent>::update(
        a_context);
    m_currentBufferIndex = 0;
  }

private:
  static constexpr uint32_t k_invalidParticleBase =
      (std::numeric_limits<uint32_t>::max)();
  static constexpr uint32_t k_maxSpawnPerFrame = 64;

  [[nodiscard]] uint32_t
  allocate_particle_range(EffectEmitterRuntimeState &a_runtime,
                          uint32_t a_requestedCapacity) {
    if (a_runtime.particleBase != k_invalidParticleBase &&
        a_runtime.particleCapacity > 0) {
      if (a_runtime.particleCapacity < a_requestedCapacity) {
        m_particleAllocator.release(a_runtime.particleBase,
                                    a_runtime.particleCapacity);
        a_runtime = {};
        a_runtime.particleBase = k_invalidParticleBase;
      } else {
        return a_runtime.particleCapacity;
      }
    }

    uint32_t particleBase = k_invalidParticleBase;
    uint32_t capacity = 0;
    if (!m_particleAllocator.allocate(a_requestedCapacity, particleBase,
                                      capacity)) {
      return 0;
    }

    a_runtime.particleBase = particleBase;
    a_runtime.particleCapacity = capacity;
    return capacity;
  }

  void release_particle_range(EffectEmitterRuntimeState &a_runtime) {
    if (a_runtime.particleBase == k_invalidParticleBase ||
        a_runtime.particleCapacity == 0) {
      return;
    }

    m_particleAllocator.release(a_runtime.particleBase,
                                a_runtime.particleCapacity);
    a_runtime = {};
    a_runtime.particleBase = k_invalidParticleBase;
  }

  void release_effect_ranges(EffectEmitterComponent &a_effect) {
    for (EffectEmitterRuntimeState &runtime : a_effect.runtimeEmitters) {
      release_particle_range(runtime);
    }
  }

  [[nodiscard]] uint32_t
  calculate_spawn_count(const EffectSystem::EffectEmitterDesc &a_emitter,
                        EffectEmitterRuntimeState &a_runtime, bool a_isPlaying,
                        float a_deltaTime) {
    if (!a_isPlaying || a_deltaTime <= 0.0f) {
      return 0;
    }

    a_runtime.timeSeconds += a_deltaTime;
    float localTime = a_runtime.timeSeconds - a_emitter.startDelay;
    if (localTime < 0.0f) {
      return 0;
    }
    if (a_emitter.duration > 0.0f && localTime > a_emitter.duration) {
      if (!a_emitter.isLooping) {
        return 0;
      }

      localTime = std::fmod(localTime, a_emitter.duration);
      a_runtime.timeSeconds = a_emitter.startDelay + localTime;
      a_runtime.hasSpawnedBurst = false;
    }

    uint32_t spawnCount = 0;
    if (!a_runtime.hasSpawnedBurst) {
      spawnCount += a_emitter.burstCount;
      a_runtime.hasSpawnedBurst = true;
    }

    if (a_emitter.emitRate > 0.0f) {
      a_runtime.emitAccumulator += a_emitter.emitRate * a_deltaTime;
      const uint32_t continuousCount =
          static_cast<uint32_t>(a_runtime.emitAccumulator);
      if (continuousCount > 0) {
        spawnCount += continuousCount;
        a_runtime.emitAccumulator -= static_cast<float>(continuousCount);
      }
    }

    return (std::min)(spawnCount, k_maxSpawnPerFrame);
  }

  [[nodiscard]] static Math::float4
  make_shape_params(const EffectSystem::EffectEmitterDesc &a_emitter) noexcept {
    if (a_emitter.shape == EffectSystem::EffectEmitterShape::Sphere) {
      return Math::float4(a_emitter.shapeRadius, 0.0f, 0.0f, 0.0f);
    }
    if (a_emitter.shape == EffectSystem::EffectEmitterShape::Box) {
      return Math::float4(a_emitter.shapeBoxExtents.x,
                          a_emitter.shapeBoxExtents.y,
                          a_emitter.shapeBoxExtents.z, 0.0f);
    }
    if (a_emitter.shape == EffectSystem::EffectEmitterShape::Cone) {
      return Math::float4(a_emitter.shapeRadius, a_emitter.shapeAngleDegrees,
                          0.0f, 0.0f);
    }

    return Math::float4(0.0f, 0.0f, 0.0f, 0.0f);
  }

  [[nodiscard]] MaterialHandle
  resolve_material_handle(const EffectEmitterComponent &a_effect,
                          std::string_view a_materialName) const {
    if (a_effect.overrideMaterialHandle.valid()) {
      return a_effect.overrideMaterialHandle;
    }

    MaterialHandle materialHandle{};
    if (!a_materialName.empty() && m_assetManager != nullptr &&
        m_assetManager->get_material(a_materialName, materialHandle)) {
      return materialHandle;
    }

    return m_defaultMaterialHandle;
  }

  [[nodiscard]] MaterialHandle resolve_material_handle(
      const EffectEmitterComponent &a_effect,
      const EffectSystem::EffectEmitterDesc &a_emitter) const {
    return resolve_material_handle(a_effect, a_emitter.materialName);
  }

  [[nodiscard]] static Math::float3
  add_float3(const Math::float3 &a_left, const Math::float3 &a_right) noexcept {
    return Math::float3(a_left.x + a_right.x, a_left.y + a_right.y,
                        a_left.z + a_right.z);
  }

  [[nodiscard]] static Math::float3 scale_float3(const Math::float3 &a_value,
                                                 float a_scale) noexcept {
    return Math::float3(a_value.x * a_scale, a_value.y * a_scale,
                        a_value.z * a_scale);
  }

  [[nodiscard]] static Math::float2 lerp_float2(const Math::float2 &a_start,
                                                const Math::float2 &a_end,
                                                float a_rate) noexcept {
    return Math::float2(a_start.x + (a_end.x - a_start.x) * a_rate,
                        a_start.y + (a_end.y - a_start.y) * a_rate);
  }

  [[nodiscard]] static Math::float4 lerp_float4(const Math::float4 &a_start,
                                                const Math::float4 &a_end,
                                                float a_rate) noexcept {
    return Math::float4(a_start.x + (a_end.x - a_start.x) * a_rate,
                        a_start.y + (a_end.y - a_start.y) * a_rate,
                        a_start.z + (a_end.z - a_start.z) * a_rate,
                        a_start.w + (a_end.w - a_start.w) * a_rate);
  }

  [[nodiscard]] static Math::float2
  evaluate_float2_curve(const Math::float2 &a_start, const Math::float2 &a_mid,
                        const Math::float2 &a_end, float a_rate,
                        float a_midTime) noexcept {
    const float midTime = (std::clamp)(a_midTime, 0.001f, 0.999f);
    if (a_rate <= midTime) {
      return lerp_float2(a_start, a_mid, a_rate / midTime);
    }

    return lerp_float2(a_mid, a_end, (a_rate - midTime) / (1.0f - midTime));
  }

  [[nodiscard]] static Math::float4
  evaluate_float4_curve(const Math::float4 &a_start, const Math::float4 &a_mid,
                        const Math::float4 &a_end, float a_rate,
                        float a_midTime) noexcept {
    const float midTime = (std::clamp)(a_midTime, 0.001f, 0.999f);
    if (a_rate <= midTime) {
      return lerp_float4(a_start, a_mid, a_rate / midTime);
    }

    return lerp_float4(a_mid, a_end, (a_rate - midTime) / (1.0f - midTime));
  }

  [[nodiscard]] static bool
  evaluate_timeline(const EffectSystem::EffectTimelineDesc &a_timeline,
                    float a_effectTime, uint32_t a_instanceIndex,
                    float &a_outLocalTime, float &a_outLifeRate) noexcept {
    const float spawnTime =
        a_timeline.startTime + static_cast<float>(a_instanceIndex) *
                                   (std::max)(a_timeline.spawnInterval, 0.0f);
    float localTime = a_effectTime - spawnTime;
    if (localTime < 0.0f) {
      return false;
    }

    const float duration = (std::max)(a_timeline.duration, 0.0f);
    if (duration > 0.0f && localTime > duration) {
      if (!a_timeline.isLooping) {
        return false;
      }

      localTime = std::fmod(localTime, duration);
    }

    const float life = (std::max)(a_timeline.life, 0.001f);
    if (localTime > life && !a_timeline.isLooping) {
      return false;
    }

    a_outLocalTime = localTime;
    a_outLifeRate = std::fmod(localTime, life) / life;
    return true;
  }

  [[nodiscard]] Math::float4
  material_color(MaterialHandle a_materialHandle) const {
    if (!a_materialHandle.valid() || m_assetManager == nullptr) {
      return Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    MaterialDesc materialDesc{};
    if (!m_assetManager->get_material(a_materialHandle, materialDesc)) {
      return Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    return materialDesc.color;
  }

  void fill_material_texture(MaterialHandle a_materialHandle,
                             uint32_t &a_textureId,
                             uint32_t &a_useTexture) const {
    a_textureId = 0;
    a_useTexture = 0;
    if (!a_materialHandle.valid() || m_assetManager == nullptr) {
      return;
    }

    MaterialDesc materialDesc{};
    if (!m_assetManager->get_material(a_materialHandle, materialDesc)) {
      return;
    }

    a_textureId = materialDesc.textureId;
    a_useTexture = materialDesc.isTextureUsed ? 1u : 0u;
  }

  void collect_emitter(const WorldTransformComponent &a_transform,
                       EffectEmitterComponent &a_effect,
                       const EffectSystem::EffectEmitterDesc &a_emitter,
                       EffectEmitterRuntimeState &a_runtime,
                       uint32_t a_emitterIndex,
                       const UpdateContext &a_context) {
    if (!a_emitter.isVisible) {
      return;
    }

    const uint32_t capacity =
        allocate_particle_range(a_runtime, a_emitter.maxParticleCount);
    if (capacity == 0) {
      return;
    }

    Math::float4 materialColor(1.0f, 1.0f, 1.0f, 1.0f);
    uint32_t textureId = 0;
    uint32_t useTexture = 0;
    const MaterialHandle materialHandle =
        resolve_material_handle(a_effect, a_emitter);
    if (materialHandle.valid() && m_assetManager != nullptr) {
      MaterialDesc materialDesc{};
      if (m_assetManager->get_material(materialHandle, materialDesc)) {
        materialColor = materialDesc.color;
        textureId = materialDesc.textureId;
        useTexture = materialDesc.isTextureUsed ? 1u : 0u;
      }
    }

    const uint32_t spawnCount = calculate_spawn_count(
        a_emitter, a_runtime, a_effect.isPlaying, a_context.deltaTime);

    ParticleSystem::ParticleEmitterItem item{};
    item.emitter.position =
        Math::float3(a_transform.position.x + a_emitter.positionOffset.x,
                     a_transform.position.y + a_emitter.positionOffset.y,
                     a_transform.position.z + a_emitter.positionOffset.z);
    item.emitter.particleBase = a_runtime.particleBase;
    item.emitter.velocityMin = a_emitter.velocityMin;
    item.emitter.particleCapacity = capacity;
    item.emitter.velocityMax = a_emitter.velocityMax;
    item.emitter.spawnCount = spawnCount;
    item.emitter.acceleration = a_emitter.acceleration;
    item.emitter.spawnCursor = a_runtime.spawnCursor;
    item.emitter.startColor =
        Math::float4(a_emitter.startColor.r * materialColor.r,
                     a_emitter.startColor.g * materialColor.g,
                     a_emitter.startColor.b * materialColor.b,
                     a_emitter.startColor.a * materialColor.a);
    item.emitter.midColor =
        Math::float4(a_emitter.midColor.r * materialColor.r,
                     a_emitter.midColor.g * materialColor.g,
                     a_emitter.midColor.b * materialColor.b,
                     a_emitter.midColor.a * materialColor.a);
    item.emitter.endColor =
        Math::float4(a_emitter.endColor.r * materialColor.r,
                     a_emitter.endColor.g * materialColor.g,
                     a_emitter.endColor.b * materialColor.b,
                     a_emitter.endColor.a * materialColor.a);
    item.emitter.sizeLifetime =
        Math::float4(a_emitter.startSize, a_emitter.endSize,
                     a_emitter.minLifetime, a_emitter.maxLifetime);
    item.emitter.curveParams =
        Math::float4(a_emitter.midSize, a_emitter.curveMidTime, 0.0f, 0.0f);
    item.emitter.shapeParams = make_shape_params(a_emitter);
    item.emitter.trailParams = Math::float4(
        a_emitter.trailWidth, a_emitter.trailLength,
        static_cast<float>(a_emitter.trailSegmentCount), a_emitter.meshScale);
    item.emitter.forceParams =
        Math::float4(a_emitter.drag, a_emitter.noiseStrength,
                     a_emitter.noiseFrequency, a_emitter.vortexStrength);
    item.emitter.attractorParams = Math::float4(
        a_emitter.attractorPosition.x, a_emitter.attractorPosition.y,
        a_emitter.attractorPosition.z, a_emitter.attractorStrength);
    item.emitter.linearForce =
        Math::float4(a_emitter.linearForce.x, a_emitter.linearForce.y,
                     a_emitter.linearForce.z, 0.0f);
    item.emitter.textureId = textureId;
    item.emitter.useTexture = useTexture;
    item.emitter.rendererType = static_cast<uint32_t>(a_emitter.rendererType);
    item.emitter.shapeType = static_cast<uint32_t>(a_emitter.shape);
    item.emitter.randomSeed =
        a_emitter.randomSeed ^ a_effect.randomSeed ^ a_emitterIndex;
    item.emitter.billboardMode = static_cast<uint32_t>(a_emitter.billboardMode);

    if (m_scene.submit_emitter(m_currentBufferIndex, item) && spawnCount > 0) {
      a_runtime.spawnCursor = (a_runtime.spawnCursor + spawnCount) % capacity;
    }
  }

  void collect_sprite(const WorldTransformComponent &a_transform,
                      const EffectEmitterComponent &a_effect,
                      const EffectSystem::EffectSpritePrimitiveDesc &a_sprite,
                      float a_effectTime, uint32_t a_primitiveIndex) {
    if (!a_sprite.isVisible) {
      return;
    }

    const uint32_t spawnCount = (std::max)(a_sprite.timeline.spawnCount, 1u);
    for (uint32_t instanceIndex = 0; instanceIndex < spawnCount;
         ++instanceIndex) {
      float localTime = 0.0f;
      float lifeRate = 0.0f;
      if (!evaluate_timeline(a_sprite.timeline, a_effectTime, instanceIndex,
                             localTime, lifeRate)) {
        continue;
      }

      const MaterialHandle materialHandle =
          resolve_material_handle(a_effect, a_sprite.materialName);
      const Math::float4 tint = material_color(materialHandle);
      const Math::float4 color = evaluate_float4_curve(
          a_sprite.startColor, a_sprite.midColor, a_sprite.endColor, lifeRate,
          a_sprite.curveMidTime);

      EffectSystem::EffectSpriteItem item{};
      item.sprite.position =
          add_float3(add_float3(a_transform.position, a_sprite.positionOffset),
                     scale_float3(a_sprite.velocity, localTime));
      item.sprite.rotation =
          a_sprite.rotation + a_sprite.angularVelocity * localTime;
      item.sprite.size = evaluate_float2_curve(
          a_sprite.startSize, a_sprite.midSize, a_sprite.endSize, lifeRate,
          a_sprite.curveMidTime);
      item.sprite.uvMin = a_sprite.uvMin;
      item.sprite.uvMax = a_sprite.uvMax;
      item.sprite.blendMode = static_cast<uint32_t>(a_sprite.blendMode);
      item.sprite.color = Math::float4(color.x * tint.x, color.y * tint.y,
                                       color.z * tint.z, color.w * tint.w);
      fill_material_texture(materialHandle, item.sprite.textureId,
                            item.sprite.useTexture);
      item.sprite.padding0 =
          a_sprite.randomSeed ^ a_effect.randomSeed ^ a_primitiveIndex;
      (void)m_primitiveScene.submit_sprite(m_currentBufferIndex, item);
    }
  }

  void collect_ribbon(const WorldTransformComponent &a_transform,
                      const EffectEmitterComponent &a_effect,
                      const EffectSystem::EffectRibbonPrimitiveDesc &a_ribbon,
                      float a_effectTime, uint32_t a_primitiveIndex) {
    if (!a_ribbon.isVisible) {
      return;
    }

    const uint32_t spawnCount = (std::max)(a_ribbon.timeline.spawnCount, 1u);
    for (uint32_t instanceIndex = 0; instanceIndex < spawnCount;
         ++instanceIndex) {
      float localTime = 0.0f;
      float lifeRate = 0.0f;
      if (!evaluate_timeline(a_ribbon.timeline, a_effectTime, instanceIndex,
                             localTime, lifeRate)) {
        continue;
      }

      const MaterialHandle materialHandle =
          resolve_material_handle(a_effect, a_ribbon.materialName);
      const Math::float4 tint = material_color(materialHandle);
      const Math::float4 startColor = evaluate_float4_curve(
          a_ribbon.startColor, a_ribbon.midColor, a_ribbon.endColor, lifeRate,
          a_ribbon.curveMidTime);

      EffectSystem::EffectRibbonItem item{};
      item.ribbon.startPosition =
          add_float3(a_transform.position, a_ribbon.startOffset);
      item.ribbon.endPosition =
          add_float3(a_transform.position, a_ribbon.endOffset);
      item.ribbon.startWidth = (std::max)(a_ribbon.width, 0.001f);
      item.ribbon.endWidth = (std::max)(a_ribbon.endWidth, 0.001f);
      item.ribbon.startColor =
          Math::float4(startColor.x * tint.x, startColor.y * tint.y,
                       startColor.z * tint.z, startColor.w * tint.w);
      item.ribbon.endColor = Math::float4(
          a_ribbon.endColor.x * tint.x, a_ribbon.endColor.y * tint.y,
          a_ribbon.endColor.z * tint.z, a_ribbon.endColor.w * tint.w);
      item.ribbon.uvScaleOffset = a_ribbon.uvScaleOffset;
      item.ribbon.segmentCount =
          (std::clamp)(a_ribbon.segmentCount, 1u,
                       GpuData::k_maxEffectRibbonSegmentCount);
      item.ribbon.blendMode = static_cast<uint32_t>(a_ribbon.blendMode);
      item.ribbon.randomSeed =
          a_ribbon.randomSeed ^ a_effect.randomSeed ^ a_primitiveIndex;
      item.ribbon.jitter = (std::max)(a_ribbon.jitter, 0.0f);
      fill_material_texture(materialHandle, item.ribbon.textureId,
                            item.ribbon.useTexture);
      (void)m_primitiveScene.submit_ribbon(m_currentBufferIndex, item);
    }
  }

  void collect_effect(Entity a_entity, WorldTransformComponent &a_transform,
                      EffectEmitterComponent &a_effect,
                      const UpdateContext &a_context) {
    a_entity;
    if (!a_effect.isVisible || m_assetManager == nullptr) {
      release_effect_ranges(a_effect);
      return;
    }

    EffectHandle effectHandle = a_effect.effectHandle;
    if (!effectHandle.valid() && !a_effect.effectName.empty()) {
      (void)m_assetManager->get_effect(a_effect.effectName, effectHandle);
      a_effect.effectHandle = effectHandle;
    }
    if (!effectHandle.valid()) {
      release_effect_ranges(a_effect);
      return;
    }

    const EffectSystem::EffectAsset *asset = nullptr;
    if (!m_assetManager->get_effect(effectHandle, asset) || asset == nullptr) {
      release_effect_ranges(a_effect);
      return;
    }

    if (a_effect.runtimeEmitters.size() > asset->emitters.size()) {
      for (size_t emitterIndex = asset->emitters.size();
           emitterIndex < a_effect.runtimeEmitters.size(); ++emitterIndex) {
        release_particle_range(a_effect.runtimeEmitters[emitterIndex]);
      }
      a_effect.runtimeEmitters.resize(asset->emitters.size());
    }
    if (a_effect.runtimeEmitters.size() < asset->emitters.size()) {
      a_effect.runtimeEmitters.resize(asset->emitters.size());
    }

    const float playbackSpeed = (std::max)(a_effect.playbackSpeed, 0.0f);
    UpdateContext effectContext = a_context;
    effectContext.deltaTime *= playbackSpeed;
    if (a_effect.isPlaying) {
      a_effect.primitiveTimeSeconds += effectContext.deltaTime;
    }

    for (uint32_t emitterIndex = 0;
         emitterIndex < static_cast<uint32_t>(asset->emitters.size());
         ++emitterIndex) {
      collect_emitter(a_transform, a_effect, asset->emitters[emitterIndex],
                      a_effect.runtimeEmitters[emitterIndex], emitterIndex,
                      effectContext);
    }

    for (uint32_t spriteIndex = 0;
         spriteIndex < static_cast<uint32_t>(asset->sprites.size());
         ++spriteIndex) {
      collect_sprite(a_transform, a_effect, asset->sprites[spriteIndex],
                     a_effect.primitiveTimeSeconds, spriteIndex);
    }

    for (uint32_t ribbonIndex = 0;
         ribbonIndex < static_cast<uint32_t>(asset->ribbons.size());
         ++ribbonIndex) {
      collect_ribbon(a_transform, a_effect, asset->ribbons[ribbonIndex],
                     a_effect.primitiveTimeSeconds, ribbonIndex);
    }
  }

  AssetManager *m_assetManager = nullptr;
  MaterialHandle m_defaultMaterialHandle{};
  ParticleSystem::ParticleRangeAllocator &m_particleAllocator;
  ParticleSystem::ParticleScene &m_scene;
  EffectSystem::EffectPrimitiveScene &m_primitiveScene;
  uint32_t m_currentBufferIndex = 0;
};
} // namespace Cue::ECS
