#pragma once
#include "stdafx.h"
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <cstring>
#include <vector>
#include <span>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <type_traits>

namespace Cue::GraphicsCore::DX12
{
    class GpuResource
    {
    public:
        GpuResource() = default;
        // コピー禁止
        GpuResource(const GpuResource&) = delete;
        GpuResource& operator=(const GpuResource&) = delete;
        // ムーブは許可
        GpuResource(GpuResource&&) = default;
        GpuResource& operator=(GpuResource&&) = default;
        // デストラクタ
        virtual ~GpuResource()
        {
            if (is_in_use())
            {
#ifdef CUE_DEBUG
                Assert::cue_assert(
                    false,
                    "GpuResource is still in use during destruction.");
#endif
            }
        }
        Result init(
            ComPtr<ID3D12Resource>&& resource,
            D3D12_RESOURCE_STATES initialState,
            std::wstring_view name)
        {
            // 1) 二重初期化を防ぐ
            if (m_resource)
            {
                return Result::fail(
                    Facility::D3D12,
                    Code::InvalidState,
                    Severity::Fatal,
                    false,
                    "GpuResource is already initialized.");
            }

            // 2) リソースを受け取る
            m_resource = std::move(resource);
            if (!m_resource)
            {
                return Result::fail(
                    Facility::D3D12,
                    Code::InvalidArg,
                    Severity::Fatal,
                    false,
                    "Invalid resource.");
            }

            // 3) 付随情報を初期化
            m_resourceDesc = m_resource->GetDesc();
            m_currentState = initialState;
            m_fence.Reset();
            m_fenceValue = 0;

            // 4) デバッグ名（任意）
            if (!name.empty())
            {
                std::wstring nameStr(name);
                m_resource->SetName(nameStr.c_str());
            }

            return Result::ok();
        }
        // 破棄
        virtual bool destroy()
        {
            if(is_in_use())
            {
                return false;
            }
            if (m_resource)
            {
                m_resource.Reset();
            }
            m_currentState = D3D12_RESOURCE_STATE_COMMON;
            m_resourceDesc = {};
            m_fence = nullptr;
            m_fenceValue = 0;
            return true;
        }
        // リソースの取得
        ID3D12Resource* operator->() { return m_resource.Get(); }
        const ID3D12Resource* operator->() const { return m_resource.Get(); }
        ID3D12Resource* get_resource() { return m_resource.Get(); }
        D3D12_RESOURCE_STATES get_use_state() const { return m_currentState; }
        void set_use_state(D3D12_RESOURCE_STATES state) { m_currentState = state; }
        const D3D12_RESOURCE_DESC& get_resource_desc() const { return m_resourceDesc; }
        void set_fence(ID3D12Fence* fence, uint64_t value)
        {
            m_fence = fence;
            m_fenceValue = value;
        }
        // リソースが使用中かどうか
        bool is_in_use() const
        {
            // フェンスが設定されていれば、完了値と比較して使用中かどうかを判断
            if(m_fence)
            {
                return m_fence->GetCompletedValue() < m_fenceValue;
            }
            return false;
        }
    protected:
        Result create_resource(
            ID3D12Device& device,
            const D3D12_HEAP_PROPERTIES& heapProperties,
            D3D12_HEAP_FLAGS heapFlags,
            const D3D12_RESOURCE_DESC& desc,
            D3D12_RESOURCE_STATES initialState,
            const D3D12_CLEAR_VALUE* clearValue,
            std::wstring_view name)
        {
            // リソースの作成
            HRESULT hr = device.CreateCommittedResource(
                &heapProperties,
                heapFlags,
                &desc,
                initialState,
                clearValue,
                IID_PPV_ARGS(&m_resource));
            if (FAILED(hr))
            {
                return Result::fail(
                    Facility::GraphicsCore,
                    Code::CreationFailed,
                    Severity::Error,
                    static_cast<uint32_t>(hr),
                    "Failed to create D3D12 resource.");
            }
            // リソース名の設定
            if (name.size() > 0)
            {
                std::wstring nameStr(name);
                m_resource->SetName(nameStr.c_str());
            }
            // メンバ変数の設定
            m_currentState = initialState;
            m_resourceDesc = desc;
            return Result::ok();
        }
    private:
        ComPtr<ID3D12Resource> m_resource = nullptr;
        D3D12_RESOURCE_STATES m_currentState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_DESC m_resourceDesc{};
        ComPtr<ID3D12Fence> m_fence = nullptr;
        uint64_t m_fenceValue = 0;
    };

