#pragma once
#include "stdafx.h"
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <cstring>
#include <vector>
#include <span>

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
        Result init(
            ComPtr<ID3D12Resource>&& resource,
            D3D12_RESOURCE_STATES initialState,
            std::wstring_view name)
        {
            // 1) 二重初期化を防ぐ
            if (m_resource)
            {
                return Result::fail(
                    Core::Facility::D3D12,
                    Core::Code::InvalidState,
                    Core::Severity::Fatal,
                    false,
                    "GpuResource is already initialized.");
            }

            // 2) リソースを受け取る
            m_resource = std::move(resource);
            if (!m_resource)
            {
                return Result::fail(
                    Core::Facility::D3D12,
                    Core::Code::InvalidArg,
                    Core::Severity::Fatal,
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


    class SlotUploadBuffer
    {
    public:
        static constexpr uint32_t invalid_slot = 0xFFFFFFFFu;

        SlotUploadBuffer() = default;
        ~SlotUploadBuffer() = default;

        SlotUploadBuffer(const SlotUploadBuffer&) = delete;
        SlotUploadBuffer& operator=(const SlotUploadBuffer&) = delete;

        void reset() noexcept
        {
            // 1) Slot 管理を初期化（バッファ実体は触らない）
            m_count = 0;
            m_freeSlots.clear();

            // 2) フラグ初期化
            if (!m_isFree.empty())
            {
                std::memset(m_isFree.data(), 1, m_isFree.size());
            }
        }

        void set_mapped_data(std::byte* mappedBase, uint64_t totalBytes, D3D12_GPU_VIRTUAL_ADDRESS gpuBase) noexcept
        {
            // 1) Map 済みのCPUポインタとGPU先頭アドレスを保持
            m_mappedBase = mappedBase;
            m_totalBytes = totalBytes;
            m_gpuBase = gpuBase;

            // 2) 既存のレイアウトが設定済みなら capacity を再計算
            if (m_strideAligned != 0)
            {
                update_capacity();
            }
        }

        void set_layout(uint32_t strideBytes, uint32_t alignment) noexcept
        {
            // 1) 入力検証（alignment=0は許可しない）
            if (strideBytes == 0)
            {
                Core::LogAssert::cue_assert(
                    false,
                    Core::LogSink::debugConsole,
                    "StrideBytes must be non-zero.");
                return;
            }
            if (alignment == 0)
            {
                Core::LogAssert::cue_assert(
                    false,
                    Core::LogSink::debugConsole,
                    "Alignment must be non-zero.");
                return;
            }

            // 2) アライン計算
            m_strideBytes = strideBytes;
            m_strideAligned = align_up_u32(strideBytes, alignment);

            // 3) mapped があるなら capacity を更新
            update_capacity();
        }

        [[nodiscard]]
        uint32_t allocate()
        {
            // 1) 前提チェック（mapped/layout 未設定なら事故る）
            if ((m_mappedBase == nullptr) || (m_strideAligned == 0) || (m_capacity == 0))
            {
                Core::LogAssert::cue_assert(
                    false,
                    Core::LogSink::debugConsole,
                    "SlotUploadBuffer is not configured. Call set_mapped_data() and set_layout().");
                return invalid_slot;
            }

            // 2) 空きスロットがあれば再利用
            if (!m_freeSlots.empty())
            {
                const uint32_t slot = m_freeSlots.back();
                m_freeSlots.pop_back();

                // 3) フラグ更新
                if (slot < m_isFree.size())
                {
                    m_isFree[slot] = 0;
                }
                return slot;
            }

            // 3) 新規スロット
            if (m_count >= m_capacity)
            {
                Core::LogAssert::cue_assert(
                    false,
                    Core::LogSink::debugConsole,
                    "SlotUploadBuffer capacity exceeded.");
                return invalid_slot;
            }

            const uint32_t slot = m_count;
            ++m_count;

            // 4) フラグ更新
            m_isFree[slot] = 0;
            return slot;
        }

        void free(uint32_t slot)
        {
            // 1) 妥当性チェック
            if (slot >= m_count)
            {
                Core::LogAssert::cue_assert(
                    false,
                    Core::LogSink::debugConsole,
                    "Invalid slot number freed.");
                return;
            }

            // 2) 二重解放チェック（O(1)）
            if (m_isFree[slot] != 0)
            {
                Core::LogAssert::cue_assert(
                    false,
                    Core::LogSink::debugConsole,
                    "Slot number already freed.");
                return;
            }

            // 3) free に戻す
            m_isFree[slot] = 1;
            m_freeSlots.push_back(slot);
        }

        [[nodiscard]]
        bool write_bytes(uint32_t slot, const void* src, uint32_t bytes, D3D12_GPU_VIRTUAL_ADDRESS* outGpuAddress = nullptr) noexcept
        {
            // 1) 前提チェック
            if ((m_mappedBase == nullptr) || (m_strideAligned == 0))
            {
                return false;
            }
            if (src == nullptr)
            {
                return false;
            }
            if (bytes == 0)
            {
                return false;
            }

            // 2) スロット検証（範囲・解放済み・容量）
            if (slot >= m_count)
            {
                return false;
            }
            if (m_isFree[slot] != 0)
            {
                return false;
            }
            if (bytes > m_strideBytes)
            {
                return false;
            }

            // 3) 書き込み先計算
            const uint64_t offset = static_cast<uint64_t>(slot) * static_cast<uint64_t>(m_strideAligned);
            if ((offset + bytes) > m_totalBytes)
            {
                return false;
            }

            std::byte* dst = m_mappedBase + offset;

            // 4) 書き込み（基本これで正解）
            std::memcpy(dst, src, bytes);

            // 5) GPUアドレス返す（必要なら）
            if (outGpuAddress)
            {
                *outGpuAddress = m_gpuBase + offset;
            }
            return true;
        }

        template<typename T>
        [[nodiscard]]
        bool write(uint32_t slot, const T& value, D3D12_GPU_VIRTUAL_ADDRESS* outGpuAddress = nullptr) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable.");

            // 1) 型サイズがスロットに収まる前提
            const uint32_t bytes = static_cast<uint32_t>(sizeof(T));
            return write_bytes(slot, &value, bytes, outGpuAddress);
        }

        [[nodiscard]]
        std::byte* cpu_ptr(uint32_t slot) noexcept
        {
            // 1) 前提チェック
            if ((m_mappedBase == nullptr) || (m_strideAligned == 0))
            {
                return nullptr;
            }
            if (slot >= m_count)
            {
                return nullptr;
            }
            if (m_isFree[slot] != 0)
            {
                return nullptr;
            }

            // 2) 先頭 + オフセット
            const uint64_t offset = static_cast<uint64_t>(slot) * static_cast<uint64_t>(m_strideAligned);
            if (offset >= m_totalBytes)
            {
                return nullptr;
            }
            return m_mappedBase + offset;
        }

        [[nodiscard]]
        D3D12_GPU_VIRTUAL_ADDRESS gpu_address(uint32_t slot) const noexcept
        {
            // 1) 範囲チェック（最低限）
            if ((m_strideAligned == 0) || (slot >= m_count))
            {
                return 0;
            }

            // 2) 先頭 + オフセット
            const uint64_t offset = static_cast<uint64_t>(slot) * static_cast<uint64_t>(m_strideAligned);
            return m_gpuBase + offset;
        }

        [[nodiscard]]
        uint32_t capacity() const noexcept
        {
            return m_capacity;
        }

        [[nodiscard]]
        uint32_t stride_aligned() const noexcept
        {
            return m_strideAligned;
        }

        [[nodiscard]]
        uint32_t allocated_count() const noexcept
        {
            return m_count;
        }

    private:
        static uint32_t align_up_u32(uint32_t v, uint32_t a) noexcept
        {
            // 1) a が2の冪である想定（違うなら呼び出し側で統一しろ）
            // 2) 切り上げ
            return (v + (a - 1u)) & ~(a - 1u);
        }

        void update_capacity() noexcept
        {
            // 1) stride と totalBytes が揃っていなければ何もしない
            if ((m_strideAligned == 0) || (m_totalBytes == 0))
            {
                return;
            }

            // 2) 容量計算
            const uint64_t cap64 = m_totalBytes / static_cast<uint64_t>(m_strideAligned);
            m_capacity = static_cast<uint32_t>(cap64);

            // 3) フラグ配列を確保
            m_isFree.resize(m_capacity);
            std::memset(m_isFree.data(), 1, m_isFree.size());

            // 4) 既存管理を初期化（capacity変化で破綻するため）
            m_count = 0;
            m_freeSlots.clear();
        }

    private:
        // 管理
        uint32_t m_count = 0;
        uint32_t m_capacity = 0;
        std::vector<uint32_t> m_freeSlots{};
        std::vector<uint8_t> m_isFree{}; // 0:使用中, 1:空き

        // レイアウト
        uint32_t m_strideBytes = 0;
        uint32_t m_strideAligned = 0;

        // 書き込み先（非所有）
        std::byte* m_mappedBase = nullptr;
        uint64_t m_totalBytes = 0;
        D3D12_GPU_VIRTUAL_ADDRESS m_gpuBase = 0;
    };

} // namespace Cue::GraphicsCore::DX12


