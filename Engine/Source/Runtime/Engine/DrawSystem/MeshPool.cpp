#include "MeshPool.h"

// === C++ includes ===
#include <algorithm>
#include <cstring>

namespace Cue::DrawSystem {
static_assert(sizeof(MeshRange) == 32u,
              "MeshRange must match the HLSL structured buffer layout.");
static_assert(sizeof(MeshChunkRange) == 16u,
              "MeshChunkRange must match the HLSL structured buffer layout.");
static_assert(sizeof(MeshletChunk) == 64u,
              "MeshletChunk must match the HLSL structured buffer layout.");
static_assert(
    sizeof(VisibilityTriangle) == 128u,
    "VisibilityTriangle must match the HLSL structured buffer layout.");
static_assert(sizeof(Core::Native::MeshletBounds) == 64u,
              "MeshletBounds must match the HLSL structured buffer layout.");

namespace {
[[nodiscard]] bool allocate_from_free_ranges(std::vector<FreeRange> &freeRanges,
                                             uint64_t byteSize,
                                             uint32_t alignment,
                                             uint64_t &outOffset) noexcept {
  // - free-list を先頭から走査し、alignment を満たす最初の範囲を探す
  outOffset = 0;
  for (size_t i = 0; i < freeRanges.size(); ++i) {
    const FreeRange freeRange = freeRanges[i];
    const uint64_t alignedOffset =
        alignment <= 1
            ? freeRange.byteOffset
            : Math::round_up_to_multiple(freeRange.byteOffset,
                                         static_cast<uint64_t>(alignment));
    const uint64_t prefixBytes = alignedOffset - freeRange.byteOffset;
    if (prefixBytes + byteSize > freeRange.byteSize) {
      continue;
    }

    const uint64_t suffixOffset = alignedOffset + byteSize;
    const uint64_t suffixBytes = freeRange.byteSize - prefixBytes - byteSize;

    // - 確保範囲の前後に残る空き領域だけを free-list に残す
    if (prefixBytes > 0 && suffixBytes > 0) {
      freeRanges[i].byteSize = prefixBytes;
      freeRanges.insert(freeRanges.begin() + static_cast<std::ptrdiff_t>(i + 1),
                        FreeRange{suffixOffset, suffixBytes});
    } else if (prefixBytes > 0) {
      freeRanges[i].byteSize = prefixBytes;
    } else if (suffixBytes > 0) {
      freeRanges[i].byteOffset = suffixOffset;
      freeRanges[i].byteSize = suffixBytes;
    } else {
      freeRanges.erase(freeRanges.begin() + static_cast<std::ptrdiff_t>(i));
    }

    outOffset = alignedOffset;
    return true;
  }

  return false;
}

void release_to_free_ranges(std::vector<FreeRange> &freeRanges,
                            uint64_t byteOffset, uint64_t byteSize) noexcept {
  // - 0 byte の解放は free-list を変更しない
  if (byteSize == 0) {
    return;
  }

  // - byteOffset の昇順を保つ位置に解放範囲を挿入する
  auto insertIt = freeRanges.begin();
  while (insertIt != freeRanges.end() && insertIt->byteOffset < byteOffset) {
    ++insertIt;
  }
  insertIt = freeRanges.insert(insertIt, FreeRange{byteOffset, byteSize});

  if (insertIt != freeRanges.begin()) {
    auto prevIt = insertIt;
    --prevIt;
    if (prevIt->byteOffset + prevIt->byteSize == insertIt->byteOffset) {
      prevIt->byteSize += insertIt->byteSize;
      insertIt = freeRanges.erase(insertIt);
      insertIt = prevIt;
    }
  }

  // - 直後の空き範囲と隣接していれば結合する
  auto nextIt = insertIt;
  ++nextIt;
  if (nextIt != freeRanges.end() &&
      insertIt->byteOffset + insertIt->byteSize == nextIt->byteOffset) {
    insertIt->byteSize += nextIt->byteSize;
    freeRanges.erase(nextIt);
  }
}

template <typename T>
[[nodiscard]] uint64_t byte_size_of(const std::vector<T> &values) noexcept {
  // - vector の要素数を byte 数に変換する
  return static_cast<uint64_t>(values.size()) *
         static_cast<uint64_t>(sizeof(T));
}

[[nodiscard]] std::vector<VisibilityTriangle>
build_visibility_triangles(const Core::Native::MeshData &meshData) {
  std::vector<VisibilityTriangle> triangles;
  triangles.reserve(meshData.indices.size() / 3u);

  for (size_t indexOffset = 0; indexOffset + 2 < meshData.indices.size();
       indexOffset += 3) {
    const uint32_t i0 = meshData.indices[indexOffset + 0];
    const uint32_t i1 = meshData.indices[indexOffset + 1];
    const uint32_t i2 = meshData.indices[indexOffset + 2];

    VisibilityTriangle triangle{};
    triangle.position0 = meshData.positions[i0];
    triangle.position1 = meshData.positions[i1];
    triangle.position2 = meshData.positions[i2];

    if (!meshData.normals.empty()) {
      const Math::float3 n0 = meshData.normals[i0];
      const Math::float3 n1 = meshData.normals[i1];
      const Math::float3 n2 = meshData.normals[i2];
      triangle.normal0 = Math::float4(n0.x, n0.y, n0.z, 0.0f);
      triangle.normal1 = Math::float4(n1.x, n1.y, n1.z, 0.0f);
      triangle.normal2 = Math::float4(n2.x, n2.y, n2.z, 0.0f);
    }

    if (!meshData.uvs.empty()) {
      const Math::float2 uv0 = meshData.uvs[i0];
      const Math::float2 uv1 = meshData.uvs[i1];
      const Math::float2 uv2 = meshData.uvs[i2];
      triangle.uv01 = Math::float4(uv0.x, uv0.y, uv1.x, uv1.y);
      triangle.uv2 = Math::float4(uv2.x, uv2.y, 0.0f, 0.0f);
    }

    triangles.push_back(triangle);
  }

  return triangles;
}

[[nodiscard]] MeshBounds
calculate_bounds(const std::vector<Math::float4> &positions) noexcept {
  MeshBounds bounds{};
  if (positions.empty()) {
    // - 頂点がない場合はゼロ境界を返す
    return bounds;
  }

  // - 全頂点を走査してローカル空間の AABB を求める
  Math::float3 minPosition(positions[0].x, positions[0].y, positions[0].z);
  Math::float3 maxPosition = minPosition;
  for (const Math::float4 &position : positions) {
    minPosition.x = (std::min)(minPosition.x, position.x);
    minPosition.y = (std::min)(minPosition.y, position.y);
    minPosition.z = (std::min)(minPosition.z, position.z);
    maxPosition.x = (std::max)(maxPosition.x, position.x);
    maxPosition.y = (std::max)(maxPosition.y, position.y);
    maxPosition.z = (std::max)(maxPosition.z, position.z);
  }

  // - AABB 中心を境界球の中心とし、最遠頂点までの距離を半径にする
  bounds.center = (minPosition + maxPosition) * 0.5f;
  float radiusSq = 0.0f;
  for (const Math::float4 &position : positions) {
    const Math::float3 delta(position.x - bounds.center.x,
                             position.y - bounds.center.y,
                             position.z - bounds.center.z);
    radiusSq = (std::max)(radiusSq, delta.dot(delta));
  }
  bounds.radius = std::sqrt(radiusSq);
  return bounds;
}

[[nodiscard]] std::vector<MeshletChunk> build_meshlet_chunks(
    const std::vector<Core::Native::MeshletBounds> &meshletBounds,
    uint32_t meshId, int32_t baseVertex, uint32_t firstMeshletBase,
    uint32_t rangeStartIndex) {
  static constexpr uint32_t kMeshletsPerChunk = 8u;

  std::vector<MeshletChunk> chunks{};
  if (meshletBounds.empty()) {
    return chunks;
  }

  const uint32_t chunkCount =
      (static_cast<uint32_t>(meshletBounds.size()) + kMeshletsPerChunk - 1u) /
      kMeshletsPerChunk;
  chunks.reserve(chunkCount);

  for (uint32_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
    const uint32_t firstLocalMeshlet = chunkIndex * kMeshletsPerChunk;
    const uint32_t localMeshletCount =
        (std::min)(kMeshletsPerChunk,
                   static_cast<uint32_t>(meshletBounds.size()) -
                       firstLocalMeshlet);

    uint32_t firstIndex = UINT32_MAX;
    uint32_t endIndex = 0;
    Math::float3 minBounds(0.0f, 0.0f, 0.0f);
    Math::float3 maxBounds(0.0f, 0.0f, 0.0f);
    bool hasBounds = false;

    for (uint32_t localOffset = 0; localOffset < localMeshletCount;
         ++localOffset) {
      const Core::Native::MeshletBounds &bounds =
          meshletBounds[firstLocalMeshlet + localOffset];
      firstIndex = (std::min)(firstIndex, bounds.firstIndex);
      endIndex = (std::max)(endIndex, bounds.firstIndex + bounds.indexCount);

      const Math::float3 radius(bounds.radius, bounds.radius, bounds.radius);
      const Math::float3 meshletMin = bounds.center - radius;
      const Math::float3 meshletMax = bounds.center + radius;
      if (!hasBounds) {
        minBounds = meshletMin;
        maxBounds = meshletMax;
        hasBounds = true;
      } else {
        minBounds.x = (std::min)(minBounds.x, meshletMin.x);
        minBounds.y = (std::min)(minBounds.y, meshletMin.y);
        minBounds.z = (std::min)(minBounds.z, meshletMin.z);
        maxBounds.x = (std::max)(maxBounds.x, meshletMax.x);
        maxBounds.y = (std::max)(maxBounds.y, meshletMax.y);
        maxBounds.z = (std::max)(maxBounds.z, meshletMax.z);
      }
    }

    MeshletChunk chunk{};
    chunk.startIndex = rangeStartIndex + firstIndex;
    chunk.indexCount = firstIndex < endIndex ? endIndex - firstIndex : 0u;
    chunk.baseVertex = baseVertex;
    chunk.firstMeshlet = firstMeshletBase + firstLocalMeshlet;
    chunk.meshletCount = localMeshletCount;
    chunk.meshId = meshId;
    chunk.materialId = 0;
    chunk.lod = 0;
    chunk.boundsCenter = (minBounds + maxBounds) * 0.5f;

    float radiusSq = 0.0f;
    for (uint32_t localOffset = 0; localOffset < localMeshletCount;
         ++localOffset) {
      const Core::Native::MeshletBounds &bounds =
          meshletBounds[firstLocalMeshlet + localOffset];
      const Math::float3 delta = bounds.center - chunk.boundsCenter;
      const float distance = std::sqrt(delta.dot(delta)) + bounds.radius;
      radiusSq = (std::max)(radiusSq, distance * distance);
    }
    chunk.boundsRadius = std::sqrt(radiusSq);
    chunks.push_back(chunk);
  }

  return chunks;
}
} // namespace

MeshPool::MeshPool(const MeshPoolDesc &desc, RHI::IBufferManager &bufferManager,
                   RHI::IViewManager &viewManager,
                   RHI::ICommandPool &commandPool, RHI::IQueuePool &queuePool)
    : m_bufferManager(bufferManager), m_viewManager(viewManager),
      m_commandPool(commandPool), m_queuePool(queuePool) {
  m_maxMeshletChunkCount = desc.maxMeshletChunkCount;
  // - コンストラクタでは初期化結果だけ保持し、呼び出し側は allocate_mesh
  // で検査できるようにする
  m_initResult = initialize_streams(desc);
  if (m_initResult) {
    m_initResult = initialize_mesh_range_state(desc);
  }
  if (m_initResult) {
    m_initResult = initialize_mesh_chunk_range_state(desc);
  }
}

MeshPool::~MeshPool() {
  // - 生成順の逆順で破棄し、staging/default の両方を BufferManager へ戻す
  if (m_meshRangeState.srvHandle.valid()) {
    m_viewManager.destroy_view(m_meshRangeState.srvHandle);
    m_meshRangeState.srvHandle = {};
  }
  if (m_meshRangeState.stagingBufferHandle.valid()) {
    m_bufferManager.destroy_buffer(m_meshRangeState.stagingBufferHandle);
    m_meshRangeState.stagingBufferHandle = {};
  }
  if (m_meshRangeState.defaultBufferHandle.valid()) {
    m_bufferManager.destroy_buffer(m_meshRangeState.defaultBufferHandle);
    m_meshRangeState.defaultBufferHandle = {};
  }
  m_meshRangeState.debugName.clear();
  m_meshRangeState.stagingRing.clear();
  m_meshRangeState.mappedStagingData = nullptr;
  m_meshRangeState.stagingCapacityInBytes = 0;
  m_meshRangeState.capacity = 0;
  m_meshRangeState.freeMeshIds.clear();
  if (m_meshChunkRangeState.srvHandle.valid()) {
    m_viewManager.destroy_view(m_meshChunkRangeState.srvHandle);
    m_meshChunkRangeState.srvHandle = {};
  }
  if (m_meshChunkRangeState.stagingBufferHandle.valid()) {
    m_bufferManager.destroy_buffer(m_meshChunkRangeState.stagingBufferHandle);
    m_meshChunkRangeState.stagingBufferHandle = {};
  }
  if (m_meshChunkRangeState.defaultBufferHandle.valid()) {
    m_bufferManager.destroy_buffer(m_meshChunkRangeState.defaultBufferHandle);
    m_meshChunkRangeState.defaultBufferHandle = {};
  }
  m_meshChunkRangeState.debugName.clear();
  m_meshChunkRangeState.stagingRing.clear();
  m_meshChunkRangeState.mappedStagingData = nullptr;
  m_meshChunkRangeState.stagingCapacityInBytes = 0;
  m_meshChunkRangeState.capacity = 0;
  if (m_meshletBoundsSrvHandle.valid()) {
    m_viewManager.destroy_view(m_meshletBoundsSrvHandle);
    m_meshletBoundsSrvHandle = {};
  }
  if (m_meshletChunkSrvHandle.valid()) {
    m_viewManager.destroy_view(m_meshletChunkSrvHandle);
    m_meshletChunkSrvHandle = {};
  }
  destroy_stream_state(m_indexStream);
  destroy_stream_state(m_visibilityTriangleStream);
  destroy_stream_state(m_rangeIndexStream);
  destroy_stream_state(m_meshletBoundsStream);
  destroy_stream_state(m_meshletChunkStream);
  destroy_stream_state(m_influenceStream);
  destroy_stream_state(m_normalStream);
  destroy_stream_state(m_uvStream);
  destroy_stream_state(m_positionStream);
}

Result MeshPool::initialize_streams(const MeshPoolDesc &desc) {
  // - 各ストリームごとの総容量を確定し、永続 default と小さい常設 staging
  // を作る
  Result result = create_stream_state(
      desc.positionName, BufferType::Vertex,
      static_cast<uint64_t>(desc.maxVertexCount) * sizeof(Math::float4),
      desc.positionStagingSize, sizeof(Math::float4), desc.maxVertexCount,
      alignof(Math::float4), m_positionStream);
  if (!result) {
    return result;
  }

  result = create_stream_state(
      desc.uvName, BufferType::Vertex,
      static_cast<uint64_t>(desc.maxVertexCount) * sizeof(Math::float2),
      desc.uvStagingSize, sizeof(Math::float2), desc.maxVertexCount,
      alignof(Math::float2), m_uvStream);
  if (!result) {
    return result;
  }

  result = create_stream_state(
      desc.normalName, BufferType::Vertex,
      static_cast<uint64_t>(desc.maxVertexCount) * sizeof(Math::float3),
      desc.normalStagingSize, sizeof(Math::float3), desc.maxVertexCount,
      alignof(Math::float3), m_normalStream);
  if (!result) {
    return result;
  }

  result = create_stream_state(
      desc.indexName, BufferType::Index,
      static_cast<uint64_t>(desc.maxIndexCount) * sizeof(uint32_t),
      desc.indexStagingSize, sizeof(uint32_t), desc.maxIndexCount,
      alignof(uint32_t), m_indexStream);
  if (!result) {
    return result;
  }

  const uint32_t maxVisibilityTriangleCount = (desc.maxIndexCount + 2u) / 3u;
  result = create_stream_state(
      desc.visibilityTriangleName, BufferType::Structured,
      static_cast<uint64_t>(maxVisibilityTriangleCount) *
          sizeof(VisibilityTriangle),
      desc.visibilityTriangleStagingSize, sizeof(VisibilityTriangle),
      maxVisibilityTriangleCount, alignof(VisibilityTriangle),
      m_visibilityTriangleStream);
  if (!result) {
    return result;
  }

  result = create_stream_state(
      desc.rangeIndexName, BufferType::Index,
      static_cast<uint64_t>(desc.maxRangeIndexCount) * sizeof(uint32_t),
      desc.rangeIndexStagingSize, sizeof(uint32_t), desc.maxRangeIndexCount,
      alignof(uint32_t), m_rangeIndexStream);
  if (!result) {
    return result;
  }

  result = create_stream_state(
      desc.meshletBoundsName, BufferType::Structured,
      static_cast<uint64_t>(desc.maxMeshletCount) *
          sizeof(Core::Native::MeshletBounds),
      desc.meshletBoundsStagingSize, sizeof(Core::Native::MeshletBounds),
      desc.maxMeshletCount, alignof(Core::Native::MeshletBounds),
      m_meshletBoundsStream);
  if (!result) {
    return result;
  }

  result = create_stream_state(
      desc.meshletChunkName, BufferType::Structured,
      static_cast<uint64_t>(desc.maxMeshletChunkCount) * sizeof(MeshletChunk),
      desc.meshletChunkStagingSize, sizeof(MeshletChunk),
      desc.maxMeshletChunkCount, alignof(MeshletChunk), m_meshletChunkStream);
  if (!result) {
    return result;
  }

  ViewDesc meshletBoundsSrvDesc{};
  meshletBoundsSrvDesc.name = desc.meshletBoundsSrvName;
  meshletBoundsSrvDesc.type = ViewType::ShaderResourceBuffer;
  meshletBoundsSrvDesc.bufferKind = BufferKind::Buffer;
  meshletBoundsSrvDesc.bufferHandle = m_meshletBoundsStream.defaultBufferHandle;
  meshletBoundsSrvDesc.firstElement = 0;
  meshletBoundsSrvDesc.numElements = desc.maxMeshletCount;
  meshletBoundsSrvDesc.structureByteStride =
      sizeof(Core::Native::MeshletBounds);
  result =
      m_viewManager.create_view(meshletBoundsSrvDesc, m_meshletBoundsSrvHandle);
  if (!result) {
    return result;
  }

  ViewDesc meshletChunkSrvDesc{};
  meshletChunkSrvDesc.name = desc.meshletChunkSrvName;
  meshletChunkSrvDesc.type = ViewType::ShaderResourceBuffer;
  meshletChunkSrvDesc.bufferKind = BufferKind::Buffer;
  meshletChunkSrvDesc.bufferHandle = m_meshletChunkStream.defaultBufferHandle;
  meshletChunkSrvDesc.firstElement = 0;
  meshletChunkSrvDesc.numElements = desc.maxMeshletChunkCount;
  meshletChunkSrvDesc.structureByteStride = sizeof(MeshletChunk);
  result =
      m_viewManager.create_view(meshletChunkSrvDesc, m_meshletChunkSrvHandle);
  if (!result) {
    return result;
  }

  return Result::ok();
}

Result MeshPool::initialize_mesh_range_state(const MeshPoolDesc &desc) {
  // - MeshRange は meshId を添字にしてシェーダから読む structured buffer
  // として確保する
  m_meshRangeState.debugName = std::string(desc.meshRangeName);

  BufferDesc defaultDesc{};
  defaultDesc.name = desc.meshRangeName;
  defaultDesc.type = BufferType::Structured;
  defaultDesc.defaultHeapCount = 1;
  defaultDesc.uploadHeapCount = 0;
  defaultDesc.initialState = ResourceState::Common;
  defaultDesc.stride = sizeof(MeshRange);
  defaultDesc.elementCount = desc.maxMeshCount;
  defaultDesc.size = defaultDesc.stride * defaultDesc.elementCount;
  defaultDesc.alignment = alignof(MeshRange);

  Result result = m_bufferManager.create_buffer(
      defaultDesc, m_meshRangeState.defaultBufferHandle);
  if (!result) {
    return result;
  }

  // - 通常更新用の staging は最大容量以下に抑え、大きな初期化は一時 upload
  // に退避できるようにする
  const uint64_t totalBytes = static_cast<uint64_t>(defaultDesc.size);
  const uint64_t stagingBytes =
      (std::min)(totalBytes, static_cast<uint64_t>(desc.meshRangeStagingCount) *
                                 sizeof(MeshRange));
  result = create_upload_buffer(m_meshRangeState.debugName + ".Staging",
                                BufferType::Structured, stagingBytes,
                                m_meshRangeState.stagingBufferHandle,
                                m_meshRangeState.mappedStagingData);
  if (!result) {
    m_bufferManager.destroy_buffer(m_meshRangeState.defaultBufferHandle);
    m_meshRangeState.defaultBufferHandle = {};
    return result;
  }

  // - MeshRange buffer 全体をシェーダリソースとして公開する
  ViewDesc meshRangeSrvDesc{};
  meshRangeSrvDesc.name = desc.meshRangeSrvName;
  meshRangeSrvDesc.type = ViewType::ShaderResourceBuffer;
  meshRangeSrvDesc.bufferKind = BufferKind::Buffer;
  meshRangeSrvDesc.bufferHandle = m_meshRangeState.defaultBufferHandle;
  meshRangeSrvDesc.firstElement = 0;
  meshRangeSrvDesc.numElements = desc.maxMeshCount;
  meshRangeSrvDesc.structureByteStride = sizeof(MeshRange);
  result =
      m_viewManager.create_view(meshRangeSrvDesc, m_meshRangeState.srvHandle);
  if (!result) {
    return result;
  }

  // - meshId は後ろから詰めておき、pop_back で O(1) 払い出しできるようにする
  m_meshRangeState.stagingCapacityInBytes = stagingBytes;
  m_meshRangeState.stagingRing.initialize(static_cast<size_t>(stagingBytes));
  m_meshRangeState.capacity = desc.maxMeshCount;
  m_meshRangeState.freeMeshIds.clear();
  m_meshRangeState.freeMeshIds.reserve(desc.maxMeshCount);
  for (uint32_t meshId = desc.maxMeshCount; meshId > 0; --meshId) {
    m_meshRangeState.freeMeshIds.push_back(meshId - 1);
  }

  // - 未登録 meshId が参照されても描画されないよう、MeshRange buffer
  // をゼロ初期化する
  UploadAllocation initializeUpload{};
  result = allocate_upload_range(m_meshRangeState, totalBytes,
                                 alignof(MeshRange), initializeUpload);
  if (!result) {
    return result;
  }

  std::memset(initializeUpload.mappedData + initializeUpload.byteOffset, 0,
              static_cast<size_t>(totalBytes));

  // - 初期化データを GPU 側 MeshRange buffer へ転送する
  BufferCopyRegion initializeRegion{};
  initializeRegion.srcBufferHandle = initializeUpload.bufferHandle;
  initializeRegion.srcUploadResourceIndex = 0;
  initializeRegion.srcByteOffset = initializeUpload.byteOffset;
  initializeRegion.dstBufferHandle = m_meshRangeState.defaultBufferHandle;
  initializeRegion.dstDefaultResourceIndex = 0;
  initializeRegion.dstByteOffset = 0;
  initializeRegion.byteSize = totalBytes;
  std::vector<BufferCopyRegion> initializeRegions{initializeRegion};
  result = copy_upload_regions(initializeRegions);
  release_upload_range(m_meshRangeState, initializeUpload);
  if (!result) {
    return result;
  }

  return Result::ok();
}

Result MeshPool::initialize_mesh_chunk_range_state(const MeshPoolDesc &desc) {
  m_meshChunkRangeState.debugName = std::string(desc.meshChunkRangeName);

  const uint64_t totalBytes =
      static_cast<uint64_t>(desc.maxMeshCount) * sizeof(MeshChunkRange);
  const uint64_t stagingBytes =
      static_cast<uint64_t>(desc.meshChunkRangeStagingCount) *
      sizeof(MeshChunkRange);

  BufferDesc defaultDesc{};
  defaultDesc.name = desc.meshChunkRangeName;
  defaultDesc.type = BufferType::Structured;
  defaultDesc.defaultHeapCount = 1;
  defaultDesc.uploadHeapCount = 0;
  defaultDesc.initialState = ResourceState::Common;
  defaultDesc.stride = sizeof(MeshChunkRange);
  defaultDesc.elementCount = desc.maxMeshCount;
  defaultDesc.size = static_cast<uint32_t>(totalBytes);
  defaultDesc.alignment = alignof(MeshChunkRange);
  Result result = m_bufferManager.create_buffer(
      defaultDesc, m_meshChunkRangeState.defaultBufferHandle);
  if (!result) {
    return result;
  }

  result = create_upload_buffer(
      std::string(desc.meshChunkRangeName) + ".Staging", BufferType::Structured,
      (std::min)(totalBytes, stagingBytes),
      m_meshChunkRangeState.stagingBufferHandle,
      m_meshChunkRangeState.mappedStagingData);
  if (!result) {
    return result;
  }

  ViewDesc srvDesc{};
  srvDesc.name = desc.meshChunkRangeSrvName;
  srvDesc.type = ViewType::ShaderResourceBuffer;
  srvDesc.bufferKind = BufferKind::Buffer;
  srvDesc.bufferHandle = m_meshChunkRangeState.defaultBufferHandle;
  srvDesc.firstElement = 0;
  srvDesc.numElements = desc.maxMeshCount;
  srvDesc.structureByteStride = sizeof(MeshChunkRange);
  result = m_viewManager.create_view(srvDesc, m_meshChunkRangeState.srvHandle);
  if (!result) {
    return result;
  }

  m_meshChunkRangeState.stagingCapacityInBytes =
      (std::min)(totalBytes, stagingBytes);
  m_meshChunkRangeState.stagingRing.initialize(
      static_cast<size_t>(m_meshChunkRangeState.stagingCapacityInBytes));
  m_meshChunkRangeState.capacity = desc.maxMeshCount;

  UploadAllocation initializeUpload{};
  result = allocate_upload_range(m_meshChunkRangeState, totalBytes,
                                 alignof(MeshChunkRange), initializeUpload);
  if (!result) {
    return result;
  }

  std::memset(initializeUpload.mappedData + initializeUpload.byteOffset, 0,
              static_cast<size_t>(totalBytes));

  BufferCopyRegion initializeRegion{};
  initializeRegion.srcBufferHandle = initializeUpload.bufferHandle;
  initializeRegion.srcUploadResourceIndex = 0;
  initializeRegion.srcByteOffset = initializeUpload.byteOffset;
  initializeRegion.dstBufferHandle = m_meshChunkRangeState.defaultBufferHandle;
  initializeRegion.dstDefaultResourceIndex = 0;
  initializeRegion.dstByteOffset = 0;
  initializeRegion.byteSize = totalBytes;
  std::vector<BufferCopyRegion> initializeRegions{initializeRegion};
  result = copy_upload_regions(initializeRegions);
  release_upload_range(m_meshChunkRangeState, initializeUpload);
  return result;
}

Result MeshPool::create_stream_state(std::string_view bufferName,
                                     BufferType bufferType, uint64_t totalBytes,
                                     uint64_t stagingBytes, uint32_t stride,
                                     uint32_t elementCount, uint32_t alignment,
                                     StreamState &outStreamState) {
  // - 常駐先は default heap、通常のコピー元は小さい staging upload buffer
  // とする
  // - default heap は描画時の参照先、staging は CPU からの更新経路として使う
  BufferDesc defaultDesc{};
  defaultDesc.name = bufferName;
  defaultDesc.type = bufferType;
  defaultDesc.defaultHeapCount = 1;
  defaultDesc.uploadHeapCount = 0;
  defaultDesc.initialState = ResourceState::Common;
  defaultDesc.stride = stride;
  defaultDesc.elementCount = elementCount;
  defaultDesc.size = static_cast<uint32_t>(totalBytes);
  defaultDesc.alignment = alignment;

  Result result = m_bufferManager.create_buffer(
      defaultDesc, outStreamState.defaultBufferHandle);
  if (!result) {
    return result;
  }

  // - staging は要求サイズを超えない範囲で確保し、足りない upload は一時 buffer
  // に逃がす
  outStreamState.debugName = std::string(bufferName);
  outStreamState.bufferType = bufferType;
  const uint64_t clampedStagingBytes = (std::min)(totalBytes, stagingBytes);
  result = create_upload_buffer(
      outStreamState.debugName + ".Staging", bufferType, clampedStagingBytes,
      outStreamState.stagingBufferHandle, outStreamState.mappedStagingData);
  if (!result) {
    m_bufferManager.destroy_buffer(outStreamState.defaultBufferHandle);
    outStreamState.defaultBufferHandle = {};
    return result;
  }

  // - 作成直後は default heap 全体を空き範囲として登録する
  outStreamState.capacityInBytes = totalBytes;
  outStreamState.stagingCapacityInBytes = clampedStagingBytes;
  outStreamState.alignment = alignment;
  outStreamState.freeRanges.clear();
  outStreamState.freeRanges.push_back(FreeRange{0, totalBytes});
  outStreamState.stagingRing.initialize(
      static_cast<size_t>(clampedStagingBytes));
  return Result::ok();
}

Result MeshPool::create_upload_buffer(std::string_view bufferName,
                                      BufferType bufferType, uint64_t byteSize,
                                      BufferHandle &outBufferHandle,
                                      std::byte *&outMappedData) {
  // - 呼び出し側が失敗時にも安全に扱えるよう、出力を先に無効化する
  outBufferHandle = {};
  outMappedData = nullptr;
  if (byteSize == 0) {
    return Result::ok();
  }
  if (byteSize > UINT32_MAX) {
    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "Upload buffer size exceeds the supported range.");
  }

