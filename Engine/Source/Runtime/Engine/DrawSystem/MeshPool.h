#pragma once

/// ****************************************************************************
/// MeshPool.h
/// ****************************************************************************

// === RHI includes ===
#include <BufferManager.h>
#include <RHICommon.h>
#include <ViewManager.h>

// === Math includes ===
#include <CueMath.h>

// === Core includes ===
#include <Native/EngineNativeStruct.h>

namespace Cue::DrawSystem {
using RHI::BufferCopyRegion;
using RHI::BufferDesc;
using RHI::BufferHandle;
using RHI::BufferKind;
using RHI::BufferType;
using RHI::commandContextLease;
using RHI::CommandListType;
using RHI::ICommandContext;
using RHI::MeshHandle;
using RHI::MeshTag;
using RHI::queueContextLease;
using RHI::ResourceBarrierDesc;
using RHI::ResourceState;
using RHI::UploadBufferView;
using RHI::ViewDesc;
using RHI::ViewHandle;
using RHI::ViewType;

/// @brief MeshPool の初期化パラメータ
struct MeshPoolDesc final {
  uint32_t maxVertexCount =
      4u * 1024u * 1024u; // プール内で保持できる最大頂点数
  uint32_t maxIndexCount =
      4u * 1024u * 1024u; // プール内で保持できる最大インデックス数
  uint32_t maxRangeIndexCount =
      4u * 1024u * 1024u; // Range 描画用 IndexBuffer の最大インデックス数
  uint32_t maxMeshCount = 4u * 1024u; // プール内で管理できる最大メッシュ数
  uint32_t maxMeshletCount =
      1024u * 1024u; // 追加カリング用 meshlet bounds の最大数
  uint32_t maxMeshletChunkCount =
      256u * 1024u; // MeshletChunk visibility 用 chunk の最大数
  uint32_t positionStagingSize =
      1u * 1024u * 1024u; // Position stream 用の常設 staging サイズ
  uint32_t uvStagingSize = 512u * 1024u; // UV stream 用の常設 staging サイズ
  uint32_t normalStagingSize =
      1u * 1024u * 1024u; // Normal stream 用の常設 staging サイズ
  uint32_t indexStagingSize =
      1u * 1024u * 1024u; // Index stream 用の常設 staging サイズ
  uint32_t rangeIndexStagingSize =
      1u * 1024u * 1024u; // Range Index stream 用の常設 staging サイズ
  uint32_t meshletBoundsStagingSize =
      1024u * 1024u; // Meshlet bounds 用の常設 staging サイズ
  uint32_t meshletChunkStagingSize =
      512u * 1024u; // MeshletChunk 用の常設 staging サイズ
  uint32_t meshChunkRangeStagingCount =
      256u; // MeshChunkRange 用の常設 staging 要素数
  uint32_t meshRangeStagingCount = 256u; // MeshRange 用の常設 staging 要素数
  std::string_view positionName =
      "MeshPool.Position";                 // Position buffer のデバッグ名
  std::string_view uvName = "MeshPool.Uv"; // UV buffer のデバッグ名
  std::string_view normalName = "MeshPool.Normal"; // Normal buffer のデバッグ名
  std::string_view indexName = "MeshPool.Index";   // Index buffer のデバッグ名
  std::string_view rangeIndexName =
      "MeshPool.RangeIndex"; // Range 描画用 Index buffer のデバッグ名
  std::string_view meshletBoundsName =
      "MeshPool.MeshletBounds"; // Meshlet bounds buffer のデバッグ名
  std::string_view meshletBoundsSrvName =
      "MeshPool.MeshletBoundsSRV"; // Meshlet bounds SRV のデバッグ名
  std::string_view meshletChunkName =
      "MeshPool.MeshletChunk"; // MeshletChunk buffer のデバッグ名
  std::string_view meshletChunkSrvName =
      "MeshPool.MeshletChunkSRV"; // MeshletChunk SRV のデバッグ名
  std::string_view meshChunkRangeName =
      "MeshPool.MeshChunkRange"; // meshId -> chunk range buffer のデバッグ名
  std::string_view meshChunkRangeSrvName =
      "MeshPool.MeshChunkRangeSRV"; // MeshChunkRange SRV のデバッグ名
  std::string_view meshRangeName =
      "MeshPool.MeshRange"; // MeshRange buffer のデバッグ名
  std::string_view meshRangeSrvName =
      "MeshPool.MeshRangeSRV"; // MeshRange SRV のデバッグ名
};

/// @brief GPU 側で参照する 1 メッシュ分のインデックス描画範囲
struct MeshRange final {
  uint32_t indexCount = 0;      // DrawIndexed に渡すインデックス数
  uint32_t startIndex = 0;      // Index buffer 内の開始インデックス
  int32_t baseVertex = 0;       // Position buffer 内の基準頂点
  uint32_t firstMeshlet = 0;    // MeshletBounds buffer 内の開始要素
  uint32_t meshletCount = 0;    // このメッシュに属する meshlet 数
  uint32_t rangeStartIndex = 0; // Range Index buffer 内の開始インデックス
  uint32_t rangeIndexCount =
      0; // Range Index buffer 上でこの mesh が占めるインデックス数
  uint32_t padding2 = 0; // GPU 構造体の 16 byte 境界合わせ
};

/// @brief メッシュの中心点と外接球半径
struct MeshBounds final {
  Math::float3 center = Math::float3::zero(); // ローカル空間の中心位置
  float radius = 0.0f;                        // center から最遠頂点までの距離
};

/// @brief meshId ごとに対応する MeshletChunk 範囲
struct MeshChunkRange final {
  uint32_t firstChunk = 0; // MeshletChunk buffer 内の開始要素
  uint32_t chunkCount = 0; // この mesh に属する chunk 数
  uint32_t padding0 = 0;
  uint32_t padding1 = 0;
};

/// @brief 複数 meshlet をまとめた coarse visibility / depth 用 chunk
struct MeshletChunk final {
  uint32_t startIndex = 0;    // RangeIndex buffer 内の開始 index
  uint32_t indexCount = 0;    // この chunk が覆う index 数
  int32_t baseVertex = 0;     // Position buffer 内の基準頂点
  uint32_t firstMeshlet = 0;  // MeshletBounds buffer 内の開始 meshlet
  uint32_t meshletCount = 0;  // この chunk が含む meshlet 数
  uint32_t meshId = 0;        // 所属 meshId
  uint32_t materialId = 0;    // 将来の material bucket 用。現状は 0。
  uint32_t lod = 0;           // 将来の LOD bucket 用。現状は 0。
  Math::float3 boundsCenter = Math::float3::zero();
  float boundsRadius = 0.0f;
  Math::float3 coneAxis = Math::float3(0.0f, 0.0f, 1.0f);
  float coneCutoff = -1.0f;
};

/// @brief MeshPool が管理する GPU リソースのハンドルをまとめた構造体
struct MeshPoolBindings final {
  BufferHandle positionBuffer = {};      // 頂点位置ストリーム
  BufferHandle uvBuffer = {};            // UV ストリーム
  BufferHandle normalBuffer = {};        // 法線ストリーム
  BufferHandle indexBuffer = {};         // インデックスストリーム
  BufferHandle rangeIndexBuffer = {};    // Range 描画用インデックスストリーム
  BufferHandle meshletBoundsBuffer = {}; // Meshlet bounds ストリーム
  BufferHandle meshletChunkBuffer = {};  // MeshletChunk ストリーム
  BufferHandle meshChunkRangeBuffer =
      {}; // meshId から MeshletChunk 範囲を引く structured buffer
  BufferHandle meshRangeBuffer =
      {}; // MeshRange 配列を保持する structured buffer
  ViewHandle meshletBoundsSrv = {}; // Meshlet bounds を読むための SRV
  ViewHandle meshletChunkSrv = {};  // MeshletChunk を読むための SRV
  ViewHandle meshChunkRangeSrv =
      {}; // MeshChunkRange を読むための SRV
  ViewHandle meshRangeSrv = {};     // シェーダから MeshRange を読むための SRV
};

/// @brief 登録済みメッシュ 1 件の CPU 側管理情報
struct MeshRecord final {
  Core::ResourceNameId nameId = 0;   // メッシュ名から生成した検索用 ID
  uint32_t meshId = 0;               // MeshRange buffer の要素インデックス
  uint32_t vertexCount = 0;          // 登録時の頂点数
  uint32_t indexCount = 0;           // 登録時のインデックス数
  uint64_t positionByteOffset = 0;   // Position stream 内の開始 byte offset
  uint64_t positionByteSize = 0;     // Position stream に確保した byte 数
  uint64_t uvByteOffset = 0;         // UV stream 内の開始 byte offset
  uint64_t uvByteSize = 0;           // UV stream に確保した byte 数
  uint64_t normalByteOffset = 0;     // Normal stream 内の開始 byte offset
  uint64_t normalByteSize = 0;       // Normal stream に確保した byte 数
  uint64_t indexByteOffset = 0;      // Index stream 内の開始 byte offset
  uint64_t indexByteSize = 0;        // Index stream に確保した byte 数
  uint64_t rangeIndexByteOffset = 0; // Range Index stream 内の開始 byte offset
  uint64_t rangeIndexByteSize = 0;   // Range Index stream に確保した byte 数
  uint64_t meshletByteOffset = 0; // MeshletBounds stream 内の開始 byte offset
  uint64_t meshletByteSize = 0;   // MeshletBounds stream に確保した byte 数
  uint32_t meshletCount = 0;      // このメッシュに属する meshlet 数
  uint64_t meshletChunkByteOffset =
      0; // MeshletChunk stream 内の開始 byte offset
  uint64_t meshletChunkByteSize =
      0; // MeshletChunk stream に確保した byte 数
  uint32_t meshletChunkCount = 0; // このメッシュに属する chunk 数
  MeshBounds bounds{};            // CPU 側で保持するメッシュ境界情報
  bool hasSkinInfluence = false; // スキニング用 influence stream を使うかどうか
};

/// @brief default heap 上で未使用になっている連続 byte 範囲
struct FreeRange final {
  uint64_t byteOffset = 0; // 空き範囲の開始 byte offset
  uint64_t byteSize = 0;   // 空き範囲の byte 数
};

/// @brief 頂点属性またはインデックスの 1 ストリームを管理する状態
struct StreamState final {
  std::string debugName{}; // RHI リソース生成時に使った識別名
  BufferType bufferType =
      BufferType::Unknown;               // Vertex / Index などの buffer 種別
  BufferHandle defaultBufferHandle = {}; // GPU 常駐先の default heap buffer
  BufferHandle stagingBufferHandle = {}; // アップロード用の常設 staging buffer
  std::vector<FreeRange> freeRanges{};   // default heap buffer 内の空き範囲一覧
  Core::RingBuffer stagingRing{};        // staging buffer の一時割り当て管理
  std::byte *mappedStagingData =
      nullptr;                         // staging buffer の CPU mapped address
  uint64_t capacityInBytes = 0;        // default heap buffer の総 byte 数
  uint64_t stagingCapacityInBytes = 0; // 常設 staging buffer の総 byte 数
  uint32_t alignment = 1;              // このストリームの割り当て alignment
};

/// @brief MeshRange buffer と meshId の払い出しを管理する状態
struct MeshRangeState final {
  std::string debugName{};               // RHI リソース生成時に使った識別名
  BufferHandle defaultBufferHandle = {}; // MeshRange 配列の GPU 常駐 buffer
  BufferHandle stagingBufferHandle =
      {};                         // MeshRange 更新用の常設 staging buffer
  ViewHandle srvHandle = {};      // MeshRange 配列を読むための SRV
  Core::RingBuffer stagingRing{}; // staging buffer の一時割り当て管理
  std::byte *mappedStagingData =
      nullptr;                         // staging buffer の CPU mapped address
  uint64_t stagingCapacityInBytes = 0; // 常設 staging buffer の総 byte 数
  uint32_t capacity = 0;               // 払い出せる meshId の最大数
  std::vector<uint32_t> freeMeshIds{}; // 再利用可能な meshId のスタック
};

/// @brief meshId と MeshletChunk 範囲を対応させる状態
struct MeshChunkRangeState final {
  std::string debugName{};
  BufferHandle defaultBufferHandle = {};
  BufferHandle stagingBufferHandle = {};
  ViewHandle srvHandle = {};
  Core::RingBuffer stagingRing{};
  std::byte *mappedStagingData = nullptr;
  uint64_t stagingCapacityInBytes = 0;
  uint32_t capacity = 0;
};

/// @brief staging buffer 上に確保したアップロード用領域
struct UploadAllocation final {
  BufferHandle bufferHandle = {};  // コピー元として使う upload buffer
  std::byte *mappedData = nullptr; // upload buffer の CPU mapped address
  uint64_t byteOffset = 0;         // upload buffer 内の開始 byte offset
  uint64_t byteSize = 0;           // 確保した byte 数
  Core::RingBuffer::Allocation
      ringAllocation{}; // 常設 staging 使用時の ring allocation
  bool isTransient =
      false; // 常設 staging ではなく一時 upload buffer を使っているか

