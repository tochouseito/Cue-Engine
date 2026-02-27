#include "FrameGraph.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>

namespace Cue::GraphicsCore
{
    namespace
    {
        [[nodiscard]] Result make_frame_graph_error(Code code, std::string_view message) noexcept
        {
            // 1) FrameGraph用の失敗結果を生成する。
            return Result::fail(Facility::GraphicsCore, code, Severity::Error, 0u, message);
        }

        [[nodiscard]] BufferHandle to_buffer_handle(const ResourceRef& ref) noexcept
        {
            // 1) 論理参照をBufferHandleへ変換する。
            return BufferHandle{ ref.index, ref.generation };
        }

        [[nodiscard]] TextureHandle to_texture_handle(const ResourceRef& ref) noexcept
        {
            // 1) 論理参照をTextureHandleへ変換する。
            return TextureHandle{ ref.index, ref.generation };
        }

        void append_unique_index(std::vector<size_t>& values, size_t value)
        {
            // 1) 重複を避けて末尾追加する。
            if (std::find(values.begin(), values.end(), value) == values.end())
            {
                values.push_back(value);
            }
        }

    } // namespace

    BufferHandle FrameGraphBuilder::create_buffer(std::string_view name)
    {
        // 1) 同名論理バッファを作成または再利用する。
        ResourceRef ref{};
        const Result result = m_frameGraph.create_named_resource(ResourceKind::Buffer, name, ref);
        if (!result)
        {
            m_frameGraph.set_setup_error(result);
            return {};
        }

        return to_buffer_handle(ref);
    }

    TextureHandle FrameGraphBuilder::create_texture(std::string_view name)
    {
        // 1) 同名論理テクスチャを作成または再利用する。
        ResourceRef ref{};
        const Result result = m_frameGraph.create_named_resource(ResourceKind::Texture, name, ref);
        if (!result)
        {
            m_frameGraph.set_setup_error(result);
            return {};
        }

        return to_texture_handle(ref);
    }

    BufferHandle FrameGraphBuilder::import_buffer(const ImportedBufferDesc& desc)
    {
        // 1) 外部バッファを論理リソースとして登録する。
        ResourceRef ref{};
        const Result result = m_frameGraph.import_named_buffer(desc, ref);
        if (!result)
        {
            m_frameGraph.set_setup_error(result);
            return {};
        }

        return to_buffer_handle(ref);
    }

    TextureHandle FrameGraphBuilder::import_texture(const ImportedTextureDesc& desc)
    {
        // 1) 外部テクスチャを論理リソースとして登録する。
        ResourceRef ref{};
        const Result result = m_frameGraph.import_named_texture(desc, ref);
        if (!result)
        {
            m_frameGraph.set_setup_error(result);
            return {};
        }

        return to_texture_handle(ref);
    }

    BufferHandle FrameGraphBuilder::get_buffer(std::string_view name)
    {
        // 1) 既存論理バッファを解決する。
        ResourceRef ref{};
        const Result result = m_frameGraph.get_named_resource(ResourceKind::Buffer, name, ref);
        if (!result)
        {
            m_frameGraph.set_setup_error(result);
            return {};
        }

        return to_buffer_handle(ref);
    }

    TextureHandle FrameGraphBuilder::get_texture(std::string_view name)
    {
        // 1) 既存論理テクスチャを解決する。
        ResourceRef ref{};
        const Result result = m_frameGraph.get_named_resource(ResourceKind::Texture, name, ref);
        if (!result)
        {
            m_frameGraph.set_setup_error(result);
            return {};
        }

        return to_texture_handle(ref);
    }