    struct BufferCreateDesc
    {
        UINT64 byteSize = 0;
        UINT   numElements = 0;
        UINT   stride = 0; // structured のとき使う。raw/byte は 0 でよい。

        D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

        std::wstring_view name;
    };

    class GpuBufferResource : public GpuResource
    {
    public:
        // デフォルトコンストラクタ
        GpuBufferResource() = default;
        // コピー禁止
        GpuBufferResource(const GpuBufferResource&) = delete;
        GpuBufferResource& operator=(const GpuBufferResource&) = delete;
        // ムーブは許可
        GpuBufferResource(GpuBufferResource&&) noexcept = default;
        GpuBufferResource& operator=(GpuBufferResource&&) noexcept = default;
        // デストラクタ
        ~GpuBufferResource() override = default;
    public:
        Result create_buffer(ID3D12Device& device, const BufferCreateDesc& desc)
        {
            // バイトサイズの整合性チェック
            UINT64 byteSize = desc.byteSize;
            m_numElements = desc.numElements;
            m_stride = desc.stride;
            // 0 の場合は要素数とストライドから計算
            if (byteSize == 0)
            {
                if ((desc.numElements == 0) || (desc.stride == 0))
                {
                    return Result::fail(
                        Facility::GraphicsCore,
                        Code::InvalidArg,
                        Severity::Error,
                        false,
                        "BufferCreateDesc size is invalid.");
                }
                byteSize = static_cast<UINT64>(desc.numElements) * static_cast<UINT64>(desc.stride);
            }
            else// サイズが指定されている場合は、要素数とストライドの積と一致するか確認
            {
                if ((desc.numElements != 0) && (desc.stride != 0))
                {
                    const UINT64 calc = static_cast<UINT64>(desc.numElements) * static_cast<UINT64>(desc.stride);
                    if (calc != byteSize)
                    {
                        return Result::fail(
                            Facility::GraphicsCore,
                            Code::InvalidArg,
                            Severity::Error,
                            false,
                            "BufferCreateDesc size mismatch.");
                    }
                }
            }

            D3D12_RESOURCE_DESC resourceDesc = {};
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resourceDesc.Width = desc.byteSize;
            resourceDesc.Height = 1;
            resourceDesc.DepthOrArraySize = 1;
            resourceDesc.MipLevels = 1;
            resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
            resourceDesc.SampleDesc = { 1, 0 };
            resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resourceDesc.Alignment = 0;
            resourceDesc.Flags = desc.flags;
            D3D12_HEAP_PROPERTIES heapProperties = {};
            heapProperties.Type = desc.heapType;
            return create_resource(
                device,
                heapProperties,
                D3D12_HEAP_FLAG_NONE,
                resourceDesc,
                desc.initialState,
                nullptr,
                desc.name);
        }
        // 要素数の取得（StructuredBuffer 用）
        UINT get_num_elements() const { return m_numElements; }
        // ストライドの取得（StructuredBuffer 用）
        UINT get_stride() const { return m_stride; }
        // バイトサイズの取得（RawBuffer 用）
        UINT64 get_byte_size() const { return get_resource_desc().Width; }
        Result map_upload_buffer(std::byte*& outMappedData)
        {
            // 1) upload heap は永続 map して SlotUploader から毎フレーム再利用できる形にする。
            outMappedData = nullptr;
            ID3D12Resource* resource = get_resource();
            if (resource == nullptr)
            {
                return Result::fail(
                    Facility::GraphicsCore,
                    Code::InvalidState,
                    Severity::Error,
                    false,
                    "Buffer resource is null.");
            }

            D3D12_HEAP_PROPERTIES actualHeapProperties{};
            D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
            const HRESULT heapResult = resource->GetHeapProperties(&actualHeapProperties, &heapFlags);
            if (FAILED(heapResult))
            {
                return Result::fail(
                    Facility::GraphicsCore,
                    Code::InvalidState,
                    Severity::Error,
                    static_cast<uint32_t>(heapResult),
                    "Failed to query heap properties.");
            }
            if (actualHeapProperties.Type != D3D12_HEAP_TYPE_UPLOAD)
            {
                return Result::fail(
                    Facility::GraphicsCore,
                    Code::InvalidArg,
                    Severity::Error,
                    false,
                    "Buffer is not created on upload heap.");
            }

            if (m_mappedData == nullptr)
            {
                void* mapped = nullptr;
                const D3D12_RANGE readRange{ 0, 0 };
                const HRESULT mapResult = resource->Map(0, &readRange, &mapped);
                if (FAILED(mapResult) || mapped == nullptr)
                {
                    return Result::fail(
                        Facility::GraphicsCore,
                        Code::InvalidState,
                        Severity::Error,
                        static_cast<uint32_t>(mapResult),
                        "Failed to map upload buffer.");
                }

                m_mappedData = static_cast<std::byte*>(mapped);
            }

            outMappedData = m_mappedData;
            return Result::ok();
        }
        bool destroy() override
        {
            // 1) 永続 map を先に閉じ、destroy 時に upload buffer の生ポインタが残らないようにする。
            if (m_mappedData != nullptr)
            {
                ID3D12Resource* resource = get_resource();
                if (resource != nullptr)
                {
                    const D3D12_RANGE writtenRange{ 0, 0 };
                    resource->Unmap(0, &writtenRange);
                }
                m_mappedData = nullptr;
            }

            return GpuResource::destroy();
        }
    private:
        UINT m_numElements = 0;
        UINT m_stride = 0;
        std::byte* m_mappedData = nullptr;
    };

