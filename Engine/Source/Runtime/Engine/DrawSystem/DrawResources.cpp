#include "DrawResources.h"

// === Engine includes ===
#include "DrawFrameState.h"
#include "DrawScene.h"

// === C++ includes ===
#include <limits>

namespace Cue::DrawSystem
{
    namespace
    {
        template <typename T>
        Result upload_slots(RHI::SlotUploader<T>& a_uploader, const std::vector<T>& a_values,
                            const char* a_errorMessage)
        {
            a_uploader.begin_frame();

            for (uint32_t slotIndex = 0; slotIndex < static_cast<uint32_t>(a_values.size()); ++slotIndex)
            {
                if (!a_uploader.push(slotIndex, a_values[slotIndex]))
                {
                    return Result::fail(Code::InvalidState, Severity::Error, a_errorMessage);
                }
            }

            if (!a_uploader.commit())
            {
                return Result::fail(Code::InvalidState, Severity::Error, a_errorMessage);
            }

            return Result::ok();
        }

        template <typename T>
        Result upload_single_slot(RHI::SlotUploader<T>& a_uploader, const T& a_value, const char* a_errorMessage)
        {
            a_uploader.begin_frame();
            if (!a_uploader.push(0, a_value))
            {
                return Result::fail(Code::InvalidState, Severity::Error, a_errorMessage);
            }
            if (!a_uploader.commit())
            {
                return Result::fail(Code::InvalidState, Severity::Error, a_errorMessage);
            }

            return Result::ok();
        }
    } // namespace