  /// @brief コピー元として利用できる割り当てかどうか
  [[nodiscard]] bool valid() const noexcept {
    return bufferHandle.valid() && mappedData != nullptr && byteSize > 0;
  }
};

/// @brief 複数メッシュの頂点/インデックスデータを共有 GPU buffer
/// 上に集約して管理するクラス
class MeshPool {
public:
  /// @brief MeshPool を生成し、各 GPU buffer と staging buffer を初期化する
  MeshPool(const MeshPoolDesc &desc, RHI::IBufferManager &bufferManager,
           RHI::IViewManager &viewManager, RHI::ICommandPool &commandPool,
           RHI::IQueuePool &queuePool);

  // コピー禁止
  MeshPool(const MeshPool &) = delete;
  MeshPool &operator=(const MeshPool &) = delete;

  // ムーブは許可
  MeshPool(MeshPool &&) = default;
  MeshPool &operator=(MeshPool &&) = default;

  /// @brief MeshPool が確保した GPU リソースと SRV を破棄する
  ~MeshPool();

  // --- Mesh の割り当てと解放 ---

  /// @brief MeshData をプールへアップロードし、参照用 MeshHandle を返す
  Result allocate_mesh(const Core::Native::MeshData &meshData,
                       MeshHandle &outHandle);

