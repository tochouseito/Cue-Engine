#pragma once

/// ************************************************************************************
/// フレームグラフ
/// ************************************************************************************

// === RHI includes ===
#include "BufferManager.h"
#include "PipelineManager.h"
#include "RHICommon.h"
#include "TextureManager.h"
#include "ViewManager.h"

// === C++ includes ===
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace Cue::RHI {
// FrameGraph 内で管理するリソースの種類。
// BufferHandle / TextureHandle は型が異なるため、依存解析用に共通の
// ResourceId へ変換するときに kind で区別する。
enum class ResourceKind : uint8_t { Buffer, Texture };

// パスがリソースに対して行うアクセス種別。
// Read 同士は並列化可能だが、Write を含む組み合わせは順序依存を作る。
enum class ResourceAccessType : uint8_t { Read, Write, ReadWrite };

// 依存解析で使う、Buffer / Texture 共通のリソース識別子。
// Handle の index と generation を保持し、破棄後に再利用された Handle を
// 別リソースとして扱えるようにしている。
struct ResourceId final {
  ResourceKind kind = ResourceKind::Buffer;
  uint32_t index = Core::Handle<Core::TestTag>::k_invalid;
  uint32_t generation = 0;

  bool valid() const noexcept {
    return index != Core::Handle<Core::TestTag>::k_invalid;
  }

  bool operator==(const ResourceId &a_other) const noexcept {
    return kind == a_other.kind && index == a_other.index &&
           generation == a_other.generation;
  }
};

struct ResourceIdHasher final {
  size_t operator()(const ResourceId &a_id) const noexcept {
    const uint64_t kindBits =
        static_cast<uint64_t>(static_cast<uint8_t>(a_id.kind)) << 63;
    const uint64_t indexBits = static_cast<uint64_t>(a_id.index) << 31;
    const uint64_t generationBits = static_cast<uint64_t>(a_id.generation);
    return static_cast<size_t>(kindBits ^ indexBits ^ generationBits);
  }
};

// describe_resources() で各パスが宣言するリソース利用情報。
// build() はこの宣言を使ってパス間依存を作り、execute() は
// requiredState / finalState から前後の resource barrier を発行する。
struct ResourceAccess final {
  ResourceId resourceId{};
  ResourceAccessType accessType = ResourceAccessType::Read;
  ResourceState requiredState = ResourceState::Common;
  ResourceState finalState = ResourceState::Common;
};

struct PassBuildInfo;
class FrameGraph;

// FrameGraphPass::setup() / describe_resources() に渡す構築用 API。
// リソース作成は FrameGraph の所有リストへ記録され、rebuild() / 破棄時に
// まとめて解放される。アクセス宣言は現在ビルド中の PassBuildInfo に積まれる。
class FrameGraphBuilder final {
public:
  FrameGraphBuilder(FrameGraph &frameGraph, PassBuildInfo *buildInfo = nullptr)
      : m_frameGraph(frameGraph), m_buildInfo(buildInfo) {}

  /// @brief buffer 作成宣言
  Result create_buffer(const BufferDesc &desc, BufferHandle &out);
  /// @brief texture 作成宣言
  Result create_texture(const TextureDesc &desc, TextureHandle &out);
  /// @brief 宣言済み buffer 取得
  Result get_buffer(std::string_view name, BufferHandle &out);
  /// @brief 宣言済み texture 取得
  Result get_texture(std::string_view name, TextureHandle &out);
  /// @brief view 作成宣言
  Result create_view(const ViewDesc &desc, ViewHandle &out);
  /// @brief 宣言済み view 取得
  Result get_view(std::string_view name, ViewHandle &out);
  /// @brief ルートシグネチャ作成宣言
  Result create_root_signature(const RootSignatureDesc &desc,
                               RootSignatureHandle &out);
  /// @brief 宣言済みルートシグネチャ取得
  Result get_root_signature(std::string_view name, RootSignatureHandle &out);
  /// @brief シェーダーブロブ作成宣言
  Result create_shader_blob(const ShaderCompileDesc &desc,
                            ShaderBlobHandle &out);
  /// @brief 宣言済みシェーダーブロブ取得
  Result get_shader_blob(std::string_view name, ShaderBlobHandle &out);
  /// @brief グラフィックスパイプライン作成宣言
  Result create_graphics_pipeline(const GraphicsPipelineStateDesc &desc,
                                  PipelineStateHandle &out);
  /// @brief 宣言済みグラフィックスパイプライン取得
  Result get_graphics_pipeline(std::string_view name, PipelineStateHandle &out);
  /// @brief コンピュートパイプライン作成宣言
  Result create_compute_pipeline(const ComputePipelineStateDesc &desc,
                                 PipelineStateHandle &out);
  /// @brief 宣言済みコンピュートパイプライン取得
  Result get_compute_pipeline(std::string_view name, PipelineStateHandle &out);

  /// @brief render target 書き込み宣言。現状は保持のみで、依存解析は
  /// write_texture() / use_texture() の宣言を参照する。
  Result render(const TextureHandle *handles, size_t count);

  /// @brief ShaderResource として buffer を読むことを宣言する。
  Result read_buffer(BufferHandle handle);
  /// @brief UnorderedAccess として buffer へ書くことを宣言する。
  Result write_buffer(BufferHandle handle);
  /// @brief UnorderedAccess として buffer を読み書きすることを宣言する。
  Result read_write_buffer(BufferHandle handle);
  /// @brief ShaderResource として texture を読むことを宣言する。
  Result read_texture(TextureHandle handle);
  /// @brief RenderTarget として texture へ書くことを宣言する。
  Result write_texture(TextureHandle handle);
  /// @brief UnorderedAccess として texture を読み書きすることを宣言する。
  Result read_write_texture(TextureHandle handle);
  /// @brief 任意の ResourceState で buffer のアクセスを宣言する。
  Result use_buffer(BufferHandle handle, ResourceAccessType accessType,
                    ResourceState requiredState, ResourceState finalState);
  /// @brief 任意の ResourceState で texture のアクセスを宣言する。
  Result use_texture(TextureHandle handle, ResourceAccessType accessType,
                     ResourceState requiredState, ResourceState finalState);

  uint32_t width() const noexcept;
  uint32_t height() const noexcept;
  const uint32_t &buffer_count() const noexcept;

private:
  Result register_buffer_access(BufferHandle handle,
                                ResourceAccessType accessType);
  Result register_texture_access(TextureHandle handle,
                                 ResourceAccessType accessType);
  Result register_buffer_access(BufferHandle handle,
                                ResourceAccessType accessType,
                                ResourceState requiredState,
                                ResourceState finalState);
  Result register_texture_access(TextureHandle handle,
                                 ResourceAccessType accessType,
                                 ResourceState requiredState,
                                 ResourceState finalState);

  FrameGraph &m_frameGraph;
  PassBuildInfo *m_buildInfo = nullptr;
  std::vector<TextureHandle> m_renderTargets;
};

struct FrameGraphContextDesc final {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t frameIndex = 0;
  ICommandContext *commandContext = nullptr;
  void *passStats = nullptr;
};

// FrameGraphPass::execute() に渡す実行時コンテキスト。
// パスはここから現在のフレーム情報と記録先の ICommandContext を取得する。
class FrameGraphContext final {
public:
  FrameGraphContext(const FrameGraphContextDesc &desc) : m_desc(desc) {}
  ~FrameGraphContext() = default;

  const uint32_t width() const noexcept { return m_desc.width; }
  const uint32_t height() const noexcept { return m_desc.height; }
  const uint32_t frame_index() const noexcept { return m_desc.frameIndex; }
  ICommandContext *commandContext() const noexcept {
    return m_desc.commandContext;
  }
  void *pass_stats() const noexcept { return m_desc.passStats; }

private:
  FrameGraphContextDesc m_desc{};
};

// FrameGraph に登録する 1 つの処理単位。
// setup() で必要な RHI オブジェクトを用意し、describe_resources() で
// 読み書きするリソースを宣言し、execute() で実際のコマンドを記録する。
class FrameGraphPass {
public:
  virtual ~FrameGraphPass() = default;
  virtual const char *name() const noexcept = 0;
  virtual CommandListType type() const noexcept = 0;
  // false を返したフレームでは execute() されない。
  // 依存関係は build() 時点の宣言を維持するため、フレームごとに変わらない。
  virtual bool is_enabled(uint32_t a_frameIndex) const noexcept {
    a_frameIndex;
    return true;
  }
  virtual Result setup(FrameGraphBuilder &builder) = 0;
  virtual Result describe_resources(FrameGraphBuilder &builder) = 0;
  virtual void execute(FrameGraphContext &context) = 0;
};

// build() 後に各パスへ残す解析結果。
// dependencyPassIndices は「このパスより先に完了している必要があるパス」の
// index。
struct PassBuildInfo final {
  std::string_view name{};
  CommandListType queueType = CommandListType::Graphics;
  std::vector<ResourceAccess> resourceAccesses{};
  std::vector<uint32_t> dependencyPassIndices{};
};

// 同じ依存段階かつ同じ Queue 種別のパスをまとめた実行単位。
// waitBatchIndices は別 Queue の producer batch を待つために使う。
struct QueueBatchInfo final {
  CommandListType queueType = CommandListType::Graphics;
  std::vector<uint32_t> passIndices{};
  std::vector<uint32_t> waitBatchIndices{};
};

// execute() のプロファイル結果。
// enableProfiling が false の場合、時間系の値は 0 にリセットされる。
struct FrameGraphExecutionStats final {
  struct PassExecutionStats final {
    struct DetailTiming final {
      std::string label{};
      double elapsedMs = 0.0;
    };

    std::string_view name{};
    CommandListType queueType = CommandListType::Graphics;
    double acquireResetSetupMs = 0.0;
    double preBarrierMs = 0.0;
    double cpuExecuteMs = 0.0;
    double postBarrierMs = 0.0;
    double closeMs = 0.0;
    double submitExecuteListsMs = 0.0;
    double submitSignalOnlyMs = 0.0;
    double submitSignalMs = 0.0;
    uint32_t submittedCommandListCount = 0;
    bool hasGpuExecuteMs = false;
    double gpuExecuteMs = 0.0;
    std::vector<DetailTiming> detailTimings{};
  };

  double totalExecuteMs = 0.0;
  double submitMs = 0.0;
  double queueWaitMs = 0.0;
  double interQueueWaitMs = 0.0;
  double finalQueueWaitMs = 0.0;
  double contextRecycleWaitMs = 0.0;
  double finalGraphicsWaitMs = 0.0;
  double finalComputeWaitMs = 0.0;
  double finalCopyWaitMs = 0.0;
  uint64_t graphicsFenceValue = 0;
  uint64_t computeFenceValue = 0;
  uint64_t copyFenceValue = 0;
  bool hasGpuFrameMs = false;
  double gpuFrameMs = 0.0;
  std::vector<PassExecutionStats> passStats{};
};

// FrameGraph が依存する RHI サービスと実行オプション。
// 各 Manager / Pool の実体は外部所有で、FrameGraph は参照して利用する。
struct FrameGraphDesc final {
  IBufferManager *bufferManager = nullptr;
  ITextureManager *textureManager = nullptr;
  IViewManager *viewManager = nullptr;
  IPipelineManager *pipelineManager = nullptr;
  ICommandPool *commandPool = nullptr;
  IQueuePool *queuePool = nullptr;
  bool usePresentQueue = true;
  bool enableProfiling = false;
  bool waitForCompletion = false;
  bool enablePassTimingLog = false;
  uint32_t passTimingLogInterval = 1;
  uint32_t width = 0;
  uint32_t height = 0;
};

/// @brief フレーム単位のレンダリングパスを依存順に実行するグラフ。
///
/// add_pass() で登録された順に setup() / describe_resources() を呼び、
/// リソースアクセス宣言から依存関係を構築する。依存のないパスは
/// Queue 種別ごとに batch 化され、Graphics / Compute / Copy Queue 間の
/// wait を含む実行計画として execute() で処理される。
class FrameGraph final {
  friend class FrameGraphBuilder;
  friend class FrameGraphContext;

public:
  FrameGraph(const FrameGraphDesc &desc, const uint32_t &bufferCount);
  // コピー禁止
  FrameGraph(const FrameGraph &) = delete;
  FrameGraph &operator=(const FrameGraph &) = delete;
  // ムーブ禁止
  FrameGraph(FrameGraph &&) = delete;
  FrameGraph &operator=(FrameGraph &&) = delete;
  ~FrameGraph();

  /// @brief パスの追加
  void add_pass(std::unique_ptr<FrameGraphPass> pass) {
    // null pass は受け付けない
    if (pass == nullptr) {
      return;
    }

    m_passes.push_back(CompiledPass{std::move(pass)});
  }
  /// @brief 描画依存関係を構築する
  Result build();
  /// @brief build() で作成したリソースを破棄し、サイズを更新して再構築する。
  Result rebuild(uint32_t a_width, uint32_t a_height);
  /// @brief build() 済みの実行計画に従って、指定フレームのパスを実行する。
  Result execute(uint32_t frameIndex);

  const std::vector<PassBuildInfo> &pass_build_infos() const noexcept {
    return m_passBuildInfos;
  }

  FrameGraphExecutionStats execution_stats_copy() const noexcept;
  FrameGraphExecutionStats execution_stats_summary_copy() const noexcept;

private:
  Result cleanup_build_resources();
  // ResourceAccess の読み書き種別からパス間依存を導出する。
  Result build_dependencies();
  // 依存グラフが循環していないことを確認する。
  Result validate_dependency_graph() const;
  // トポロジカル順に QueueBatchInfo を作り、Queue 間 wait を解決する。
  Result build_execution_plan();

  struct CompiledPass final {
    std::unique_ptr<FrameGraphPass> pass = nullptr;
    PassBuildInfo buildInfo{};
  };

private:
  static ResourceId make_resource_id(BufferHandle handle) noexcept;
  static ResourceId make_resource_id(TextureHandle handle) noexcept;
  static BufferHandle make_buffer_handle(const ResourceId &resourceId) noexcept;
  static TextureHandle
  make_texture_handle(const ResourceId &resourceId) noexcept;
  static ResourceAccessType
  merge_access_type(ResourceAccessType current,
                    ResourceAccessType incoming) noexcept;
  static bool has_dependency(ResourceAccessType previous,
                             ResourceAccessType next) noexcept;

  FrameGraphDesc m_desc;
  const uint32_t &m_bufferCount;
  std::vector<CompiledPass> m_passes;
  std::vector<PassBuildInfo> m_passBuildInfos;
  std::vector<QueueBatchInfo> m_executionPlan;
  mutable std::mutex m_executionStatsMutex{};
  FrameGraphExecutionStats m_executionStats{};
  uint64_t m_executeCount = 0;
  std::vector<BufferHandle> m_createdBuffers;
  std::vector<TextureHandle> m_createdTextures;
  std::vector<ViewHandle> m_createdViews;
  std::vector<RootSignatureHandle> m_createdRootSignatures;
  std::vector<ShaderBlobHandle> m_createdShaderBlobs;
  std::vector<PipelineStateHandle> m_createdGraphicsPipelines;
  std::vector<PipelineStateHandle> m_createdComputePipelines;
};
} // namespace Cue::RHI
