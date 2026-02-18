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

    template<typename T>
    class SlotUploadBuffer
    {
        // Tがトリビアルコピーであることを静的アサート
        static_assert(std::is_trivially_copyable_v<T>, "SlotUploadBuffer requires trivial types");
    public:
        SlotUploadBuffer() = default;
        ~SlotUploadBuffer() = default;

        void initialize(size_t capacity, size_t alignment, std::byte* mappedData) noexcept
        {
            // 1) チェック
            if (capacity == 0 || alignment == 0 || !mappedData)
            {
                return; // 無効なパラメータ
            }

            // 2) 設定保持
            m_capacity = capacity;
            m_alignment = alignment;
            m_stride = sizeof(T);
            m_alignedStride = Math::round_up_to_multiple(m_stride, m_alignment);
            m_mappedData = mappedData;

            // 3) キュー準備
            m_uploadQueue.clear();
            m_uploadQueue.reserve(capacity);
            m_staging.clear();
        }

        void begin_frame() noexcept
        {
            m_uploadQueue.clear();
        }

        bool push(uint32_t slotIdx, const T& value) noexcept
        {
            // 1) 範囲チェック
            if (slotIdx >= m_capacity)
            {
                return false; // スロットインデックスが容量を超えている
            }

            // 2) キューに追加
            m_uploadQueue.push_back({ value, slotIdx });
            return true;
        }

        bool commit() noexcept
        {
            // 1) チェック
            if (!m_mappedData)
            {
                return false; // Mapされたデータがない
            }
            if (m_uploadQueue.empty())
            {
                return true; // コミットするデータがない
            }

            // 2) 重複削除
            //    - slotIdx -> unique内の位置 を覚えて、後から来た値で上書き
            std::vector<UploadData> unique;
            unique.reserve(m_uploadQueue.size());

            std::unordered_map<uint32_t, size_t> slotToPos;
            slotToPos.reserve(m_uploadQueue.size());

            for (const auto& e : m_uploadQueue)
            {
                auto it = slotToPos.find(e.slotIdx);
                if (it == slotToPos.end())
                {
                    slotToPos.emplace(e.slotIdx, unique.size());
                    unique.push_back(e);
                }
                else
                {
                    unique[it->second].value = e.value; // 最後勝ち
                }
            }

            // 3) slot順にソート
            std::sort(
                unique.begin(),
                unique.end(),
                [](const UploadData& a, const UploadData& b)
                {
                    return a.slotIdx < b.slotIdx;
                });

            // 4) 連続する区間を検出し、区間ごとに staging を作って memcpy
            size_t i = 0;
            while (i < unique.size())
            {
                // 4-1) 連続区間 [i, j] を見つける
                size_t j = i + 1;
                while (j < unique.size())
                {
                    const uint32_t prev = unique[j - 1].slotIdx;
                    const uint32_t curr = unique[j].slotIdx;

                    if (curr != (prev + 1u))
                    {
                        break;
                    }
                    ++j;
                }

                // 4-2) 区間サイズ
                const size_t runCount = j - i;
                const size_t runBytes = runCount * m_alignedStride;

                // 4-3) staging確保＆ゼロクリア
                if (m_staging.size() < runBytes)
                {
                    m_staging.resize(runBytes);
                }
                std::memset(m_staging.data(), 0, runBytes);

                // 4-4) staging に穴あき配置で詰める
                for (size_t k = 0; k < runCount; ++k)
                {
                    std::byte* dstSlot = m_staging.data() + (k * m_alignedStride);
                    std::memcpy(dstSlot, &unique[i + k].value, sizeof(T));
                }

                // 4-5) mapped へ区間ごとに memcpy
                const uint32_t startSlot = unique[i].slotIdx;
                std::byte* dst = m_mappedData + (static_cast<size_t>(startSlot) * m_alignedStride);
                std::memcpy(dst, m_staging.data(), runBytes);

                // 4-6) 次の区間へ
                i = j;
            }

            return true;
        }
    private:
        struct UploadData
        {
            T value;
            uint32_t slotIdx;
        };
    private:
        size_t m_alignment{}; // スロットのアライメント
        size_t m_stride{}; // Tのサイズ
        size_t m_alignedStride{}; // アライメントを考慮したスロットのサイズ
        size_t m_capacity{}; // スロットの最大数

        std::byte* m_mappedData = nullptr; // MapされたGPUメモリへのポインタ
        std::vector<UploadData> m_uploadQueue;
        std::vector<std::byte> m_staging;// 区間コピー用のスタッギングバッファ
    };

} // namespace Cue::GraphicsCore::DX12