  /// @brief MeshHandle に対応する領域と meshId を解放する
  Result free_mesh(MeshHandle handle);

  /// @brief MeshHandle からシェーダ参照用の meshId を取得する
  Result get_mesh_id(MeshHandle handle, uint32_t &outMeshId) const;

  /// @brief meshId に対応する描画範囲を取得する
  Result get_mesh_range(uint32_t meshId, MeshRange &outMeshRange) const;

  /// @brief meshId に対応する境界情報を取得する
  Result get_mesh_bounds(uint32_t meshId, MeshBounds &outBounds) const;

  /// @brief 描画パスが参照する MeshPool の GPU リソース一式を取得する
  Result get_bindings(MeshPoolBindings &outBindings) const;

  /// @brief MeshletChunk buffer に確保済みの chunk 数を返す
  [[nodiscard]] uint32_t allocated_meshlet_chunk_count() const noexcept {
    return m_allocatedMeshletChunkCount;
  }

  /// @brief MeshletChunk buffer の最大 chunk 数を返す
  [[nodiscard]] uint32_t max_meshlet_chunk_count() const noexcept {
    return m_maxMeshletChunkCount;
  }

private:
  /// @brief position / uv / normal / index 各ストリームの buffer
  /// 状態を初期化する
  Result initialize_streams(const MeshPoolDesc &desc);