  // - upload heap の buffer を 1 byte stride の線形領域として確保する
  BufferDesc uploadDesc{};
  uploadDesc.name = std::string(bufferName);
  uploadDesc.type = bufferType;
  uploadDesc.defaultHeapCount = 0;
  uploadDesc.uploadHeapCount = 1;
  uploadDesc.initialState = ResourceState::CopySource;
  uploadDesc.stride = 1;
  uploadDesc.elementCount = static_cast<uint32_t>(byteSize);
  uploadDesc.size = static_cast<uint32_t>(byteSize);
  uploadDesc.alignment = 1;

  Result result = m_bufferManager.create_buffer(uploadDesc, outBufferHandle);
  if (!result) {
    return result;
  }

  // - CPU から直接書き込むため、BufferManager から mapped address を取得する
  UploadBufferView uploadView{};
  result = m_bufferManager.get_upload_buffer_view(outBufferHandle, uploadView);
  if (!result) {
    m_bufferManager.destroy_buffer(outBufferHandle);
    outBufferHandle = {};
    return result;
  }
  if (uploadView.mappedDatas.size() != 1 ||
      uploadView.mappedDatas[0] == nullptr) {
    m_bufferManager.destroy_buffer(outBufferHandle);
    outBufferHandle = {};
    return Result::fail(Code::InternalError, Severity::Error,
                        "Static mesh pool upload buffer is not mapped.");
  }