    void FrameGraphBuilder::read(BufferHandle handle, ResourceState requiredState)
    {
        // 1) 読み取りアクセスを宣言する。
        const Result result = m_frameGraph.push_access(
            ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Read, requiredState, false, ResourceState::Common },
            m_accesses);
        if (!result)
        {
            m_frameGraph.set_setup_error(result);
        }
    }

    void FrameGraphBuilder::read(TextureHandle handle, ResourceState requiredState)
    {
        // 1) 読み取りアクセスを宣言する。
        const Result result = m_frameGraph.push_access(
            ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Read, requiredState, false, ResourceState::Common },
            m_accesses);
        if (!result)
        {
            m_frameGraph.set_setup_error(result);
        }
    }

    void FrameGraphBuilder::write(BufferHandle handle, ResourceState requiredState)
    {
        // 1) 終了状態を固定しない書き込みを宣言する。
        const Result result = m_frameGraph.push_access(
            ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, requiredState, false, ResourceState::Common },
            m_accesses);
        if (!result)
        {
            m_frameGraph.set_setup_error(result);
        }
    }

    void FrameGraphBuilder::write(TextureHandle handle, ResourceState requiredState)
    {
        // 1) 終了状態を固定しない書き込みを宣言する。
        const Result result = m_frameGraph.push_access(
            ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, requiredState, false, ResourceState::Common },
            m_accesses);
        if (!result)
        {
            m_frameGraph.set_setup_error(result);
        }
    }

    void FrameGraphBuilder::write(BufferHandle handle, ResourceState requiredState, ResourceState finalState)
    {
        // 1) 終了状態を指定した書き込みを宣言する。
        const Result result = m_frameGraph.push_access(
            ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, requiredState, true, finalState },
            m_accesses);
        if (!result)
        {
            m_frameGraph.set_setup_error(result);
        }
    }

    void FrameGraphBuilder::write(TextureHandle handle, ResourceState requiredState, ResourceState finalState)
    {
        // 1) 終了状態を指定した書き込みを宣言する。
        const Result result = m_frameGraph.push_access(
            ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, requiredState, true, finalState },
            m_accesses);
        if (!result)
        {
            m_frameGraph.set_setup_error(result);
        }
    }

    Result FrameGraph::add_pass(std::unique_ptr<FrameGraphPass> pass)
    {
        // 1) null Pass を拒否する。
        if (!pass)
        {
            return make_frame_graph_error(Code::InvalidArg, "add_pass: pass is null");
        }

        // 2) 登録し、再buildを要求状態へ戻す。
        const size_t index = m_passes.size();
        m_passes.push_back(CompiledPass{ std::move(pass), {}, CommandListType::Graphics, index });
        m_isBuilt = false;
        return Result::ok();
    }

    Result FrameGraph::build()
    {
        // 1) 前回buildの生成物を破棄する。
        reset_build_artifacts();

        // 2) Passが無ければ空グラフとして成功扱いにする。
        if (m_passes.empty())
        {
            m_isBuilt = true;
            return Result::ok();
        }

        // 3) 宣言収集と計画コンパイルを順に実行する。
        Result result = collect_pass_declarations();
        if (!result)
        {
            return result;
        }

        result = compile_dependencies();
        if (!result)
        {
            return result;
        }

        result = compile_execution_order();
        if (!result)
        {
            return result;
        }

        result = compile_queue_waits();
        if (!result)
        {
            return result;
        }

        result = compile_barriers();
        if (!result)
        {
            return result;
        }

        // 4) 実行可能状態へ遷移する。
        m_signalPointByPass.assign(m_passes.size(), QueueSyncPoint{});
        m_isBuilt = true;
        return Result::ok();
    }

    Result FrameGraph::execute(IFrameGraphRuntime& runtime)
    {
        // 1) build済みであることを確認する。
        if (!m_isBuilt)
        {
            return make_frame_graph_error(Code::InvalidState, "execute: graph is not built");
        }

        // 2) 実行順に従い、Passごとのcommand contextへ記録してsubmitする。
        for (size_t passIndex : m_executionOrder)
        {
            if (passIndex >= m_passes.size())
            {
                return make_frame_graph_error(Code::InvalidState, "execute: invalid pass index");
            }

            CompiledPass& compiled = m_passes[passIndex];
            PassExecutionPlan& plan = m_plansByPass[passIndex];

            IQueueContext* queue = runtime.get_queue_context(plan.queueType);
            if (queue == nullptr)
            {
                return make_frame_graph_error(Code::NotFound, "execute: queue context not found");
            }

            for (const QueueWaitEvent& waitEvent : plan.waitEvents)
            {
                QueueSyncPoint point{};

                if (waitEvent.isExternalSyncPoint)
                {
                    point = waitEvent.externalSyncPoint;
                }
                else
                {
                    if (waitEvent.sourcePassIndex >= m_signalPointByPass.size())
                    {
                        return make_frame_graph_error(Code::InvalidState, "execute: wait source index out of range");
                    }

                    point = m_signalPointByPass[waitEvent.sourcePassIndex];
                }
                if (point.value == 0)
                {
                    return make_frame_graph_error(Code::InvalidState, "execute: missing source queue signal");
                }

                Result result = queue->wait(point);
                if (!result && (result.code != Code::Unsupported))
                {
                    return result;
                }
            }

            ICommandContext* cmd = runtime.acquire_pass_command_context(plan.queueType, passIndex);
            if (cmd == nullptr)
            {
                return make_frame_graph_error(Code::NotFound, "execute: command context not found");
            }

            Result result = issue_barriers(*cmd, plan.preBarriers);
            if (!result)
            {
                return result;
            }

            const char* eventName = compiled.pass->name();
            if ((eventName == nullptr) || (eventName[0] == '\0'))
            {
                eventName = "FrameGraphPass";
            }

            cmd->begin_event(eventName);
            compiled.pass->execute(*cmd);
            cmd->end_event();

            result = issue_barriers(*cmd, plan.postBarriers);
            if (!result)
            {
                return result;
            }

            result = cmd->close();
            if (!result)
            {
                return result;
            }

            result = queue->submit(*cmd);
            if (!result && (result.code != Code::Unsupported))
            {
                return result;
            }

            QueueSyncPoint signalPoint{};
            result = queue->signal(signalPoint);
            if (!result && (result.code != Code::Unsupported))
            {
                return result;
            }

            if (result)
            {
                m_signalPointByPass[passIndex] = signalPoint;
            }
        }

        return Result::ok();
    }

    Result FrameGraph::create_named_resource(ResourceKind kind, std::string_view name, ResourceRef& outRef)
    {
        // 1) 名前を検証する。
        if (name.empty())
        {
            return make_frame_graph_error(Code::InvalidArg, "create resource: empty name");
        }

        const ResourceNameId nameId = fnv1a64(name);

        // 2) 既存リソースがあれば種別一致を確認して返す。
        const auto found = m_resourceByNameId.find(nameId);
        if (found != m_resourceByNameId.end())
        {
            if (found->second.kind != kind)
            {
                return make_frame_graph_error(Code::InvalidArg, "create resource: kind mismatch");
            }

            outRef = found->second;
            return Result::ok();
        }

        // 3) 新規論理リソースを登録する。
        if (m_resourceKinds.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
        {
            return make_frame_graph_error(Code::OutOfMemory, "create resource: index overflow");
        }

        const uint32_t index = static_cast<uint32_t>(m_resourceKinds.size());
        outRef = ResourceRef{ kind, index, 0u };

        m_resourceKinds.push_back(kind);
        m_resourceInfos.push_back(LogicalResourceInfo{ kind });
        m_resourceByNameId.emplace(nameId, outRef);
        return Result::ok();
    }

    Result FrameGraph::import_named_buffer(const ImportedBufferDesc& desc, ResourceRef& outRef)
    {
        // 1) import記述を検証する。
        if (desc.name.empty())
        {
            return make_frame_graph_error(Code::InvalidArg, "import buffer: empty name");
        }
        if (!desc.sourceHandle.valid())
        {
            return make_frame_graph_error(Code::InvalidArg, "import buffer: invalid source handle");
        }

        // 2) 論理リソースを作成または取得する。
        Result result = create_named_resource(ResourceKind::Buffer, desc.name, outRef);
        if (!result)
        {
            return result;
        }

        // 3) import属性を保存する。
        LogicalResourceInfo& info = m_resourceInfos[outRef.index];
        info.imported = true;
        info.ownerQueue = desc.ownerQueue;
        info.initialState = desc.initialState;
        info.hasFinalState = desc.hasFinalState;
        info.finalState = desc.finalState;
        info.hasInitialSyncPoint = desc.hasInitialSyncPoint;
        info.initialSyncPoint = desc.initialSyncPoint;
        info.importedBufferHandle = desc.sourceHandle;
        return Result::ok();
    }

    Result FrameGraph::import_named_texture(const ImportedTextureDesc& desc, ResourceRef& outRef)
    {
        // 1) import記述を検証する。
        if (desc.name.empty())
        {
            return make_frame_graph_error(Code::InvalidArg, "import texture: empty name");
        }
        if (!desc.sourceHandle.valid())
        {
            return make_frame_graph_error(Code::InvalidArg, "import texture: invalid source handle");
        }

        // 2) 論理リソースを作成または取得する。
        Result result = create_named_resource(ResourceKind::Texture, desc.name, outRef);
        if (!result)
        {
            return result;
        }

        // 3) import属性を保存する。
        LogicalResourceInfo& info = m_resourceInfos[outRef.index];
        info.imported = true;
        info.ownerQueue = desc.ownerQueue;
        info.initialState = desc.initialState;
        info.hasFinalState = desc.hasFinalState;
        info.finalState = desc.finalState;
        info.hasInitialSyncPoint = desc.hasInitialSyncPoint;
        info.initialSyncPoint = desc.initialSyncPoint;
        info.importedTextureHandle = desc.sourceHandle;
        return Result::ok();
    }

    Result FrameGraph::get_named_resource(ResourceKind kind, std::string_view name, ResourceRef& outRef) const
    {
        // 1) 名前を検証する。
        if (name.empty())
        {
            return make_frame_graph_error(Code::InvalidArg, "get resource: empty name");
        }

        const ResourceNameId nameId = fnv1a64(name);
        const auto found = m_resourceByNameId.find(nameId);
        if (found == m_resourceByNameId.end())
        {
            return make_frame_graph_error(Code::NotFound, "get resource: not found");
        }
        if (found->second.kind != kind)
        {
            return make_frame_graph_error(Code::InvalidArg, "get resource: kind mismatch");
        }

        outRef = found->second;
        return Result::ok();
    }

    Result FrameGraph::validate_resource_ref(const ResourceRef& ref) const
    {
        // 1) ハンドル妥当性を確認する。
        if (!ref.valid())
        {
            return make_frame_graph_error(Code::InvalidArg, "resource access: invalid handle");
        }
        if (ref.index >= m_resourceKinds.size())
        {
            return make_frame_graph_error(Code::InvalidArg, "resource access: out of range");
        }
        if (m_resourceKinds[ref.index] != ref.kind)
        {
            return make_frame_graph_error(Code::InvalidArg, "resource access: kind mismatch");
        }

        return Result::ok();
    }

    Result FrameGraph::push_access(const ResourceAccess& access, std::vector<ResourceAccess>& accesses)
    {
        // 1) 宣言されたアクセスの対象を検証する。
        const Result result = validate_resource_ref(access.handle);
        if (!result)
        {
            return result;
        }

        // 2) アクセス宣言を収集する。
        accesses.push_back(access);
        return Result::ok();
    }

    void FrameGraph::set_setup_error(Result result) noexcept
    {
        // 1) 最初の失敗だけを保持して根本原因を残す。
        if (m_setupError)
        {
            if (!result)
            {
                m_setupError = result;
            }
        }
    }

    bool FrameGraph::has_setup_error() const noexcept
    {
        // 1) setupフェーズ中の失敗有無を返す。
        return !static_cast<bool>(m_setupError);
    }

    void FrameGraph::reset_build_artifacts()
    {
        // 1) build生成物と一時状態を初期化する。
        m_isBuilt = false;
        m_setupError = Result::ok();
        m_resourceKinds.clear();
        m_resourceInfos.clear();
        m_resourceByNameId.clear();
        m_plansByPass.clear();
        m_executionOrder.clear();
        m_signalPointByPass.clear();

        // 2) 各Passのsetup結果も再収集するためクリアする。
        for (size_t i = 0; i < m_passes.size(); ++i)
        {
            m_passes[i].accesses.clear();
            m_passes[i].queueType = CommandListType::Graphics;
            m_passes[i].originalIndex = i;
        }
    }

    Result FrameGraph::collect_pass_declarations()
    {
        // 1) Passごとのqueue typeとアクセス宣言を収集する。
        for (CompiledPass& compiled : m_passes)
        {
            if (!compiled.pass)
            {
                return make_frame_graph_error(Code::InvalidState, "build: null pass");
            }

            compiled.queueType = compiled.pass->queue_type();
            FrameGraphBuilder builder(*this, compiled.accesses);
            compiled.pass->setup(builder);

            if (has_setup_error())
            {
                return m_setupError;
            }
        }

        return Result::ok();
    }

    Result FrameGraph::compile_dependencies()
    {
        // 1) Passごとの計画コンテナを初期化する。
        m_plansByPass.assign(m_passes.size(), PassExecutionPlan{});
        for (size_t i = 0; i < m_passes.size(); ++i)
        {
            m_plansByPass[i].queueType = m_passes[i].queueType;
        }

        std::vector<ResourceHazardState> hazardStates(m_resourceKinds.size());

        // 2) 宣言順を基準にハザード依存とアクセス区間を抽出する。
        for (size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
        {
            const CompiledPass& compiled = m_passes[passIndex];
            PassExecutionPlan& plan = m_plansByPass[passIndex];

            for (const ResourceAccess& access : compiled.accesses)
            {
                const Result validation = validate_resource_ref(access.handle);
                if (!validation)
                {
                    return validation;
                }

                LogicalResourceInfo& resourceInfo = m_resourceInfos[access.handle.index];
                if (resourceInfo.firstAccessPass < 0)
                {
                    resourceInfo.firstAccessPass = static_cast<int32_t>(passIndex);
                }
                resourceInfo.lastAccessPass = static_cast<int32_t>(passIndex);

                ResourceHazardState& hazard = hazardStates[access.handle.index];
                if (access.type == ResourceAccessType::Read)
                {
                    if (hazard.lastWriter >= 0)
                    {
                        append_unique_index(plan.dependencies, static_cast<size_t>(hazard.lastWriter));
                    }

                    const int32_t passIndex32 = static_cast<int32_t>(passIndex);
                    if (std::find(hazard.lastReaders.begin(), hazard.lastReaders.end(), passIndex32) == hazard.lastReaders.end())
                    {
                        hazard.lastReaders.push_back(passIndex32);
                    }
                }
                else
                {
                    if (hazard.lastWriter >= 0)
                    {
                        append_unique_index(plan.dependencies, static_cast<size_t>(hazard.lastWriter));
                    }
                    for (int32_t reader : hazard.lastReaders)
                    {
                        if (reader >= 0)
                        {
                            append_unique_index(plan.dependencies, static_cast<size_t>(reader));
                        }
                    }

                    hazard.lastReaders.clear();
                    hazard.lastWriter = static_cast<int32_t>(passIndex);
                }
            }

            std::sort(plan.dependencies.begin(), plan.dependencies.end());
        }

        return Result::ok();
    }

    Result FrameGraph::compile_execution_order()
    {
        // 1) 依存情報から入次数と隣接リストを構築する。
        m_executionOrder.clear();
        m_executionOrder.reserve(m_passes.size());

        std::vector<size_t> indegree(m_passes.size(), 0u);
        std::vector<std::vector<size_t>> adjacency(m_passes.size());

        for (size_t passIndex = 0; passIndex < m_plansByPass.size(); ++passIndex)
        {
            for (size_t dependencyIndex : m_plansByPass[passIndex].dependencies)
            {
                if (dependencyIndex >= m_passes.size())
                {
                    return make_frame_graph_error(Code::InvalidState, "build: dependency index out of range");
                }

                ++indegree[passIndex];
                adjacency[dependencyIndex].push_back(passIndex);
            }
        }

        // 2) 安定トポロジカルソートで再順序化する（低い元順番を優先）。
        std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready;
        for (size_t i = 0; i < indegree.size(); ++i)
        {
            if (indegree[i] == 0u)
            {
                ready.push(i);
            }
        }

        while (!ready.empty())
        {
            const size_t passIndex = ready.top();
            ready.pop();

            m_executionOrder.push_back(passIndex);

            for (size_t dependentIndex : adjacency[passIndex])
            {
                if (indegree[dependentIndex] == 0u)
                {
                    return make_frame_graph_error(Code::InvalidState, "build: invalid indegree state");
                }

                --indegree[dependentIndex];
                if (indegree[dependentIndex] == 0u)
                {
                    ready.push(dependentIndex);
                }
            }
        }

        // 3) 閉路があれば失敗とする。
        if (m_executionOrder.size() != m_passes.size())
        {
            return make_frame_graph_error(Code::InvalidState, "build: dependency cycle detected");
        }

        return Result::ok();
    }

    Result FrameGraph::compile_queue_waits()
    {
        // 1) 既存の待機計画をクリアする。
        for (PassExecutionPlan& plan : m_plansByPass)
        {
            plan.waitEvents.clear();
        }

        auto append_wait = [](std::vector<QueueWaitEvent>& waits, const QueueWaitEvent& candidate)
        {
            // 1) 同一待機イベントの重複を防ぐ。
            const auto it = std::find_if(
                waits.begin(),
                waits.end(),
                [&candidate](const QueueWaitEvent& event)
                {
                    return (event.isExternalSyncPoint == candidate.isExternalSyncPoint)
                        && (event.sourcePassIndex == candidate.sourcePassIndex)
                        && (event.sourceQueue == candidate.sourceQueue)
                        && (event.waitQueue == candidate.waitQueue)
                        && (event.externalSyncPoint.queueType == candidate.externalSyncPoint.queueType)
                        && (event.externalSyncPoint.value == candidate.externalSyncPoint.value);
                });
            if (it == waits.end())
            {
                waits.push_back(candidate);
            }
        };

        // 2) 依存辺のうちキュー跨ぎのものを wait/signal 対象として登録する。
        for (size_t passIndex = 0; passIndex < m_plansByPass.size(); ++passIndex)
        {
            PassExecutionPlan& plan = m_plansByPass[passIndex];
            for (size_t dependencyIndex : plan.dependencies)
            {
                const CommandListType sourceQueue = m_plansByPass[dependencyIndex].queueType;
                const CommandListType waitQueue = plan.queueType;
                if (sourceQueue != waitQueue)
                {
                    append_wait(
                        plan.waitEvents,
                        QueueWaitEvent{ false, dependencyIndex, sourceQueue, waitQueue, {} });
                }
            }
        }

        // 3) importされた外部同期点があれば、最初のアクセスPassに待機計画を追加する。
        for (size_t resourceIndex = 0; resourceIndex < m_resourceInfos.size(); ++resourceIndex)
        {
            const LogicalResourceInfo& info = m_resourceInfos[resourceIndex];
            if (!info.imported || !info.hasInitialSyncPoint || (info.firstAccessPass < 0))
            {
                continue;
            }

            const size_t firstPassIndex = static_cast<size_t>(info.firstAccessPass);
            if (firstPassIndex >= m_plansByPass.size())
            {
                return make_frame_graph_error(Code::InvalidState, "build: first access index out of range");
            }

            PassExecutionPlan& plan = m_plansByPass[firstPassIndex];
            append_wait(
                plan.waitEvents,
                QueueWaitEvent{ true, 0u, info.initialSyncPoint.queueType, plan.queueType, info.initialSyncPoint });
        }

        return Result::ok();
    }

    Result FrameGraph::compile_barriers()
    {
        // 1) 初期状態をimport情報から構築し、barrier計画をクリアする。
        std::vector<ResourceState> currentStateByResource(m_resourceInfos.size(), ResourceState::Common);
        for (size_t i = 0; i < m_resourceInfos.size(); ++i)
        {
            currentStateByResource[i] = m_resourceInfos[i].initialState;
        }

        for (PassExecutionPlan& plan : m_plansByPass)
        {
            plan.preBarriers.clear();
            plan.postBarriers.clear();
        }

        // 2) 実行順に沿って前後バリアを計画する。
        for (size_t passIndex : m_executionOrder)
        {
            if (passIndex >= m_passes.size())
            {
                return make_frame_graph_error(Code::InvalidState, "build: barrier pass index out of range");
            }

            const CompiledPass& compiled = m_passes[passIndex];
            PassExecutionPlan& plan = m_plansByPass[passIndex];

            for (const ResourceAccess& access : compiled.accesses)
            {
                ResourceState& currentState = currentStateByResource[access.handle.index];
                if (currentState != access.requiredState)
                {
                    plan.preBarriers.push_back(
                        BarrierEvent{
                            ResourceBarrierDesc{
                                access.handle.kind,
                                access.handle.index,
                                access.handle.generation,
                                currentState,
                                access.requiredState },
                            true });
                    currentState = access.requiredState;
                }

                if (access.hasFinalState && (currentState != access.finalState))
                {
                    plan.postBarriers.push_back(
                        BarrierEvent{
                            ResourceBarrierDesc{
                                access.handle.kind,
                                access.handle.index,
                                access.handle.generation,
                                currentState,
                                access.finalState },
                            false });
                    currentState = access.finalState;
                }
            }
        }

        // 3) importされた外部リソースの最終状態要求を、最後に触れたPassの後ろへ付与する。
        for (size_t resourceIndex = 0; resourceIndex < m_resourceInfos.size(); ++resourceIndex)
        {
            const LogicalResourceInfo& info = m_resourceInfos[resourceIndex];
            if (!info.imported || !info.hasFinalState || (info.lastAccessPass < 0))
            {
                continue;
            }

            const size_t lastPassIndex = static_cast<size_t>(info.lastAccessPass);
            if (lastPassIndex >= m_plansByPass.size())
            {
                return make_frame_graph_error(Code::InvalidState, "build: last access index out of range");
            }

            ResourceState& currentState = currentStateByResource[resourceIndex];
            if (currentState == info.finalState)
            {
                continue;
            }

            m_plansByPass[lastPassIndex].postBarriers.push_back(
                BarrierEvent{
                    ResourceBarrierDesc{
                        info.kind,
                        static_cast<uint32_t>(resourceIndex),
                        0u,
                        currentState,
                        info.finalState },
                    false });
            currentState = info.finalState;
        }

        return Result::ok();
    }

    Result FrameGraph::issue_barriers(ICommandContext& cmd, const std::vector<BarrierEvent>& barriers)
    {
        // 1) バリアが無ければ何もしない。
        if (barriers.empty())
        {
            return Result::ok();
        }

        // 2) 抽象APIへ一括発行する。
        std::vector<ResourceBarrierDesc> descs;
        descs.reserve(barriers.size());
        for (const BarrierEvent& event : barriers)
        {
            descs.push_back(event.barrier);
        }

        Result result = cmd.resource_barriers(descs.data(), descs.size());
        if (!result && (result.code != Code::Unsupported))
        {
            return result;
        }

        return Result::ok();
    }
} // namespace Cue::GraphicsCore