  /// @brief MeshRange 用 buffer、SRV、meshId の空きリストを初期化する
  Result initialize_mesh_range_state(const MeshPoolDesc &desc);

  /// @brief meshId から MeshletChunk 範囲を引く buffer を初期化する
  Result initialize_mesh_chunk_range_state(const MeshPoolDesc &desc);

  /// @brief 1 つの頂点属性またはインデックスストリーム用 buffer 状態を作成する
  Result create_stream_state(std::string_view bufferName, BufferType bufferType,
                             uint64_t totalBytes, uint64_t stagingBytes,
                             uint32_t stride, uint32_t elementCount,
                             uint32_t alignment, StreamState &outStreamState);

  /// @brief CPU mapped 済みの upload buffer を作成する
  Result create_upload_buffer(std::string_view bufferName,
                              BufferType bufferType, uint64_t byteSize,
                              BufferHandle &outBufferHandle,
                              std::byte *&outMappedData);

  /// @brief default heap buffer 内の空き範囲から連続領域を確保する
  Result allocate_stream_range(StreamState &streamState, uint64_t byteSize,
                               uint32_t alignment, uint64_t &outOffset);

  /// @brief default heap buffer 内の領域を空き範囲へ戻す
  void release_stream_range(StreamState &streamState, uint64_t byteOffset,
                            uint64_t byteSize);

  /// @brief ストリーム更新用の upload 領域を確保する
  Result allocate_upload_range(StreamState &streamState, uint64_t byteSize,
                               uint32_t alignment,
                               UploadAllocation &outAllocation);