  // - mapped address を呼び出し側へ返し、以降の memcpy/memset
  // の書き込み先にする
  outMappedData = uploadView.mappedDatas[0];
  return Result::ok();
}

Result MeshPool::allocate_stream_range(StreamState &streamState,
                                       uint64_t byteSize, uint32_t alignment,
                                       uint64_t &outOffset) {
  // - 常駐先は free-list で管理し、任意順の解放後でも再利用できるようにする
  if (!allocate_from_free_ranges(streamState.freeRanges, byteSize, alignment,
                                 outOffset)) {
    return Result::fail(Code::OutOfMemory, Severity::Error,
                        "Static mesh pool default buffer is out of space.");
  }

  return Result::ok();
}

void MeshPool::release_stream_range(StreamState &streamState,
                                    uint64_t byteOffset, uint64_t byteSize) {
  // - 解放済み領域を free-list に戻し、隣接区間は即時マージして断片化を抑える
  release_to_free_ranges(streamState.freeRanges, byteOffset, byteSize);
}

Result MeshPool::allocate_upload_range(StreamState &streamState,
                                       uint64_t byteSize, uint32_t alignment,
                                       UploadAllocation &outAllocation) {
  // - まず常設 staging ring から確保し、毎回 buffer
  // を作らずに済む経路を優先する
  outAllocation = {};
  if (streamState.stagingBufferHandle.valid() &&
      streamState.mappedStagingData != nullptr &&
      streamState.stagingRing.allocate(static_cast<size_t>(byteSize),
                                       static_cast<size_t>(alignment),
                                       outAllocation.ringAllocation)) {
    outAllocation.bufferHandle = streamState.stagingBufferHandle;
    outAllocation.mappedData = streamState.mappedStagingData;
    outAllocation.byteOffset = outAllocation.ringAllocation.offset;
    outAllocation.byteSize = byteSize;
    outAllocation.isTransient = false;
    return Result::ok();
  }

  // - 常設 staging に収まらない場合は、この upload 専用の一時 buffer を作る
  Result result = create_upload_buffer(
      std::string_view{}, streamState.bufferType, byteSize,
      outAllocation.bufferHandle, outAllocation.mappedData);
  if (!result) {
    return result;
  }

  outAllocation.byteOffset = 0;
  outAllocation.byteSize = byteSize;
  outAllocation.isTransient = true;
  return Result::ok();
}

