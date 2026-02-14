#pragma once
#include "stdafx.h"

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
                Core::LogAssert::cue_assert(
                    false,
                    Core::LogSink::debugConsole,
                    "GpuResource is still in use during destruction.");
            }
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
                    Core::Facility::GraphicsCore,
                    Core::Code::CreationFailed,
                    Core::Severity::Error,
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
        GpuBufferResource() = default;
        ~GpuBufferResource() override = default;
    protected:
        Result create_buffer(ID3D12Device& device, const BufferCreateDesc& desc)
        {
            // バイトサイズの整合性チェック
            UINT64 byteSize = desc.byteSize;
            // 0 の場合は要素数とストライドから計算
            if (byteSize == 0)
            {
                if ((desc.numElements == 0) || (desc.stride == 0))
                {
                    return Result::fail(
                        Core::Facility::GraphicsCore,
                        Core::Code::InvalidArg,
                        Core::Severity::Error,
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
                            Core::Facility::GraphicsCore,
                            Core::Code::InvalidArg,
                            Core::Severity::Error,
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
    private:
    };

    class GpuTextureResource : public GpuResource
    {
    public:
        GpuTextureResource() = default;
        ~GpuTextureResource() override = default;
    protected:
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
    private:

    };
}