    Result DrawResources::create_renderable_info_buffer(const uint32_t a_maxObjectCount)
    {
        // RenderableInfoBuffer の設定
        // - 各描画対象の mesh/material/transform 参照情報を GPU から読む structured buffer として作成する
        RHI::BufferDesc renderableInfoBufferDesc{};
        renderableInfoBufferDesc.name = "RenderableInfoBuffer";
        renderableInfoBufferDesc.type = RHI::BufferType::Structured;
        renderableInfoBufferDesc.defaultHeapCount = 1;
        renderableInfoBufferDesc.uploadHeapCount = m_bufferCount;
        renderableInfoBufferDesc.initialState = RHI::ResourceState::ShaderResource;
        renderableInfoBufferDesc.stride = sizeof(GpuData::RenderableInfo);
        renderableInfoBufferDesc.elementCount = a_maxObjectCount;
        renderableInfoBufferDesc.size = renderableInfoBufferDesc.stride * renderableInfoBufferDesc.elementCount;
        renderableInfoBufferDesc.alignment = alignof(GpuData::RenderableInfo);

        // RenderableInfoBuffer の作成
        // - handle は DrawResourceType の添字で保持し、後続 pass から取得できるようにする
        RHI::BufferHandle& renderableInfoBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::RenderableInfoBuffer)];
        Result result = m_bufferManager->create_buffer(renderableInfoBufferDesc, renderableInfoBufferHandle);
        if (!result)
        {
            return result;
        }

        // RenderableInfoBuffer の uploader 作成
        // - フレームごとに CPU から更新できるよう、bufferCount 分の SlotUploader を作る
        result = m_bufferManager->create_slot_uploaders(
            m_bufferHandles[static_cast<size_t>(DrawResourceType::RenderableInfoBuffer)], m_bufferCount,
            m_renderableInfoUploaders);
        if (!result)
        {
            return result;
        }
        if (m_renderableInfoUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal, "RenderableInfoBuffer uploader was not created.");
        }

        // RenderableInfoBuffer の SRV 作成
        // - shader から objectId を添字にして RenderableInfo を読むための SRV を作る
        RHI::ViewDesc renderableInfoBufferSrvDesc{};
        renderableInfoBufferSrvDesc.name = "RenderableInfoBufferSRV";
        renderableInfoBufferSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
        renderableInfoBufferSrvDesc.bufferKind = RHI::BufferKind::Buffer;
        renderableInfoBufferSrvDesc.bufferHandle = renderableInfoBufferHandle;
        renderableInfoBufferSrvDesc.firstElement = 0;
        renderableInfoBufferSrvDesc.numElements = renderableInfoBufferDesc.elementCount;
        renderableInfoBufferSrvDesc.structureByteStride = renderableInfoBufferDesc.stride;

        // ビューの作成
        RHI::ViewHandle& renderableInfoBufferSrvHandle =
            m_viewHandles[static_cast<size_t>(DrawResourceType::RenderableInfoBuffer)];
        result = m_viewManager->create_view(renderableInfoBufferSrvDesc, renderableInfoBufferSrvHandle);
        if (!result)
        {
            return result;
        }

        m_maxRenderableInfoCount = a_maxObjectCount;
        return Result::ok();
    }

    Result DrawResources::create_transform_buffer(const uint32_t a_maxObjectCount)
    {
        // TransformBuffer の設定
        // - オブジェクトごとの world / normal matrix を GPU から読む structured buffer として作成する
        RHI::BufferDesc transformBufferDesc{};
        transformBufferDesc.name = "TransformBuffer";
        transformBufferDesc.type = RHI::BufferType::Structured;
        transformBufferDesc.defaultHeapCount = 1;
        transformBufferDesc.uploadHeapCount = m_bufferCount;
        transformBufferDesc.initialState = RHI::ResourceState::ShaderResource;
        transformBufferDesc.stride = sizeof(GpuData::ObjectTransformGpu);
        transformBufferDesc.elementCount = a_maxObjectCount;
        transformBufferDesc.size = transformBufferDesc.stride * transformBufferDesc.elementCount;
        transformBufferDesc.alignment = alignof(GpuData::ObjectTransformGpu);
        RHI::BufferHandle& transformBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::TransformBuffer)];

        // TransformBuffer の作成
        // - RenderableInfo の transformId から参照される buffer handle を保存する
        Result result = m_bufferManager->create_buffer(transformBufferDesc, transformBufferHandle);
        if (!result)
        {
            return result;
        }

        // TransformBuffer の uploader 作成
        // - transform はフレームごとに変わるため、各 frame resource 用の uploader を用意する
        result = m_bufferManager->create_slot_uploaders(transformBufferHandle, m_bufferCount, m_transformUploaders);
        if (!result)
        {
            return result;
        }
        if (m_transformUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal, "TransformBuffer uploader was not created.");
        }

        // TransformBuffer の SRV 作成
        // - vertex/compute shader から transform 配列として参照する
        RHI::ViewDesc transformBufferSrvDesc{};
        transformBufferSrvDesc.name = "TransformBufferSRV";
        transformBufferSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
        transformBufferSrvDesc.bufferKind = RHI::BufferKind::Buffer;
        transformBufferSrvDesc.bufferHandle = transformBufferHandle;
        transformBufferSrvDesc.firstElement = 0;
        transformBufferSrvDesc.numElements = transformBufferDesc.elementCount;
        transformBufferSrvDesc.structureByteStride = transformBufferDesc.stride;

        // ビューの作成
        RHI::ViewHandle& transformBufferSrvHandle =
            m_viewHandles[static_cast<size_t>(DrawResourceType::TransformBuffer)];
        result = m_viewManager->create_view(transformBufferSrvDesc, transformBufferSrvHandle);
        if (!result)
        {
            return result;
        }

        m_maxTransformCount = a_maxObjectCount;
        return Result::ok();
    }

    Result DrawResources::upload_draw_scene(uint32_t a_bufferIndex, const DrawScene& a_scene,
                                            DrawFrameData& a_frameData)
    {
        a_frameData.objectCount = 0;

        if (a_bufferIndex >= m_bufferCount)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "DrawResources buffer index is out of range.");
        }

        if (m_renderableInfoUploaders.size() != m_bufferCount || m_transformUploaders.size() != m_bufferCount ||
            m_staticMeshIndirectCommandUploaders.size() != m_bufferCount ||
            m_staticMeshIndirectCommandCountUploaders.size() != m_bufferCount ||
            m_staticMeshObjectIndexUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "DrawResources uploaders are not initialized.");
        }

        const size_t objectCount = a_scene.object_count();
        const std::vector<GpuData::RenderableInfo>& renderableInfos = a_scene.renderable_infos();
        const std::vector<GpuData::ObjectTransformGpu>& transforms = a_scene.transforms();

        // DrawScene は 3 つの配列を同じ index で対応させる契約。
        if (renderableInfos.size() != objectCount || transforms.size() != objectCount)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "DrawScene arrays are not aligned.");
        }

        if (objectCount > m_maxRenderableInfoCount)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "RenderableInfoBuffer capacity is too small.");
        }

        if (objectCount > m_maxTransformCount)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "TransformBuffer capacity is too small.");
        }

        if (objectCount > (std::numeric_limits<uint32_t>::max)())
        {
            return Result::fail(Code::InvalidState, Severity::Error, "DrawScene object count exceeds uint32_t range.");
        }

        if (a_frameData.staticMeshIndirectCommands.size() > m_maxStaticMeshBatchCount)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "StaticMesh indirect command buffer capacity is too small.");
        }

        if (a_frameData.staticMeshObjectIndices.size() > m_maxStaticMeshObjectIndexCount)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "StaticMesh object index buffer capacity is too small.");
        }

        if (a_frameData.staticMeshBatches.size() != a_frameData.staticMeshIndirectCommands.size())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "StaticMesh batch and command arrays are not aligned.");
        }

        if (a_frameData.staticMeshBatchCount != static_cast<uint32_t>(a_frameData.staticMeshBatches.size()) ||
            a_frameData.indirectCommandCount != static_cast<uint32_t>(a_frameData.staticMeshIndirectCommands.size()))
        {
            return Result::fail(Code::InvalidState, Severity::Error, "StaticMesh frame counts are not aligned.");
        }

        Result result = upload_slots(m_renderableInfoUploaders[a_bufferIndex], renderableInfos,
                                     "Failed to upload RenderableInfoBuffer.");
        if (!result)
        {
            return result;
        }

        result = upload_slots(m_transformUploaders[a_bufferIndex], transforms, "Failed to upload TransformBuffer.");
        if (!result)
        {
            return result;
        }

        result = upload_slots(m_staticMeshIndirectCommandUploaders[a_bufferIndex],
                              a_frameData.staticMeshIndirectCommands,
                              "Failed to upload StaticMeshIndirectCommandBuffer.");
        if (!result)
        {
            return result;
        }

        result = upload_slots(m_staticMeshObjectIndexUploaders[a_bufferIndex],
                              a_frameData.staticMeshObjectIndices,
                              "Failed to upload StaticMeshObjectIndexBuffer.");
        if (!result)
        {
            return result;
        }

        result = upload_single_slot(m_staticMeshIndirectCommandCountUploaders[a_bufferIndex],
                                    a_frameData.indirectCommandCount,
                                    "Failed to upload StaticMeshIndirectCommandCountBuffer.");
        if (!result)
        {
            return result;
        }

        a_frameData.objectCount = static_cast<uint32_t>(objectCount);
        return Result::ok();
    }

    Result DrawResources::create_view_projection_buffer()
    {
        // - constant buffer は D3D12 の CBV 要件に合わせて 256 byte alignment にする
        constexpr uint32_t k_constantBufferAlignment = 256;

        // - カメラ行列は 1 要素だけの constant buffer として持つ
        RHI::BufferDesc viewProjectionBufferDesc{};
        viewProjectionBufferDesc.name = "ViewProjectionBuffer";
        viewProjectionBufferDesc.type = RHI::BufferType::Constant;
        viewProjectionBufferDesc.defaultHeapCount = 1;
        viewProjectionBufferDesc.uploadHeapCount = m_bufferCount;
        viewProjectionBufferDesc.initialState = RHI::ResourceState::Common;
        viewProjectionBufferDesc.stride = sizeof(GpuData::ViewProjectionGpu);
        viewProjectionBufferDesc.elementCount = 1;
        viewProjectionBufferDesc.size = viewProjectionBufferDesc.stride * viewProjectionBufferDesc.elementCount;
        viewProjectionBufferDesc.alignment = k_constantBufferAlignment;

        RHI::BufferHandle& viewProjectionBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::ViewProjectionBuffer)];
        // - 描画 pass から CBV として bind できる buffer handle を保存する
        Result result = m_bufferManager->create_buffer(viewProjectionBufferDesc, viewProjectionBufferHandle);
        if (!result)
        {
            return result;
        }

        // - カメラ行列もフレームごとに更新するため、frame resource 数分の uploader を作る
        result = m_bufferManager->create_slot_uploaders(viewProjectionBufferHandle, m_bufferCount,
                                                        m_viewProjectionUploaders);
        if (!result)
        {
            return result;
        }
        if (m_viewProjectionUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal, "ViewProjectionBuffer uploader was not created.");
        }

        return Result::ok();
    }

    std::vector<RHI::SlotUploader<GpuData::IndirectCommand>>&
    DrawResources::static_mesh_indirect_command_uploaders() noexcept
    {
        return m_staticMeshIndirectCommandUploaders;
    }

    std::vector<RHI::SlotUploader<uint32_t>>&
    DrawResources::static_mesh_indirect_command_count_uploaders() noexcept
    {
        return m_staticMeshIndirectCommandCountUploaders;
    }

    std::vector<RHI::SlotUploader<uint32_t>>& DrawResources::static_mesh_object_index_uploaders() noexcept
    {
        return m_staticMeshObjectIndexUploaders;
    }

    RHI::BufferHandle DrawResources::static_mesh_indirect_command_buffer_handle() const noexcept
    {
        return m_bufferHandles[static_cast<size_t>(DrawResourceType::StaticMeshIndirectCommandBuffer)];
    }

    RHI::BufferHandle DrawResources::static_mesh_indirect_command_count_buffer_handle() const noexcept
    {
        return m_bufferHandles[static_cast<size_t>(DrawResourceType::StaticMeshIndirectCommandCountBuffer)];
    }

    RHI::BufferHandle DrawResources::static_mesh_object_index_buffer_handle() const noexcept
    {
        return m_bufferHandles[static_cast<size_t>(DrawResourceType::StaticMeshObjectIndexBuffer)];
    }

    RHI::ViewHandle DrawResources::static_mesh_object_index_buffer_srv_handle() const noexcept
    {
        return m_viewHandles[static_cast<size_t>(DrawResourceType::StaticMeshObjectIndexBuffer)];
    }

    Result DrawResources::create_material_buffer(const uint32_t a_maxMaterialCount)
    {
        // - materialId から参照されるマテリアルパラメータ配列を structured buffer として作成する
        RHI::BufferDesc materialBufferDesc{};
        materialBufferDesc.name = "MaterialBuffer";
        materialBufferDesc.type = RHI::BufferType::Structured;
        materialBufferDesc.defaultHeapCount = 1;
        materialBufferDesc.uploadHeapCount = m_bufferCount;
        materialBufferDesc.initialState = RHI::ResourceState::ShaderResource;
        materialBufferDesc.stride = sizeof(GpuData::MaterialGpu);
        materialBufferDesc.elementCount = a_maxMaterialCount;
        materialBufferDesc.size = materialBufferDesc.stride * materialBufferDesc.elementCount;
        materialBufferDesc.alignment = alignof(GpuData::MaterialGpu);

        RHI::BufferHandle& materialBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::MaterialBuffer)];
        // - Material buffer の GPU 実体を作成し、共通 handle 配列へ格納する
        Result result = m_bufferManager->create_buffer(materialBufferDesc, materialBufferHandle);
        if (!result)
        {
            return result;
        }

        // - MaterialGpu の更新用 uploader を frame resource 数分作る
        result = m_bufferManager->create_slot_uploaders(materialBufferHandle, m_bufferCount, m_materialUploaders);
        if (!result)
        {
            return result;
        }
        if (m_materialUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal, "MaterialBuffer uploader was not created.");
        }

        // - pixel shader などから materialId で読むための SRV を作成する
        RHI::ViewDesc materialBufferSrvDesc{};
        materialBufferSrvDesc.name = "MaterialBufferSRV";
        materialBufferSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
        materialBufferSrvDesc.bufferKind = RHI::BufferKind::Buffer;
        materialBufferSrvDesc.bufferHandle = materialBufferHandle;
        materialBufferSrvDesc.firstElement = 0;
        materialBufferSrvDesc.numElements = materialBufferDesc.elementCount;
        materialBufferSrvDesc.structureByteStride = materialBufferDesc.stride;

        RHI::ViewHandle& materialBufferSrvHandle = m_viewHandles[static_cast<size_t>(DrawResourceType::MaterialBuffer)];
        result = m_viewManager->create_view(materialBufferSrvDesc, materialBufferSrvHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result DrawResources::create_render_cell_buffer(const uint32_t a_maxCellCount)
    {
        // - セル単位の bounds と object range を GPU culling から読む structured buffer として作成する
        RHI::BufferDesc renderCellBufferDesc{};
        renderCellBufferDesc.name = "RenderCellBuffer";
        renderCellBufferDesc.type = RHI::BufferType::Structured;
        renderCellBufferDesc.defaultHeapCount = 1;
        renderCellBufferDesc.uploadHeapCount = m_bufferCount;
        renderCellBufferDesc.initialState = RHI::ResourceState::ShaderResource;
        renderCellBufferDesc.stride = sizeof(GpuData::RenderCellGpu);
        renderCellBufferDesc.elementCount = a_maxCellCount;
        renderCellBufferDesc.size = renderCellBufferDesc.stride * renderCellBufferDesc.elementCount;
        renderCellBufferDesc.alignment = alignof(GpuData::RenderCellGpu);

        RHI::BufferHandle& renderCellBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::RenderCellBuffer)];
        // - Cell culling pass が参照する RenderCell buffer handle を保存する
        Result result = m_bufferManager->create_buffer(renderCellBufferDesc, renderCellBufferHandle);
        if (!result)
        {
            return result;
        }

        // - セル情報は scene setup 後にアップロードされるため、更新用 uploader を用意する
        result = m_bufferManager->create_slot_uploaders(renderCellBufferHandle, m_bufferCount, m_renderCellUploaders);
        if (!result)
        {
            return result;
        }
        if (m_renderCellUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal, "RenderCellBuffer uploader was not created.");
        }

        // - compute shader から cellIndex で RenderCellGpu を読むための SRV を作る
        RHI::ViewDesc renderCellBufferSrvDesc{};
        renderCellBufferSrvDesc.name = "RenderCellBufferSRV";
        renderCellBufferSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
        renderCellBufferSrvDesc.bufferKind = RHI::BufferKind::Buffer;
        renderCellBufferSrvDesc.bufferHandle = renderCellBufferHandle;
        renderCellBufferSrvDesc.firstElement = 0;
        renderCellBufferSrvDesc.numElements = renderCellBufferDesc.elementCount;
        renderCellBufferSrvDesc.structureByteStride = renderCellBufferDesc.stride;

        RHI::ViewHandle& renderCellBufferSrvHandle =
            m_viewHandles[static_cast<size_t>(DrawResourceType::RenderCellBuffer)];
        result = m_viewManager->create_view(renderCellBufferSrvDesc, renderCellBufferSrvHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result DrawResources::create_render_object_buffer(const uint32_t a_maxObjectCount)
    {
        // RenderObjectBuffer の設定
        // - GPU culling 後の可視オブジェクトを書き込むため、UAV buffer として作成する
        RHI::BufferDesc renderObjectBufferDesc{};
        renderObjectBufferDesc.name = "RenderObjectBuffer";
        renderObjectBufferDesc.type = RHI::BufferType::UnorderedAccess;
        renderObjectBufferDesc.defaultHeapCount = 1;
        renderObjectBufferDesc.uploadHeapCount = m_bufferCount;
        renderObjectBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
        renderObjectBufferDesc.stride = sizeof(GpuData::RenderObject);
        renderObjectBufferDesc.elementCount = a_maxObjectCount;
        renderObjectBufferDesc.size = renderObjectBufferDesc.stride * renderObjectBufferDesc.elementCount;
        renderObjectBufferDesc.alignment = alignof(GpuData::RenderObject);

        // RenderObjectBuffer の作成
        // - 後続の batching / forward pass が参照する output buffer handle を保存する
        RHI::BufferHandle& renderObjectBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::RenderObjectBuffer)];
        Result result = m_bufferManager->create_buffer(renderObjectBufferDesc, renderObjectBufferHandle);
        if (!result)
        {
            return result;
        }

        // - CPU から初期値やフレームごとの RenderObject を書き込めるよう uploader も作成しておく
        result =
            m_bufferManager->create_slot_uploaders(renderObjectBufferHandle, m_bufferCount, m_renderObjectUploaders);
        if (!result)
        {
            return result;
        }
        if (m_renderObjectUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal, "RenderObjectBuffer uploader was not created.");
        }

        // RenderObjectBuffer の UAV 作成
        // - compute shader が可視 RenderObject を書き込むための UAV を作る
        RHI::ViewDesc renderObjectBufferUavDesc{};
        renderObjectBufferUavDesc.name = "RenderObjectBufferUAV";
        renderObjectBufferUavDesc.type = RHI::ViewType::UnorderedAccessBuffer;
        renderObjectBufferUavDesc.bufferKind = RHI::BufferKind::Buffer;
        renderObjectBufferUavDesc.bufferHandle = renderObjectBufferHandle;
        renderObjectBufferUavDesc.firstElement = 0;
        renderObjectBufferUavDesc.numElements = renderObjectBufferDesc.elementCount;
        renderObjectBufferUavDesc.structureByteStride = renderObjectBufferDesc.stride;

        // ビューの作成
        RHI::ViewHandle& renderObjectBufferUavHandle =
            m_viewHandles[static_cast<size_t>(DrawResourceType::RenderObjectBuffer)];
        result = m_viewManager->create_view(renderObjectBufferUavDesc, renderObjectBufferUavHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result DrawResources::create_object_count_buffer()
    {
        // ObjectCountBuffer の設定
        // - 可視オブジェクト数を GPU 側で atomic 更新する raw UAV buffer として作成する
        RHI::BufferDesc renderObjectCountBufferDesc{};
        renderObjectCountBufferDesc.name = "VisibleObjectCountBuffer";
        renderObjectCountBufferDesc.type = RHI::BufferType::Raw;
        renderObjectCountBufferDesc.defaultHeapCount = 1;
        renderObjectCountBufferDesc.uploadHeapCount = m_bufferCount;
        renderObjectCountBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
        renderObjectCountBufferDesc.stride = sizeof(uint32_t);
        renderObjectCountBufferDesc.elementCount = 1;
        renderObjectCountBufferDesc.size = sizeof(uint32_t);
        renderObjectCountBufferDesc.alignment = alignof(uint32_t);

        // ObjectCountBuffer の作成
        // - RenderObjectBuffer への書き込み数を共有する counter buffer handle を保存する
        RHI::BufferHandle& renderObjectCountBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::VisibleObjectCountBuffer)];
        Result result = m_bufferManager->create_buffer(renderObjectCountBufferDesc, renderObjectCountBufferHandle);
        if (!result)
        {
            return result;
        }

        // - フレーム開始時の初期値アップロードなどに使う uploader を作成する
        result = m_bufferManager->create_slot_uploaders(renderObjectCountBufferHandle, m_bufferCount,
                                                        m_visibleObjectCountUploaders);
        if (!result)
        {
            return result;
        }
        if (m_visibleObjectCountUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal,
                                "VisibleObjectCountBuffer uploader was not created.");
        }

        // ObjectCountBuffer の UAV 作成
        // - compute shader が raw uint counter として読み書きするための UAV を作る
        RHI::ViewDesc renderObjectCountBufferUavDesc{};
        renderObjectCountBufferUavDesc.name = "VisibleObjectCountBufferUAV";
        renderObjectCountBufferUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
        renderObjectCountBufferUavDesc.bufferKind = RHI::BufferKind::Buffer;
        renderObjectCountBufferUavDesc.bufferHandle = renderObjectCountBufferHandle;
        renderObjectCountBufferUavDesc.firstElement = 0;
        renderObjectCountBufferUavDesc.numElements = renderObjectCountBufferDesc.size / sizeof(uint32_t);

        // ビューの作成
        RHI::ViewHandle& renderObjectCountBufferUavHandle =
            m_viewHandles[static_cast<size_t>(DrawResourceType::VisibleObjectCountBuffer)];
        result = m_viewManager->create_view(renderObjectCountBufferUavDesc, renderObjectCountBufferUavHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result DrawResources::create_static_mesh_batch_buffers(uint32_t a_maxBatchCount, uint32_t a_maxObjectIndexCount)
    {
        if (a_maxBatchCount == 0 || a_maxObjectIndexCount == 0)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "StaticMesh batch buffer capacity must be greater than zero.");
        }

        // StaticMeshIndirectCommandBuffer は ExecuteIndirect の引数列として使う。
        RHI::BufferDesc commandBufferDesc{};
        commandBufferDesc.name = "StaticMeshIndirectCommandBuffer";
        commandBufferDesc.type = RHI::BufferType::Structured;
        commandBufferDesc.defaultHeapCount = 0;
        commandBufferDesc.uploadHeapCount = m_bufferCount;
        commandBufferDesc.initialState = RHI::ResourceState::IndirectArgument;
        commandBufferDesc.stride = sizeof(GpuData::IndirectCommand);
        commandBufferDesc.elementCount = a_maxBatchCount;
        commandBufferDesc.size = commandBufferDesc.stride * commandBufferDesc.elementCount;
        commandBufferDesc.alignment = alignof(GpuData::IndirectCommand);

        RHI::BufferHandle& commandBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::StaticMeshIndirectCommandBuffer)];
        Result result = m_bufferManager->create_buffer(commandBufferDesc, commandBufferHandle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            commandBufferHandle,
            m_bufferCount,
            m_staticMeshIndirectCommandUploaders);
        if (!result)
        {
            return result;
        }
        if (m_staticMeshIndirectCommandUploaders.size() != m_bufferCount)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "StaticMeshIndirectCommandBuffer uploader was not created.");
        }

        // ExecuteIndirect の command count は 1 要素の uint buffer として毎フレーム更新する。
        RHI::BufferDesc commandCountBufferDesc{};
        commandCountBufferDesc.name = "StaticMeshIndirectCommandCountBuffer";
        commandCountBufferDesc.type = RHI::BufferType::Raw;
        commandCountBufferDesc.defaultHeapCount = 0;
        commandCountBufferDesc.uploadHeapCount = m_bufferCount;
        commandCountBufferDesc.initialState = RHI::ResourceState::IndirectArgument;
        commandCountBufferDesc.stride = sizeof(uint32_t);
        commandCountBufferDesc.elementCount = 1;
        commandCountBufferDesc.size = sizeof(uint32_t);
        commandCountBufferDesc.alignment = alignof(uint32_t);

        RHI::BufferHandle& commandCountBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::StaticMeshIndirectCommandCountBuffer)];
        result = m_bufferManager->create_buffer(commandCountBufferDesc, commandCountBufferHandle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            commandCountBufferHandle,
            m_bufferCount,
            m_staticMeshIndirectCommandCountUploaders);
        if (!result)
        {
            return result;
        }
        if (m_staticMeshIndirectCommandCountUploaders.size() != m_bufferCount)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "StaticMeshIndirectCommandCountBuffer uploader was not created.");
        }

        // object index buffer は indirect command の drawObjectStartIndex から参照する。
        RHI::BufferDesc objectIndexBufferDesc{};
        objectIndexBufferDesc.name = "StaticMeshObjectIndexBuffer";
        objectIndexBufferDesc.type = RHI::BufferType::Structured;
        objectIndexBufferDesc.defaultHeapCount = 0;
        objectIndexBufferDesc.uploadHeapCount = m_bufferCount;
        objectIndexBufferDesc.initialState = RHI::ResourceState::ShaderResource;
        objectIndexBufferDesc.stride = sizeof(uint32_t);
        objectIndexBufferDesc.elementCount = a_maxObjectIndexCount;
        objectIndexBufferDesc.size = objectIndexBufferDesc.stride * objectIndexBufferDesc.elementCount;
        objectIndexBufferDesc.alignment = alignof(uint32_t);

        RHI::BufferHandle& objectIndexBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::StaticMeshObjectIndexBuffer)];
        result = m_bufferManager->create_buffer(objectIndexBufferDesc, objectIndexBufferHandle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            objectIndexBufferHandle,
            m_bufferCount,
            m_staticMeshObjectIndexUploaders);
        if (!result)
        {
            return result;
        }
        if (m_staticMeshObjectIndexUploaders.size() != m_bufferCount)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "StaticMeshObjectIndexBuffer uploader was not created.");
        }

        RHI::ViewDesc objectIndexBufferSrvDesc{};
        objectIndexBufferSrvDesc.name = "StaticMeshObjectIndexBufferSRV";
        objectIndexBufferSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
        objectIndexBufferSrvDesc.bufferKind = RHI::BufferKind::Buffer;
        objectIndexBufferSrvDesc.bufferHandle = objectIndexBufferHandle;
        objectIndexBufferSrvDesc.firstElement = 0;
        objectIndexBufferSrvDesc.numElements = objectIndexBufferDesc.elementCount;
        objectIndexBufferSrvDesc.structureByteStride = objectIndexBufferDesc.stride;

        RHI::ViewHandle& objectIndexBufferSrvHandle =
            m_viewHandles[static_cast<size_t>(DrawResourceType::StaticMeshObjectIndexBuffer)];
        result = m_viewManager->create_view(objectIndexBufferSrvDesc, objectIndexBufferSrvHandle);
        if (!result)
        {
            return result;
        }

        m_maxStaticMeshBatchCount = a_maxBatchCount;
        m_maxStaticMeshObjectIndexCount = a_maxObjectIndexCount;
        return Result::ok();
    }
} // namespace Cue::DrawSystem