Result MeshPool::allocate_upload_range(MeshRangeState &meshRangeState,
                                       uint64_t byteSize, uint32_t alignment,
                                       UploadAllocation &outAllocation) {
  // - MeshRange 更新も常設 staging ring を優先して使う
  outAllocation = {};
  if (meshRangeState.stagingBufferHandle.valid() &&
      meshRangeState.mappedStagingData != nullptr &&
      meshRangeState.stagingRing.allocate(static_cast<size_t>(byteSize),
                                          static_cast<size_t>(alignment),
                                          outAllocation.ringAllocation)) {
    outAllocation.bufferHandle = meshRangeState.stagingBufferHandle;
    outAllocation.mappedData = meshRangeState.mappedStagingData;
    outAllocation.byteOffset = outAllocation.ringAllocation.offset;
    outAllocation.byteSize = byteSize;
    outAllocation.isTransient = false;
    return Result::ok();
  }

  // - staging が足りない場合は structured buffer 用の一時 upload buffer を作る
  Result result = create_upload_buffer(
      std::string_view{}, BufferType::Structured, byteSize,
      outAllocation.bufferHandle, outAllocation.mappedData);
  if (!result) {
    return result;
  }

  outAllocation.byteOffset = 0;
  outAllocation.byteSize = byteSize;
  outAllocation.isTransient = true;
  return Result::ok();
}

Result MeshPool::allocate_upload_range(MeshChunkRangeState &meshChunkRangeState,
                                       uint64_t byteSize, uint32_t alignment,
                                       UploadAllocation &outAllocation) {
  outAllocation = {};
  if (meshChunkRangeState.stagingBufferHandle.valid() &&
      meshChunkRangeState.mappedStagingData != nullptr &&
      meshChunkRangeState.stagingRing.allocate(static_cast<size_t>(byteSize),
                                               static_cast<size_t>(alignment),
                                               outAllocation.ringAllocation)) {
    outAllocation.bufferHandle = meshChunkRangeState.stagingBufferHandle;
    outAllocation.mappedData = meshChunkRangeState.mappedStagingData;
    outAllocation.byteOffset = outAllocation.ringAllocation.offset;
    outAllocation.byteSize = byteSize;
    outAllocation.isTransient = false;
    return Result::ok();
  }

  Result result = create_upload_buffer(
      std::string_view{}, BufferType::Structured, byteSize,
      outAllocation.bufferHandle, outAllocation.mappedData);
  if (!result) {
    return result;
  }

  outAllocation.byteOffset = 0;
  outAllocation.byteSize = byteSize;
  outAllocation.isTransient = true;
  return Result::ok();
}

void MeshPool::release_upload_range(StreamState &streamState,
                                    UploadAllocation &allocation) {
  // - 無効な割り当ては解放済みとして扱う
  if (!allocation.valid()) {
    return;
  }

  // - 一時 buffer は破棄し、常設 staging は ring allocation だけを戻す
  if (allocation.isTransient) {
    Result result = m_bufferManager.destroy_buffer(allocation.bufferHandle);
    CUE_ASSERT_MSG(result,
                   "Failed to destroy transient static mesh upload buffer.");
  } else if (allocation.ringAllocation.valid()) {
    const bool released =
        streamState.stagingRing.release(allocation.ringAllocation);
    CUE_ASSERT_MSG(released,
                   "Failed to release static mesh staging ring allocation.");
  }

  allocation = {};
}

void MeshPool::release_upload_range(MeshRangeState &meshRangeState,
                                    UploadAllocation &allocation) {
  // - 無効な割り当ては解放済みとして扱う
  if (!allocation.valid()) {
    return;
  }

  // - 一時 buffer は破棄し、常設 staging は ring allocation だけを戻す
  if (allocation.isTransient) {
    Result result = m_bufferManager.destroy_buffer(allocation.bufferHandle);
    CUE_ASSERT_MSG(result,
                   "Failed to destroy transient mesh range upload buffer.");
  } else if (allocation.ringAllocation.valid()) {
    const bool released =
        meshRangeState.stagingRing.release(allocation.ringAllocation);
    CUE_ASSERT_MSG(released,
                   "Failed to release mesh range staging ring allocation.");
  }

  allocation = {};
}

void MeshPool::release_upload_range(MeshChunkRangeState &meshChunkRangeState,
                                    UploadAllocation &allocation) {
  if (!allocation.valid()) {
    allocation = {};
    return;
  }

  if (allocation.isTransient) {
    Result result = m_bufferManager.destroy_buffer(allocation.bufferHandle);
    CUE_ASSERT_MSG(
        result, "Failed to destroy transient mesh chunk range upload buffer.");
  } else if (allocation.ringAllocation.valid()) {
    const bool released =
        meshChunkRangeState.stagingRing.release(allocation.ringAllocation);
    CUE_ASSERT_MSG(
        released,
        "Failed to release mesh chunk range staging ring allocation.");
  }
  allocation = {};
}

void MeshPool::write_upload_bytes(const UploadAllocation &allocation,
                                  const void *sourceData, uint64_t byteSize) {
  // - upload staging へ直接書き込み、データ未指定の属性はゼロで埋める
  std::byte *dst = allocation.mappedData + allocation.byteOffset;
  if (sourceData == nullptr) {
    std::memset(dst, 0, static_cast<size_t>(byteSize));
    return;
  }

  std::memcpy(dst, sourceData, static_cast<size_t>(byteSize));
}

