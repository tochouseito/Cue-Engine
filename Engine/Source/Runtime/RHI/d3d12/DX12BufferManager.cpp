#include "DX12BufferManager.h"

namespace Cue::RHI::DX12
{
    Result DX12BufferManager::create_buffer(const BufferDesc& desc, BufferHandle& out)
    {
        DX12BufferRecord record{};

        // --- 引数の検査 ---
        CUE_ASSERT_MSG(desc.defaultHeapCount + desc.uploadHeapCount > 0, "Buffer must have at least one heap.");
        CUE_ASSERT_MSG(desc.size > 0, "Buffer size must be greater than 0.");
        CUE_ASSERT_MSG(desc.stride > 0, "Buffer stride must be greater than 0.");
        CUE_ASSERT_MSG(desc.elementCount > 0, "Buffer element count must be greater than 0.");
        CUE_ASSERT_MSG(desc.alignment > 0, "Buffer alignment must be greater than 0.");
        CUE_ASSERT_MSG(desc.type != BufferType::Unknown, "Buffer type must be specified.");

        // デフォルトヒープバッファの作成
        for (uint32_t i = 0; i < desc.defaultHeapCount; ++i)
        {
            // バッファの初期化処理
            DX12GpuResource resource;
            D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
            D3D12_HEAP_PROPERTIES heapProperties = {};
            heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // デフォルトヒープは GPU 専用のヒープタイプ
            D3D12_RESOURCE_DESC resourceDesc = {};
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resourceDesc.Width = desc.size;
            resourceDesc.Height = 1;
            resourceDesc.DepthOrArraySize = 1;
            resourceDesc.MipLevels = 1;
            resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
            resourceDesc.SampleDesc = { 1, 0 };
            resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resourceDesc.Alignment = 0;
            resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            // リソース名の変換
            std::wstring name = L"";
            PAL::Win::utf8_to_wide(desc.name, &name);
            // 実リソース生成
            resource.create(
                *m_renderDevice.get_d3d12_device(),
                heapProperties,
                heapFlags,
                resourceDesc,
                convert_resource_state(desc.initialState),
                nullptr,
                name);
            // 成功したらレコードに追加
            record.resources.emplace_back(std::move(resource));
        }

        // アップロードヒープバッファの作成
        for (uint32_t i = 0; i < desc.uploadHeapCount; ++i)
        {
            // バッファの初期化処理
            DX12GpuResource resource;
            D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
            D3D12_HEAP_PROPERTIES heapProperties = {};
            heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD; // アップロードヒープは CPU アクセス可能なヒープタイプ
            D3D12_RESOURCE_DESC resourceDesc = {};
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resourceDesc.Width = desc.size;
            resourceDesc.Height = 1;
            resourceDesc.DepthOrArraySize = 1;
            resourceDesc.MipLevels = 1;
            resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
            resourceDesc.SampleDesc = { 1, 0 };
            resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resourceDesc.Alignment = 0;
            resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            // リソース名の変換
            std::wstring name = L"";
            PAL::Win::utf8_to_wide(desc.name, &name);
            // 実リソース生成
            resource.create(
                *m_renderDevice.get_d3d12_device(),
                heapProperties,
                heapFlags,
                resourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, // アップロードヒープは常に GENERIC_READ で作成
                nullptr,
                name);
            // 成功したらレコードに追加
            record.resources.emplace_back(std::move(resource));
        }

        return Result::ok();
    }
    Result DX12BufferManager::destroy_buffer(BufferHandle handle)
    {
        handle;
        return Result();
    }
}