    class GpuTextureResource : public GpuResource
    {
    public:
        // デフォルトコンストラクタ
        GpuTextureResource() = default;
        // コピー禁止
        GpuTextureResource(const GpuTextureResource&) = delete;
        GpuTextureResource& operator=(const GpuTextureResource&) = delete;
        // ムーブは許可
        GpuTextureResource(GpuTextureResource&&) noexcept = default;
        GpuTextureResource& operator=(GpuTextureResource&&) noexcept = default;
        // デストラクタ
        ~GpuTextureResource() override = default;

        Result create_texture(ID3D12Device& device, D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, std::wstring_view name)
        {
            D3D12_HEAP_PROPERTIES heapProperties = {};
            heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
            return create_resource(
                device,
                heapProperties,
                D3D12_HEAP_FLAG_NONE,
                desc,
                initialState,
                clearValue,
                name);
        }
        Result create_depth_stencil_texture_2d(
            ID3D12Device& device,
            uint32_t width,
            uint32_t height,
            DXGI_FORMAT format,
            D3D12_RESOURCE_STATES initialState,
            float clearDepth,
            uint8_t clearStencil,
            std::wstring_view name)
        {
            // 1) 深度テクスチャは 2D 固定で作り、DSV/clear の前提を resource 生成時に確定する。
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Alignment = 0;
            desc.Width = width;
            desc.Height = height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = format;
            desc.SampleDesc = { 1, 0 };
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            // 2) 既定クリア値を埋めておくことで、ClearDepthStencilView と最適化ヒントを一致させる。
            D3D12_CLEAR_VALUE clearValue{};
            clearValue.Format = format;
            clearValue.DepthStencil.Depth = clearDepth;
            clearValue.DepthStencil.Stencil = clearStencil;

            return create_texture(device, desc, initialState, &clearValue, name);
        }

        Result create_render_target_texture_2d(
            ID3D12Device& device,
            uint32_t width,
            uint32_t height,
            DXGI_FORMAT format,
            D3D12_RESOURCE_STATES initialState,
            const float clearColor[4],
            std::wstring_view name)
        {
            // 1) カラー render target は 2D 固定で確保し、RTV/SRV 両方の view 作成元として使う。
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Alignment = 0;
            desc.Width = width;
            desc.Height = height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = format;
            desc.SampleDesc = { 1, 0 };
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

            // 2) 既定 clear 値を resource 作成時にも渡し、ClearRenderTargetView の最適化条件と一致させる。
            D3D12_CLEAR_VALUE clearValue{};
            clearValue.Format = format;
            clearValue.Color[0] = clearColor[0];
            clearValue.Color[1] = clearColor[1];
            clearValue.Color[2] = clearColor[2];
            clearValue.Color[3] = clearColor[3];

            return create_texture(device, desc, initialState, &clearValue, name);
        }
    private:

    };

    

} // namespace Cue::GraphicsCore::DX12