Result
MeshPool::copy_upload_regions(const std::vector<BufferCopyRegion> &regions) {
  // - コピー対象がない呼び出しは使用ミスとして弾く
  if (regions.empty()) {
    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "Copy regions must not be empty.");
  }

  // - コピー専用の command context と queue を pool から借りる
  commandContextLease commandContext{};
  Result result =
      m_commandPool.get_command_context(CommandListType::Copy, commandContext);
  if (!result) {
    return result;
  }

  queueContextLease queueContext{};
  result = m_queuePool.get_queue_context(CommandListType::Copy, queueContext);
  if (!result) {
    m_commandPool.return_command_context(commandContext);
    return result;
  }

  // - context を初期化し、コピー command を記録できる状態にする
  result = commandContext->reset();
  if (result) {
    result = commandContext->setup(0, 1);
  }

  // - 各コピー先を CopyDest に遷移し、コピー後は Common に戻す
  if (result) {
    for (const BufferCopyRegion &region : regions) {
      ResourceBarrierDesc toCopyDestBarrier{};
      toCopyDestBarrier.after = ResourceState::CopyDest;
      result = commandContext->resource_barrier(region.dstBufferHandle,
                                                toCopyDestBarrier);
      if (!result) {
        break;
      }

      result = commandContext->copy_buffer_region(region);
      ResourceBarrierDesc toCommonBarrier{};
      toCommonBarrier.after = ResourceState::Common;
      Result restoreResult = commandContext->resource_barrier(
          region.dstBufferHandle, toCommonBarrier);
      if (!result) {
        break;
      }
      if (!restoreResult) {
        result = restoreResult;
        break;
      }
    }
  }

  if (result) {
    result = commandContext->close();
  }
  // - コピー queue に投入し、完了を待ってから staging 領域を再利用可能にする
  if (result) {
    std::vector<ICommandContext *> contexts{commandContext.get()};
    result = queueContext->submit(contexts);
  }
  if (result) {
    result = queueContext->signal();
  }
  if (result) {
    result = queueContext->wait();
  }

  m_commandPool.return_command_context(commandContext);
  m_queuePool.return_queue_context(queueContext);
  return result;
}

void MeshPool::destroy_stream_state(StreamState &streamState) {
  // - staging/default の順に破棄し、BufferManager の所有実体を明示的に返す
  if (streamState.stagingBufferHandle.valid()) {
    m_bufferManager.destroy_buffer(streamState.stagingBufferHandle);
    streamState.stagingBufferHandle = {};
  }
  if (streamState.defaultBufferHandle.valid()) {
    m_bufferManager.destroy_buffer(streamState.defaultBufferHandle);
    streamState.defaultBufferHandle = {};
  }

  streamState.debugName.clear();
  streamState.bufferType = BufferType::Unknown;
  // - 破棄後に再利用されても古い状態を参照しないよう、管理情報も初期化する
  streamState.freeRanges.clear();
  streamState.stagingRing.clear();
  streamState.mappedStagingData = nullptr;
  streamState.capacityInBytes = 0;
  streamState.stagingCapacityInBytes = 0;
}

Result MeshPool::allocate_mesh_id(uint32_t &outMeshId) {
  // - freeMeshIds をスタックとして使い、空なら MeshRange buffer
  // の容量超過を返す
  if (m_meshRangeState.freeMeshIds.empty()) {
    return Result::fail(Code::OutOfMemory, Severity::Error,
                        "Static mesh range buffer is out of mesh slots.");
  }

  outMeshId = m_meshRangeState.freeMeshIds.back();
  m_meshRangeState.freeMeshIds.pop_back();
  return Result::ok();
}

void MeshPool::release_mesh_id(uint32_t meshId) {
  // - 範囲外の meshId は無視し、範囲内のものだけ再利用候補へ戻す
  if (meshId >= m_meshRangeState.capacity) {
    return;
  }

  m_meshRangeState.freeMeshIds.push_back(meshId);
}

void MeshPool::write_mesh_range(uint32_t meshId, const MeshRange &meshRange) {
  // - 内部用途の即時更新なので、失敗は assert で検出する
  Result result = upload_mesh_range(meshId, meshRange);
  CUE_ASSERT_MSG(result, "Failed to write mesh range.");
}

Result MeshPool::upload_mesh_range(uint32_t meshId,
                                   const MeshRange &meshRange) {
  // - meshId は MeshRange buffer の要素番号なので capacity 内に制限する
  if (meshId >= m_meshRangeState.capacity) {
    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "Mesh range slot is out of bounds.");
  }

  // - 1 要素分の MeshRange を upload buffer に書き込む
  UploadAllocation uploadAllocation{};
  Result result = allocate_upload_range(m_meshRangeState, sizeof(MeshRange),
                                        alignof(MeshRange), uploadAllocation);
  if (!result) {
    return result;
  }

  write_upload_bytes(uploadAllocation, &meshRange, sizeof(MeshRange));

  // - meshId に対応する structured buffer 要素だけを更新する
  BufferCopyRegion region{};
  region.srcBufferHandle = uploadAllocation.bufferHandle;
  region.srcUploadResourceIndex = 0;
  region.srcByteOffset = uploadAllocation.byteOffset;
  region.dstBufferHandle = m_meshRangeState.defaultBufferHandle;
  region.dstDefaultResourceIndex = 0;
  region.dstByteOffset = static_cast<uint64_t>(meshId) * sizeof(MeshRange);
  region.byteSize = sizeof(MeshRange);

  std::vector<BufferCopyRegion> regions{region};
  result = copy_upload_regions(regions);
  release_upload_range(m_meshRangeState, uploadAllocation);
  return result;
}

Result MeshPool::upload_mesh_chunk_range(uint32_t meshId,
                                         const MeshChunkRange &meshChunkRange) {
  if (meshId >= m_meshChunkRangeState.capacity) {
    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "Mesh chunk range slot is out of bounds.");
  }

  UploadAllocation uploadAllocation{};
  Result result =
      allocate_upload_range(m_meshChunkRangeState, sizeof(MeshChunkRange),
                            alignof(MeshChunkRange), uploadAllocation);
  if (!result) {
    return result;
  }

  write_upload_bytes(uploadAllocation, &meshChunkRange, sizeof(MeshChunkRange));

  BufferCopyRegion region{};
  region.srcBufferHandle = uploadAllocation.bufferHandle;
  region.srcUploadResourceIndex = 0;
  region.srcByteOffset = uploadAllocation.byteOffset;
  region.dstBufferHandle = m_meshChunkRangeState.defaultBufferHandle;
  region.dstDefaultResourceIndex = 0;
  region.dstByteOffset = static_cast<uint64_t>(meshId) * sizeof(MeshChunkRange);
  region.byteSize = sizeof(MeshChunkRange);

  std::vector<BufferCopyRegion> regions{region};
  result = copy_upload_regions(regions);
  release_upload_range(m_meshChunkRangeState, uploadAllocation);
  return result;
}