  /// @brief MeshRange 更新用の upload 領域を確保する
  Result allocate_upload_range(MeshRangeState &meshRangeState,
                               uint64_t byteSize, uint32_t alignment,
                               UploadAllocation &outAllocation);

  /// @brief MeshChunkRange 更新用の upload 領域を確保する
  Result allocate_upload_range(MeshChunkRangeState &meshChunkRangeState,
                               uint64_t byteSize, uint32_t alignment,
                               UploadAllocation &outAllocation);

  /// @brief ストリーム更新用の upload 領域を解放する
  void release_upload_range(StreamState &streamState,
                            UploadAllocation &allocation);

  /// @brief MeshRange 更新用の upload 領域を解放する
  void release_upload_range(MeshRangeState &meshRangeState,
                            UploadAllocation &allocation);

  /// @brief MeshChunkRange 更新用の upload 領域を解放する
  void release_upload_range(MeshChunkRangeState &meshChunkRangeState,
                            UploadAllocation &allocation);

  /// @brief upload 領域へ CPU 側データを書き込む
  void write_upload_bytes(const UploadAllocation &allocation,
                          const void *sourceData, uint64_t byteSize);

  /// @brief 複数の upload buffer から default heap buffer へコピーを発行する
  Result copy_upload_regions(const std::vector<BufferCopyRegion> &regions);

  /// @brief StreamState が保持する default / staging buffer を破棄する
  void destroy_stream_state(StreamState &streamState);

  /// @brief MeshRange buffer の空き要素から meshId を払い出す
  Result allocate_mesh_id(uint32_t &outMeshId);

  /// @brief meshId を再利用可能な空きリストへ戻す
  void release_mesh_id(uint32_t meshId);

  /// @brief mapped staging 上に MeshRange を書き込む
  void write_mesh_range(uint32_t meshId, const MeshRange &meshRange);

  /// @brief MeshRange buffer の指定 meshId 要素を GPU 側へアップロードする
  Result upload_mesh_range(uint32_t meshId, const MeshRange &meshRange);

  /// @brief MeshChunkRange buffer の指定 meshId 要素を GPU 側へアップロードする
  Result upload_mesh_chunk_range(uint32_t meshId,
                                 const MeshChunkRange &meshChunkRange);

private:
  RHI::IBufferManager &m_bufferManager; // buffer の生成/破棄を行う外部 manager
  RHI::IViewManager
      &m_viewManager; // SRV などの view を生成/破棄する外部 manager
  RHI::ICommandPool &m_commandPool; // GPU コピー用 command context の取得元
  RHI::IQueuePool &m_queuePool; // GPU コピー command を投入する queue の取得元
  Core::Registry<RHI::MeshTag, MeshRecord>
      m_meshRegistry; // MeshHandle と MeshRecord の対応表
  std::unordered_map<Core::ResourceNameId, MeshHandle>
      m_nameToHandlesMap; // メッシュ名 ID から MeshHandle を引く表
  std::unordered_map<uint32_t, MeshHandle>
      m_meshIdToHandlesMap;         // meshId から MeshHandle を引く表
  StreamState m_positionStream{};   // 位置データ用ストリーム
  StreamState m_uvStream{};         // UV データ用ストリーム
  StreamState m_normalStream{};     // 法線データ用ストリーム
  StreamState m_influenceStream{};  // スキニング influence データ用ストリーム
  StreamState m_indexStream{};      // インデックスデータ用ストリーム
  StreamState m_rangeIndexStream{}; // Range 描画用インデックスデータストリーム
  StreamState
      m_meshletBoundsStream{}; // 追加カリング用 meshlet bounds ストリーム
  StreamState m_meshletChunkStream{}; // coarse MeshletChunk ストリーム
  ViewHandle m_meshletBoundsSrvHandle{}; // Meshlet bounds buffer の SRV
  ViewHandle m_meshletChunkSrvHandle{}; // MeshletChunk buffer の SRV
  MeshRangeState m_meshRangeState{};     // MeshRange buffer と meshId 管理状態
  MeshChunkRangeState
      m_meshChunkRangeState{}; // meshId -> MeshletChunk range 管理状態
  uint32_t m_maxMeshletChunkCount = 0; // MeshletChunk buffer の最大 chunk 数
  uint32_t m_allocatedMeshletChunkCount = 0; // 登録済み MeshletChunk 数
  Result m_initResult = Result::ok();    // コンストラクタ内初期化の結果
};
} // namespace Cue::DrawSystem
