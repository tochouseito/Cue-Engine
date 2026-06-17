#include <EffectSystem/EffectPrimitiveResources.h>

namespace Cue::EffectSystem {
Result EffectPrimitiveResources::create_frame_buffer() {
  constexpr uint32_t k_constantBufferAlignment = 256;

  RHI::BufferDesc bufferDesc{};
  bufferDesc.name = "EffectPrimitiveFrameBuffer";
  bufferDesc.type = RHI::BufferType::Constant;
  bufferDesc.defaultHeapCount = 1;
  bufferDesc.uploadHeapCount = m_bufferCount;
  bufferDesc.initialState = RHI::ResourceState::ShaderResource;
  bufferDesc.stride = sizeof(GpuData::EffectFrameGpu);
  bufferDesc.elementCount = 1;
  bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
  bufferDesc.alignment = k_constantBufferAlignment;

  RHI::BufferHandle &handle = m_bufferHandles[static_cast<size_t>(
      EffectPrimitiveResourceType::FrameBuffer)];
  Result result = m_bufferManager->create_buffer(bufferDesc, handle);
  if (!result) {
    return result;
  }

  result = m_bufferManager->create_slot_uploaders(handle, m_bufferCount,
                                                  m_frameUploaders);
  if (!result) {
    return result;
  }
  if (m_frameUploaders.size() != m_bufferCount) {
    return Result::fail(Code::InternalError, Severity::Fatal,
                        "Effect primitive frame uploader was not created.");
  }

  return Result::ok();
}

Result
EffectPrimitiveResources::create_sprite_buffer(uint32_t a_maxSpriteCount) {
  RHI::BufferDesc bufferDesc{};
  bufferDesc.name = "EffectSpriteBuffer";
  bufferDesc.type = RHI::BufferType::Structured;
  bufferDesc.defaultHeapCount = 1;
  bufferDesc.uploadHeapCount = m_bufferCount;
  bufferDesc.initialState = RHI::ResourceState::ShaderResource;
  bufferDesc.stride = sizeof(GpuData::EffectSpriteGpu);
  bufferDesc.elementCount = a_maxSpriteCount;
  bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
  bufferDesc.alignment = alignof(GpuData::EffectSpriteGpu);

  RHI::BufferHandle &handle = m_bufferHandles[static_cast<size_t>(
      EffectPrimitiveResourceType::SpriteBuffer)];
  Result result = m_bufferManager->create_buffer(bufferDesc, handle);
  if (!result) {
    return result;
  }

  result = m_bufferManager->create_slot_uploaders(handle, m_bufferCount,
                                                  m_spriteUploaders);
  if (!result) {
    return result;
  }
  if (m_spriteUploaders.size() != m_bufferCount) {
    return Result::fail(Code::InternalError, Severity::Fatal,
                        "Effect sprite uploader was not created.");
  }

  return Result::ok();
}

Result
EffectPrimitiveResources::create_ribbon_buffer(uint32_t a_maxRibbonCount) {
  RHI::BufferDesc bufferDesc{};
  bufferDesc.name = "EffectRibbonBuffer";
  bufferDesc.type = RHI::BufferType::Structured;
  bufferDesc.defaultHeapCount = 1;
  bufferDesc.uploadHeapCount = m_bufferCount;
  bufferDesc.initialState = RHI::ResourceState::ShaderResource;
  bufferDesc.stride = sizeof(GpuData::EffectRibbonGpu);
  bufferDesc.elementCount = a_maxRibbonCount;
  bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
  bufferDesc.alignment = alignof(GpuData::EffectRibbonGpu);

  RHI::BufferHandle &handle = m_bufferHandles[static_cast<size_t>(
      EffectPrimitiveResourceType::RibbonBuffer)];
  Result result = m_bufferManager->create_buffer(bufferDesc, handle);
  if (!result) {
    return result;
  }

  result = m_bufferManager->create_slot_uploaders(handle, m_bufferCount,
                                                  m_ribbonUploaders);
  if (!result) {
    return result;
  }
  if (m_ribbonUploaders.size() != m_bufferCount) {
    return Result::fail(Code::InternalError, Severity::Fatal,
                        "Effect ribbon uploader was not created.");
  }

  return Result::ok();
}
} // namespace Cue::EffectSystem