Result MeshPool::allocate_mesh(const Core::Native::MeshData &meshData,
                               MeshHandle &outHandle) {
  // - 初期化状態と入力データを検証し、壊れた pool での割り当てを防ぐ
  outHandle = {};
  if (!m_initResult) {
    return m_initResult;
  }
  if (meshData.positions.empty() || meshData.indices.empty()) {
    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "MeshData must contain positions and indices.");
  }

  const uint32_t vertexCount = meshData.vertex_count();
  if (!meshData.uvs.empty() &&
      meshData.uvs.size() != meshData.positions.size()) {
    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "UV count must match the position count.");
  }
  if (!meshData.normals.empty() &&
      meshData.normals.size() != meshData.positions.size()) {
    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "Normal count must match the position count.");
  }
  if ((meshData.indices.size() % 3u) != 0u) {
    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "MeshData indices must form triangle lists.");
  }
  for (const uint32_t index : meshData.indices) {
    if (index >= vertexCount) {
      return Result::fail(Code::InvalidArgument, Severity::Error,
                          "MeshData contains an out-of-range index.");
    }
  }

  // - 常駐先の空き領域を先に押さえ、どれか一つでも足りなければ全体を巻き戻す
  MeshRecord record{};
  const std::vector<uint32_t> &rangeIndices =
      meshData.rangeIndices.empty() ? meshData.indices : meshData.rangeIndices;
  const std::vector<VisibilityTriangle> visibilityTriangles =
      build_visibility_triangles(meshData);
  record.vertexCount = vertexCount;
  record.indexCount = static_cast<uint32_t>(meshData.indices.size());
  record.positionByteSize = byte_size_of(meshData.positions);
  record.uvByteSize = static_cast<uint64_t>(vertexCount) * sizeof(Math::float2);
  record.normalByteSize =
      static_cast<uint64_t>(vertexCount) * sizeof(Math::float3);
  record.indexByteSize = byte_size_of(meshData.indices);
  record.visibilityTriangleByteSize = byte_size_of(visibilityTriangles);
  record.rangeIndexByteSize = byte_size_of(rangeIndices);
  record.meshletCount = static_cast<uint32_t>(meshData.meshletBounds.size());
  record.meshletByteSize = byte_size_of(meshData.meshletBounds);
  record.meshletChunkCount =
      record.meshletCount == 0 ? 0 : (record.meshletCount + 7u) / 8u;
  record.meshletChunkByteSize =
      static_cast<uint64_t>(record.meshletChunkCount) * sizeof(MeshletChunk);
  record.bounds = calculate_bounds(meshData.positions);
  Result result = allocate_mesh_id(record.meshId);
  if (!result) {
    return result;
  }

  result =
      allocate_stream_range(m_positionStream, record.positionByteSize,
                            alignof(Math::float4), record.positionByteOffset);
  if (!result) {
    release_mesh_id(record.meshId);
    return result;
  }

  result = allocate_stream_range(m_uvStream, record.uvByteSize,
                                 alignof(Math::float2), record.uvByteOffset);
  if (!result) {
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  result =
      allocate_stream_range(m_normalStream, record.normalByteSize,
                            alignof(Math::float3), record.normalByteOffset);
  if (!result) {
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  result = allocate_stream_range(m_indexStream, record.indexByteSize,
                                 alignof(uint32_t), record.indexByteOffset);
  if (!result) {
    release_stream_range(m_normalStream, record.normalByteOffset,
                         record.normalByteSize);
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  result = allocate_stream_range(
      m_visibilityTriangleStream, record.visibilityTriangleByteSize,
      alignof(VisibilityTriangle), record.visibilityTriangleByteOffset);
  if (!result) {
    release_stream_range(m_indexStream, record.indexByteOffset,
                         record.indexByteSize);
    release_stream_range(m_normalStream, record.normalByteOffset,
                         record.normalByteSize);
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  result =
      allocate_stream_range(m_rangeIndexStream, record.rangeIndexByteSize,
                            alignof(uint32_t), record.rangeIndexByteOffset);
  if (!result) {
    release_stream_range(m_visibilityTriangleStream,
                         record.visibilityTriangleByteOffset,
                         record.visibilityTriangleByteSize);
    release_stream_range(m_indexStream, record.indexByteOffset,
                         record.indexByteSize);
    release_stream_range(m_normalStream, record.normalByteOffset,
                         record.normalByteSize);
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  if (record.meshletByteSize > 0) {
    result = allocate_stream_range(
        m_meshletBoundsStream, record.meshletByteSize,
        alignof(Core::Native::MeshletBounds), record.meshletByteOffset);
    if (!result) {
      // - meshlet bounds は追加カリング用の任意データなので、容量不足時は
      //   メッシュ登録自体は継続し、後段では常に visible 扱いにする。
      record.meshletByteOffset = 0;
      record.meshletByteSize = 0;
      record.meshletCount = 0;
      record.meshletChunkByteOffset = 0;
      record.meshletChunkByteSize = 0;
      record.meshletChunkCount = 0;
      result = Result::ok();
    }
  }

  if (record.meshletChunkByteSize > 0) {
    result = allocate_stream_range(
        m_meshletChunkStream, record.meshletChunkByteSize,
        alignof(MeshletChunk), record.meshletChunkByteOffset);
    if (!result) {
      record.meshletChunkByteOffset = 0;
      record.meshletChunkByteSize = 0;
      record.meshletChunkCount = 0;
      result = Result::ok();
    }
  }

  // - 常設 staging に乗る分は ring を使い、乗らない分だけ一時 upload buffer
  // へ逃がす
  UploadAllocation positionUpload{};
  UploadAllocation uvUpload{};
  UploadAllocation normalUpload{};
  UploadAllocation indexUpload{};
  UploadAllocation visibilityTriangleUpload{};
  UploadAllocation rangeIndexUpload{};
  UploadAllocation meshletUpload{};
  UploadAllocation meshletChunkUpload{};

  result = allocate_upload_range(m_positionStream, record.positionByteSize,
                                 alignof(Math::float4), positionUpload);
  if (!result) {
    release_stream_range(m_meshletChunkStream, record.meshletChunkByteOffset,
                         record.meshletChunkByteSize);
    release_stream_range(m_meshletBoundsStream, record.meshletByteOffset,
                         record.meshletByteSize);
    release_stream_range(m_rangeIndexStream, record.rangeIndexByteOffset,
                         record.rangeIndexByteSize);
    release_stream_range(m_visibilityTriangleStream,
                         record.visibilityTriangleByteOffset,
                         record.visibilityTriangleByteSize);
    release_stream_range(m_indexStream, record.indexByteOffset,
                         record.indexByteSize);
    release_stream_range(m_normalStream, record.normalByteOffset,
                         record.normalByteSize);
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  result = allocate_upload_range(m_uvStream, record.uvByteSize,
                                 alignof(Math::float2), uvUpload);
  if (!result) {
    release_upload_range(m_positionStream, positionUpload);
    release_stream_range(m_meshletChunkStream, record.meshletChunkByteOffset,
                         record.meshletChunkByteSize);
    release_stream_range(m_meshletBoundsStream, record.meshletByteOffset,
                         record.meshletByteSize);
    release_stream_range(m_rangeIndexStream, record.rangeIndexByteOffset,
                         record.rangeIndexByteSize);
    release_stream_range(m_visibilityTriangleStream,
                         record.visibilityTriangleByteOffset,
                         record.visibilityTriangleByteSize);
    release_stream_range(m_indexStream, record.indexByteOffset,
                         record.indexByteSize);
    release_stream_range(m_normalStream, record.normalByteOffset,
                         record.normalByteSize);
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  result = allocate_upload_range(m_normalStream, record.normalByteSize,
                                 alignof(Math::float3), normalUpload);
  if (!result) {
    release_upload_range(m_uvStream, uvUpload);
    release_upload_range(m_positionStream, positionUpload);
    release_stream_range(m_meshletChunkStream, record.meshletChunkByteOffset,
                         record.meshletChunkByteSize);
    release_stream_range(m_meshletBoundsStream, record.meshletByteOffset,
                         record.meshletByteSize);
    release_stream_range(m_rangeIndexStream, record.rangeIndexByteOffset,
                         record.rangeIndexByteSize);
    release_stream_range(m_visibilityTriangleStream,
                         record.visibilityTriangleByteOffset,
                         record.visibilityTriangleByteSize);
    release_stream_range(m_indexStream, record.indexByteOffset,
                         record.indexByteSize);
    release_stream_range(m_normalStream, record.normalByteOffset,
                         record.normalByteSize);
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  result = allocate_upload_range(m_indexStream, record.indexByteSize,
                                 alignof(uint32_t), indexUpload);
  if (!result) {
    release_upload_range(m_normalStream, normalUpload);
    release_upload_range(m_uvStream, uvUpload);
    release_upload_range(m_positionStream, positionUpload);
    release_stream_range(m_meshletBoundsStream, record.meshletByteOffset,
                         record.meshletByteSize);
    release_stream_range(m_meshletChunkStream, record.meshletChunkByteOffset,
                         record.meshletChunkByteSize);
    release_stream_range(m_rangeIndexStream, record.rangeIndexByteOffset,
                         record.rangeIndexByteSize);
    release_stream_range(m_visibilityTriangleStream,
                         record.visibilityTriangleByteOffset,
                         record.visibilityTriangleByteSize);
    release_stream_range(m_indexStream, record.indexByteOffset,
                         record.indexByteSize);
    release_stream_range(m_normalStream, record.normalByteOffset,
                         record.normalByteSize);
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  result = allocate_upload_range(
      m_visibilityTriangleStream, record.visibilityTriangleByteSize,
      alignof(VisibilityTriangle), visibilityTriangleUpload);
  if (!result) {
    release_upload_range(m_indexStream, indexUpload);
    release_upload_range(m_normalStream, normalUpload);
    release_upload_range(m_uvStream, uvUpload);
    release_upload_range(m_positionStream, positionUpload);
    release_stream_range(m_meshletBoundsStream, record.meshletByteOffset,
                         record.meshletByteSize);
    release_stream_range(m_meshletChunkStream, record.meshletChunkByteOffset,
                         record.meshletChunkByteSize);
    release_stream_range(m_rangeIndexStream, record.rangeIndexByteOffset,
                         record.rangeIndexByteSize);
    release_stream_range(m_visibilityTriangleStream,
                         record.visibilityTriangleByteOffset,
                         record.visibilityTriangleByteSize);
    release_stream_range(m_indexStream, record.indexByteOffset,
                         record.indexByteSize);
    release_stream_range(m_normalStream, record.normalByteOffset,
                         record.normalByteSize);
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  result = allocate_upload_range(m_rangeIndexStream, record.rangeIndexByteSize,
                                 alignof(uint32_t), rangeIndexUpload);
  if (!result) {
    release_upload_range(m_visibilityTriangleStream, visibilityTriangleUpload);
    release_upload_range(m_indexStream, indexUpload);
    release_upload_range(m_normalStream, normalUpload);
    release_upload_range(m_uvStream, uvUpload);
    release_upload_range(m_positionStream, positionUpload);
    release_stream_range(m_meshletBoundsStream, record.meshletByteOffset,
                         record.meshletByteSize);
    release_stream_range(m_meshletChunkStream, record.meshletChunkByteOffset,
                         record.meshletChunkByteSize);
    release_stream_range(m_rangeIndexStream, record.rangeIndexByteOffset,
                         record.rangeIndexByteSize);
    release_stream_range(m_visibilityTriangleStream,
                         record.visibilityTriangleByteOffset,
                         record.visibilityTriangleByteSize);
    release_stream_range(m_indexStream, record.indexByteOffset,
                         record.indexByteSize);
    release_stream_range(m_normalStream, record.normalByteOffset,
                         record.normalByteSize);
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  if (record.meshletByteSize > 0) {
    result = allocate_upload_range(
        m_meshletBoundsStream, record.meshletByteSize,
        alignof(Core::Native::MeshletBounds), meshletUpload);
    if (!result) {
      // - meshlet bounds の upload だけ失敗した場合も描画は継続する。
      release_stream_range(m_meshletBoundsStream, record.meshletByteOffset,
                           record.meshletByteSize);
      release_stream_range(m_meshletChunkStream, record.meshletChunkByteOffset,
                           record.meshletChunkByteSize);
      record.meshletByteOffset = 0;
      record.meshletByteSize = 0;
      record.meshletCount = 0;
      record.meshletChunkByteOffset = 0;
      record.meshletChunkByteSize = 0;
      record.meshletChunkCount = 0;
      result = Result::ok();
    }
  }

  if (record.meshletChunkByteSize > 0) {
    result =
        allocate_upload_range(m_meshletChunkStream, record.meshletChunkByteSize,
                              alignof(MeshletChunk), meshletChunkUpload);
    if (!result) {
      release_stream_range(m_meshletChunkStream, record.meshletChunkByteOffset,
                           record.meshletChunkByteSize);
      record.meshletChunkByteOffset = 0;
      record.meshletChunkByteSize = 0;
      record.meshletChunkCount = 0;
      result = Result::ok();
    }
  }

  // - index/vertex の offset から GPU 側で参照する MeshRange を作る
  UploadAllocation meshRangeUpload{};
  UploadAllocation meshChunkRangeUpload{};
  MeshRange meshRange{};
  MeshChunkRange meshChunkRange{};
  meshRange.indexCount = record.indexCount;
  meshRange.startIndex =
      static_cast<uint32_t>(record.indexByteOffset / sizeof(uint32_t));
  meshRange.baseVertex =
      static_cast<int32_t>(record.positionByteOffset / sizeof(Math::float4));
  meshRange.firstMeshlet = static_cast<uint32_t>(
      record.meshletByteOffset / sizeof(Core::Native::MeshletBounds));
  meshRange.meshletCount = record.meshletCount;
  meshRange.rangeStartIndex =
      static_cast<uint32_t>(record.rangeIndexByteOffset / sizeof(uint32_t));
  meshRange.rangeIndexCount =
      static_cast<uint32_t>(record.rangeIndexByteSize / sizeof(uint32_t));
  meshRange.visibilityTriangleStart = static_cast<uint32_t>(
      record.visibilityTriangleByteOffset / sizeof(VisibilityTriangle));
  meshChunkRange.firstChunk = static_cast<uint32_t>(
      record.meshletChunkByteOffset / sizeof(MeshletChunk));
  meshChunkRange.chunkCount = record.meshletChunkCount;

  result = allocate_upload_range(m_meshRangeState, sizeof(MeshRange),
                                 alignof(MeshRange), meshRangeUpload);
  if (!result) {
    release_upload_range(m_meshletChunkStream, meshletChunkUpload);
    release_upload_range(m_meshletBoundsStream, meshletUpload);
    release_upload_range(m_rangeIndexStream, rangeIndexUpload);
    release_upload_range(m_visibilityTriangleStream, visibilityTriangleUpload);
    release_upload_range(m_indexStream, indexUpload);
    release_upload_range(m_normalStream, normalUpload);
    release_upload_range(m_uvStream, uvUpload);
    release_upload_range(m_positionStream, positionUpload);
    release_stream_range(m_meshletBoundsStream, record.meshletByteOffset,
                         record.meshletByteSize);
    release_stream_range(m_meshletChunkStream, record.meshletChunkByteOffset,
                         record.meshletChunkByteSize);
    release_stream_range(m_rangeIndexStream, record.rangeIndexByteOffset,
                         record.rangeIndexByteSize);
    release_stream_range(m_visibilityTriangleStream,
                         record.visibilityTriangleByteOffset,
                         record.visibilityTriangleByteSize);
    release_stream_range(m_indexStream, record.indexByteOffset,
                         record.indexByteSize);
    release_stream_range(m_normalStream, record.normalByteOffset,
                         record.normalByteSize);
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  result = allocate_upload_range(m_meshChunkRangeState, sizeof(MeshChunkRange),
                                 alignof(MeshChunkRange), meshChunkRangeUpload);
  if (!result) {
    release_upload_range(m_meshRangeState, meshRangeUpload);
    release_upload_range(m_meshletChunkStream, meshletChunkUpload);
    release_upload_range(m_meshletBoundsStream, meshletUpload);
    release_upload_range(m_rangeIndexStream, rangeIndexUpload);
    release_upload_range(m_visibilityTriangleStream, visibilityTriangleUpload);
    release_upload_range(m_indexStream, indexUpload);
    release_upload_range(m_normalStream, normalUpload);
    release_upload_range(m_uvStream, uvUpload);
    release_upload_range(m_positionStream, positionUpload);
    release_stream_range(m_meshletBoundsStream, record.meshletByteOffset,
                         record.meshletByteSize);
    release_stream_range(m_meshletChunkStream, record.meshletChunkByteOffset,
                         record.meshletChunkByteSize);
    release_stream_range(m_rangeIndexStream, record.rangeIndexByteOffset,
                         record.rangeIndexByteSize);
    release_stream_range(m_visibilityTriangleStream,
                         record.visibilityTriangleByteOffset,
                         record.visibilityTriangleByteSize);
    release_stream_range(m_indexStream, record.indexByteOffset,
                         record.indexByteSize);
    release_stream_range(m_normalStream, record.normalByteOffset,
                         record.normalByteSize);
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  // - CPU 側の meshData を各 upload 領域へ書き込む。未指定 UV/Normal
  // はゼロ埋めにする
  write_upload_bytes(positionUpload, meshData.positions.data(),
                     record.positionByteSize);
  write_upload_bytes(uvUpload,
                     meshData.uvs.empty() ? nullptr : meshData.uvs.data(),
                     record.uvByteSize);
  write_upload_bytes(normalUpload,
                     meshData.normals.empty() ? nullptr
                                              : meshData.normals.data(),
                     record.normalByteSize);
  write_upload_bytes(indexUpload, meshData.indices.data(),
                     record.indexByteSize);
  write_upload_bytes(visibilityTriangleUpload, visibilityTriangles.data(),
                     record.visibilityTriangleByteSize);
  write_upload_bytes(rangeIndexUpload, rangeIndices.data(),
                     record.rangeIndexByteSize);
  if (record.meshletByteSize > 0) {
    std::vector<Core::Native::MeshletBounds> rangeMeshletBounds =
        meshData.meshletBounds;
    const uint32_t rangeStartIndex =
        static_cast<uint32_t>(record.rangeIndexByteOffset / sizeof(uint32_t));
    for (Core::Native::MeshletBounds &meshletBounds : rangeMeshletBounds) {
      meshletBounds.firstIndex += rangeStartIndex;
    }
    write_upload_bytes(meshletUpload, rangeMeshletBounds.data(),
                       record.meshletByteSize);
  }
  if (record.meshletChunkByteSize > 0) {
    const uint32_t firstMeshletBase = static_cast<uint32_t>(
        record.meshletByteOffset / sizeof(Core::Native::MeshletBounds));
    const uint32_t rangeStartIndex =
        static_cast<uint32_t>(record.rangeIndexByteOffset / sizeof(uint32_t));
    const std::vector<MeshletChunk> meshletChunks = build_meshlet_chunks(
        meshData.meshletBounds, record.meshId, meshRange.baseVertex,
        firstMeshletBase, rangeStartIndex);
    write_upload_bytes(meshletChunkUpload, meshletChunks.data(),
                       record.meshletChunkByteSize);
  }
  write_upload_bytes(meshRangeUpload, &meshRange, sizeof(MeshRange));
  write_upload_bytes(meshChunkRangeUpload, &meshChunkRange,
                     sizeof(MeshChunkRange));

  // - 各 stream と MeshRange buffer へのコピーを 1 回の copy command にまとめる
  std::vector<BufferCopyRegion> uploadRegions{
      BufferCopyRegion{.srcBufferHandle = positionUpload.bufferHandle,
                       .srcUploadResourceIndex = 0,
                       .srcByteOffset = positionUpload.byteOffset,
                       .dstBufferHandle = m_positionStream.defaultBufferHandle,
                       .dstDefaultResourceIndex = 0,
                       .dstByteOffset = record.positionByteOffset,
                       .byteSize = record.positionByteSize},
      BufferCopyRegion{.srcBufferHandle = uvUpload.bufferHandle,
                       .srcUploadResourceIndex = 0,
                       .srcByteOffset = uvUpload.byteOffset,
                       .dstBufferHandle = m_uvStream.defaultBufferHandle,
                       .dstDefaultResourceIndex = 0,
                       .dstByteOffset = record.uvByteOffset,
                       .byteSize = record.uvByteSize},
      BufferCopyRegion{.srcBufferHandle = normalUpload.bufferHandle,
                       .srcUploadResourceIndex = 0,
                       .srcByteOffset = normalUpload.byteOffset,
                       .dstBufferHandle = m_normalStream.defaultBufferHandle,
                       .dstDefaultResourceIndex = 0,
                       .dstByteOffset = record.normalByteOffset,
                       .byteSize = record.normalByteSize},
      BufferCopyRegion{.srcBufferHandle = indexUpload.bufferHandle,
                       .srcUploadResourceIndex = 0,
                       .srcByteOffset = indexUpload.byteOffset,
                       .dstBufferHandle = m_indexStream.defaultBufferHandle,
                       .dstDefaultResourceIndex = 0,
                       .dstByteOffset = record.indexByteOffset,
                       .byteSize = record.indexByteSize},
      BufferCopyRegion{.srcBufferHandle = visibilityTriangleUpload.bufferHandle,
                       .srcUploadResourceIndex = 0,
                       .srcByteOffset = visibilityTriangleUpload.byteOffset,
                       .dstBufferHandle =
                           m_visibilityTriangleStream.defaultBufferHandle,
                       .dstDefaultResourceIndex = 0,
                       .dstByteOffset = record.visibilityTriangleByteOffset,
                       .byteSize = record.visibilityTriangleByteSize},
      BufferCopyRegion{.srcBufferHandle = rangeIndexUpload.bufferHandle,
                       .srcUploadResourceIndex = 0,
                       .srcByteOffset = rangeIndexUpload.byteOffset,
                       .dstBufferHandle =
                           m_rangeIndexStream.defaultBufferHandle,
                       .dstDefaultResourceIndex = 0,
                       .dstByteOffset = record.rangeIndexByteOffset,
                       .byteSize = record.rangeIndexByteSize},
      BufferCopyRegion{.srcBufferHandle = meshletUpload.bufferHandle,
                       .srcUploadResourceIndex = 0,
                       .srcByteOffset = meshletUpload.byteOffset,
                       .dstBufferHandle =
                           m_meshletBoundsStream.defaultBufferHandle,
                       .dstDefaultResourceIndex = 0,
                       .dstByteOffset = record.meshletByteOffset,
                       .byteSize = record.meshletByteSize},
      BufferCopyRegion{.srcBufferHandle = meshletChunkUpload.bufferHandle,
                       .srcUploadResourceIndex = 0,
                       .srcByteOffset = meshletChunkUpload.byteOffset,
                       .dstBufferHandle =
                           m_meshletChunkStream.defaultBufferHandle,
                       .dstDefaultResourceIndex = 0,
                       .dstByteOffset = record.meshletChunkByteOffset,
                       .byteSize = record.meshletChunkByteSize},
      BufferCopyRegion{.srcBufferHandle = meshRangeUpload.bufferHandle,
                       .srcUploadResourceIndex = 0,
                       .srcByteOffset = meshRangeUpload.byteOffset,
                       .dstBufferHandle = m_meshRangeState.defaultBufferHandle,
                       .dstDefaultResourceIndex = 0,
                       .dstByteOffset = static_cast<uint64_t>(record.meshId) *
                                        sizeof(MeshRange),
                       .byteSize = sizeof(MeshRange)},
      BufferCopyRegion{.srcBufferHandle = meshChunkRangeUpload.bufferHandle,
                       .srcUploadResourceIndex = 0,
                       .srcByteOffset = meshChunkRangeUpload.byteOffset,
                       .dstBufferHandle =
                           m_meshChunkRangeState.defaultBufferHandle,
                       .dstDefaultResourceIndex = 0,
                       .dstByteOffset = static_cast<uint64_t>(record.meshId) *
                                        sizeof(MeshChunkRange),
                       .byteSize = sizeof(MeshChunkRange)}};
  if (record.meshletByteSize == 0) {
    uploadRegions.erase(uploadRegions.begin() + 6);
  }
  if (record.meshletChunkByteSize == 0) {
    const size_t meshletChunkRegionIndex = record.meshletByteSize == 0 ? 6 : 7;
    uploadRegions.erase(uploadRegions.begin() +
                        static_cast<std::ptrdiff_t>(meshletChunkRegionIndex));
  }

  result = copy_upload_regions(uploadRegions);

  // - GPU コピーは同期完了まで待つため、ここで upload 領域を返却できる
  release_upload_range(m_meshChunkRangeState, meshChunkRangeUpload);
  release_upload_range(m_meshRangeState, meshRangeUpload);
  release_upload_range(m_meshletChunkStream, meshletChunkUpload);
  release_upload_range(m_meshletBoundsStream, meshletUpload);
  release_upload_range(m_rangeIndexStream, rangeIndexUpload);
  release_upload_range(m_visibilityTriangleStream, visibilityTriangleUpload);
  release_upload_range(m_indexStream, indexUpload);
  release_upload_range(m_normalStream, normalUpload);
  release_upload_range(m_uvStream, uvUpload);
  release_upload_range(m_positionStream, positionUpload);

  if (!result) {
    // - コピー失敗時は確保済み default heap 領域と meshId を登録前に巻き戻す
    release_stream_range(m_meshletChunkStream, record.meshletChunkByteOffset,
                         record.meshletChunkByteSize);
    release_stream_range(m_meshletBoundsStream, record.meshletByteOffset,
                         record.meshletByteSize);
    release_stream_range(m_rangeIndexStream, record.rangeIndexByteOffset,
                         record.rangeIndexByteSize);
    release_stream_range(m_visibilityTriangleStream,
                         record.visibilityTriangleByteOffset,
                         record.visibilityTriangleByteSize);
    release_stream_range(m_indexStream, record.indexByteOffset,
                         record.indexByteSize);
    release_stream_range(m_normalStream, record.normalByteOffset,
                         record.normalByteSize);
    release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
    release_stream_range(m_positionStream, record.positionByteOffset,
                         record.positionByteSize);
    release_mesh_id(record.meshId);
    return result;
  }

  // - 転送完了後にメッシュレコードを登録し、必要なら名前引きも更新する
  if (!meshData.name.empty()) {
    record.nameId = Core::fnv1a64(meshData.name);
  }

  MeshHandle handle = m_meshRegistry.create(record);
  m_meshIdToHandlesMap[record.meshId] = handle;
  m_allocatedMeshletChunkCount += record.meshletChunkCount;
  if (record.nameId != 0) {
    m_nameToHandlesMap[record.nameId] = handle;
  }

  outHandle = handle;
  return Result::ok();
}

Result MeshPool::free_mesh(MeshHandle handle) {
  // - レコードを解決して、常駐領域と名前引きをまとめて巻き戻す
  MeshRecord record{};
  if (!m_meshRegistry.try_copy_get(handle, record)) {
    return Result::fail(Code::NotFound, Severity::Error,
                        "Static mesh handle was not found.");
  }

  release_stream_range(m_positionStream, record.positionByteOffset,
                       record.positionByteSize);
  release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
  release_stream_range(m_normalStream, record.normalByteOffset,
                       record.normalByteSize);
  release_stream_range(m_indexStream, record.indexByteOffset,
                       record.indexByteSize);
  release_stream_range(m_visibilityTriangleStream,
                       record.visibilityTriangleByteOffset,
                       record.visibilityTriangleByteSize);
  release_stream_range(m_rangeIndexStream, record.rangeIndexByteOffset,
                       record.rangeIndexByteSize);
  release_stream_range(m_meshletBoundsStream, record.meshletByteOffset,
                       record.meshletByteSize);
  release_stream_range(m_meshletChunkStream, record.meshletChunkByteOffset,
                       record.meshletChunkByteSize);
  m_allocatedMeshletChunkCount -=
      (std::min)(m_allocatedMeshletChunkCount, record.meshletChunkCount);
  release_mesh_id(record.meshId);

  // - 解放済み meshId の MeshRange
  // をゼロに戻し、古い描画範囲が残らないようにする
  Result result = upload_mesh_range(record.meshId, MeshRange{});
  if (!result) {
    return result;
  }
  result = upload_mesh_chunk_range(record.meshId, MeshChunkRange{});
  if (!result) {
    return result;
  }

  // - 名前引きと meshId 引きの補助テーブルからも削除する
  if (record.nameId != 0) {
    const auto it = m_nameToHandlesMap.find(record.nameId);
    if (it != m_nameToHandlesMap.end() && it->second == handle) {
      m_nameToHandlesMap.erase(it);
    }
  }
  m_meshIdToHandlesMap.erase(record.meshId);

  // - registry から外してハンドルを無効化し、次回の再利用に備える
  if (!m_meshRegistry.destroy(handle)) {
    return Result::fail(Code::InternalError, Severity::Error,
                        "Failed to destroy static mesh record.");
  }

  return Result::ok();
}

Result MeshPool::get_mesh_id(MeshHandle handle, uint32_t &outMeshId) const {
  // - handle を registry で解決し、GPU 側 MeshRange の添字を返す
  MeshRecord record{};
  if (!m_meshRegistry.try_copy_get(handle, record)) {
    return Result::fail(Code::NotFound, Severity::Error,
                        "Static mesh handle was not found.");
  }

  outMeshId = record.meshId;
  return Result::ok();
}

Result MeshPool::get_bindings(MeshPoolBindings &outBindings) const {
  // - 描画パスが必要とする GPU buffer handle を現在の StreamState から集める
  outBindings.positionBuffer = m_positionStream.defaultBufferHandle;
  outBindings.uvBuffer = m_uvStream.defaultBufferHandle;
  outBindings.normalBuffer = m_normalStream.defaultBufferHandle;
  outBindings.indexBuffer = m_indexStream.defaultBufferHandle;
  outBindings.visibilityTriangleBuffer =
      m_visibilityTriangleStream.defaultBufferHandle;
  outBindings.rangeIndexBuffer = m_rangeIndexStream.defaultBufferHandle;
  outBindings.meshletBoundsBuffer = m_meshletBoundsStream.defaultBufferHandle;
  outBindings.meshletChunkBuffer = m_meshletChunkStream.defaultBufferHandle;
  outBindings.meshChunkRangeBuffer = m_meshChunkRangeState.defaultBufferHandle;
  outBindings.meshRangeBuffer = m_meshRangeState.defaultBufferHandle;
  outBindings.meshletBoundsSrv = m_meshletBoundsSrvHandle;
  outBindings.meshletChunkSrv = m_meshletChunkSrvHandle;
  outBindings.meshChunkRangeSrv = m_meshChunkRangeState.srvHandle;
  outBindings.meshRangeSrv = m_meshRangeState.srvHandle;
  return Result::ok();
}

Result MeshPool::get_mesh_range(uint32_t meshId,
                                MeshRange &outMeshRange) const {
  // - 失敗時に呼び出し側が古い値を使わないよう、出力を先に初期化する
  outMeshRange = {};

  // - meshId から registry handle を引き、登録済みレコードを取得する
  const auto it = m_meshIdToHandlesMap.find(meshId);
  if (it == m_meshIdToHandlesMap.end()) {
    return Result::fail(Code::NotFound, Severity::Error,
                        "Static mesh id was not found.");
  }

  MeshRecord record{};
  if (!m_meshRegistry.try_copy_get(it->second, record)) {
    return Result::fail(Code::NotFound, Severity::Error,
                        "Static mesh record was not found.");
  }

  // - record の byte offset を DrawIndexed 向けの index/vertex offset に戻す
  outMeshRange.indexCount = record.indexCount;
  outMeshRange.startIndex =
      static_cast<uint32_t>(record.indexByteOffset / sizeof(uint32_t));
  outMeshRange.baseVertex =
      static_cast<int32_t>(record.positionByteOffset / sizeof(Math::float4));
  outMeshRange.firstMeshlet = static_cast<uint32_t>(
      record.meshletByteOffset / sizeof(Core::Native::MeshletBounds));
  outMeshRange.meshletCount = record.meshletCount;
  outMeshRange.rangeStartIndex =
      static_cast<uint32_t>(record.rangeIndexByteOffset / sizeof(uint32_t));
  outMeshRange.rangeIndexCount =
      static_cast<uint32_t>(record.rangeIndexByteSize / sizeof(uint32_t));
  outMeshRange.visibilityTriangleStart = static_cast<uint32_t>(
      record.visibilityTriangleByteOffset / sizeof(VisibilityTriangle));
  return Result::ok();
}

Result MeshPool::get_mesh_bounds(uint32_t meshId, MeshBounds &outBounds) const {
  // - 失敗時に呼び出し側が古い値を使わないよう、出力を先に初期化する
  outBounds = {};

  // - meshId から登録済みレコードを引き、CPU 側で保持している境界情報を返す
  const auto it = m_meshIdToHandlesMap.find(meshId);
  if (it == m_meshIdToHandlesMap.end()) {
    return Result::fail(Code::NotFound, Severity::Error,
                        "Static mesh id was not found.");
  }

  MeshRecord record{};
  if (!m_meshRegistry.try_copy_get(it->second, record)) {
    return Result::fail(Code::NotFound, Severity::Error,
                        "Static mesh record was not found.");
  }

  outBounds = record.bounds;
  return Result::ok();
}

} // namespace Cue::DrawSystem
