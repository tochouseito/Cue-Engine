#include "ResourceUploader.h"

namespace Cue::RHI::DX12
{
    namespace
    {
        struct TransitionRecord final
        {
            DX12GpuResource* resource = nullptr;
            D3D12_RESOURCE_STATES oldState = D3D12_RESOURCE_STATE_COMMON;
        };
    }

    ResourceUploader::ResourceUploader(
        DX12BufferManager& a_bufferManager,
        DX12CommandPool& a_commandPool,
        DX12QueuePool& a_queuePool)
        : m_bufferManager(a_bufferManager)
        , m_commandPool(a_commandPool)
        , m_queuePool(a_queuePool)
    {
    }

    Result ResourceUploader::upload_buffer_region(const BufferUploadRegion& a_region)
    {
        // 1) 単一リージョン API も複数リージョン経由へ寄せて、検証と submit 経路を一本化する。
        return upload_buffer_regions(std::vector<BufferUploadRegion>{ a_region });
    }

    Result ResourceUploader::upload_buffer_regions(const std::vector<BufferUploadRegion>& a_regions)
    {
        // 1) コピー対象が無い呼び出しを早期に弾き、空 submit を作らないようにする。
        if (a_regions.empty())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Upload regions must not be empty.");
        }

        // 2) Copy 用の command context / queue を確保し、記録開始状態へ戻す。
        CommandContextLease commandContext{};
        Result result = m_commandPool.get_command_context(CommandListType::Copy, commandContext);
        if (!result)
        {
            return result;
        }

        QueueContextLease queueContext{};
        result = m_queuePool.get_queue_context(CommandListType::Copy, queueContext);
        if (!result)
        {
            m_commandPool.return_command_context(commandContext);
            return result;
        }

        result = commandContext->reset();
        if (!result)
        {
            m_commandPool.return_command_context(commandContext);
            m_queuePool.return_queue_context(queueContext);
            return result;
        }

        auto* dx12CommandContext = static_cast<DX12GpuCommandContext*>(commandContext.get());
        ID3D12GraphicsCommandList* commandList = dx12CommandContext->d3d12_command_list();
        if (commandList == nullptr)
        {
            m_commandPool.return_command_context(commandContext);
            m_queuePool.return_queue_context(queueContext);
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Copy command list is null.");
        }

        std::vector<TransitionRecord> transitions{};
        transitions.reserve(a_regions.size());

        // 3) 各リージョンの実リソースを解決し、必要な barrier と CopyBufferRegion を記録する。
        for (const BufferUploadRegion& region : a_regions)
        {
            if (region.byteSize == 0)
            {
                result = Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Upload region byte size must be greater than 0.");
                break;
            }

            DX12GpuResource* srcResource = nullptr;
            result = resolve_upload_resource(
                region.srcBufferHandle,
                region.srcUploadResourceIndex,
                &srcResource);
            if (!result)
            {
                break;
            }

            DX12GpuResource* dstResource = nullptr;
            result = resolve_default_resource(
                region.dstBufferHandle,
                region.dstDefaultResourceIndex,
                &dstResource);
            if (!result)
            {
                break;
            }

            if (region.srcByteOffset + region.byteSize > srcResource->get_buffer_size())
            {
                result = Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Upload source range exceeds the source buffer size.");
                break;
            }
            if (region.dstByteOffset + region.byteSize > dstResource->get_buffer_size())
            {
                result = Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Upload destination range exceeds the destination buffer size.");
                break;
            }

            bool needsRestore = true;
            for (const TransitionRecord& transition : transitions)
            {
                if (transition.resource == dstResource)
                {
                    needsRestore = false;
                    break;
                }
            }
            if (needsRestore)
            {
                transitions.push_back({ dstResource, dstResource->get_current_d3d12_state()});
            }

            result = transition_resource(*commandList, *dstResource, D3D12_RESOURCE_STATE_COPY_DEST);
            if (!result)
            {
                break;
            }

            commandList->CopyBufferRegion(
                dstResource->get_resource(),
                region.dstByteOffset,
                srcResource->get_resource(),
                region.srcByteOffset,
                region.byteSize);
        }

        // 4) コピー後は元の状態へ戻してから close / submit / wait し、呼び出し側へ同期完了を返す。
        if (result)
        {
            for (const TransitionRecord& transition : transitions)
            {
                result = transition_resource(*commandList, *transition.resource, transition.oldState);
                if (!result)
                {
                    break;
                }
            }
        }

        if (result)
        {
            result = commandContext->close();
        }
        if (result)
        {
            std::vector<ICommandContext*> contexts{ commandContext.get() };
            result = queueContext->submit(contexts);
        }
        if (result)
        {
            result = queueContext->signal();
        }
        if (result)
        {
            result = queueContext->wait();
        }

        m_commandPool.return_command_context(commandContext);
        m_queuePool.return_queue_context(queueContext);
        return result;
    }

    Result ResourceUploader::resolve_upload_resource(
        BufferHandle a_handle,
        uint32_t a_resourceIndex,
        DX12GpuResource** a_outResource)
    {
        // 1) ハンドルを解決して upload 実体群へ辿り、MeshPool 側が扱う一時 staging を特定する。
        DX12BufferRecord* record = nullptr;
        *a_outResource = nullptr;
        if (!m_bufferManager.try_get_record(a_handle, &record) || record == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Source buffer record was not found.");
        }

        // 2) upload 実体の添字を検証し、CPU から書き込んだ staging だけをコピー元に許可する。
        if (a_resourceIndex >= record->uploadResources.size())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Source upload resource index is out of range.");
        }

        *a_outResource = &record->uploadResources[a_resourceIndex];
        return Result::ok();
    }

    Result ResourceUploader::resolve_default_resource(
        BufferHandle a_handle,
        uint32_t a_resourceIndex,
        DX12GpuResource** a_outResource)
    {
        // 1) ハンドルを解決して default 実体群へ辿り、GPU 常駐側の書き込み先を特定する。
        DX12BufferRecord* record = nullptr;
        *a_outResource = nullptr;
        if (!m_bufferManager.try_get_record(a_handle, &record) || record == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Destination buffer record was not found.");
        }

        // 2) default 実体の添字を検証し、存在しないバックバッファ番号へのコピーを防ぐ。
        if (a_resourceIndex >= record->defaultResources.size())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Destination default resource index is out of range.");
        }

        *a_outResource = &record->defaultResources[a_resourceIndex];
        return Result::ok();
    }

    Result ResourceUploader::transition_resource(
        ID3D12GraphicsCommandList& a_commandList,
        DX12GpuResource& a_resource,
        D3D12_RESOURCE_STATES a_newState)
    {
        // 1) 同一 state への遷移は無駄なので、既に目的 state なら何もしない。
        const D3D12_RESOURCE_STATES oldState = a_resource.get_current_d3d12_state();
        if (oldState == a_newState)
        {
            return Result::ok();
        }

        // 2) barrier を 1 本だけ積んで、コピー前後の state を明示的に揃える。
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = a_resource.get_resource();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = oldState;
        barrier.Transition.StateAfter = a_newState;
        a_commandList.ResourceBarrier(1, &barrier);

        // 3) state 追跡を更新して、次のアップロードでも正しい遷移元を使えるようにする。
        a_resource.set_current_state(a_newState);
        return Result::ok();
    }
}
