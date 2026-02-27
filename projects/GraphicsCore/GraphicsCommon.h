#pragma once

// === C++ standard library includes ===
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// === Base includes ===
#include <Result.h>
#include <CueAssert.h>

// === Math includes ===
#include <CueMath.h>

namespace Cue::GraphicsCore
{
    enum class ResourceState : uint8_t
    {
        Common,
        RenderTarget,
        UnorderedAccess,
        ShaderResource,
        DepthWrite,
        Present
    };

    inline const char* resource_state_to_string(ResourceState state) noexcept
    {
        switch (state)
        {
        case ResourceState::Common: return "Common";
        case ResourceState::RenderTarget: return "RenderTarget";
        case ResourceState::UnorderedAccess: return "UnorderedAccess";
        case ResourceState::ShaderResource: return "ShaderResource";
        case ResourceState::DepthWrite: return "DepthWrite";
        case ResourceState::Present: return "Present";
        default: return "Unknown";
        }
    }

    enum class CommandListType : uint8_t
    {
        Graphics,
        Compute,
        Copy
    };

    inline const char* command_list_type_to_string(CommandListType type) noexcept
    {
        switch (type)
        {
        case CommandListType::Graphics: return "Graphics";
        case CommandListType::Compute: return "Compute";
        case CommandListType::Copy: return "Copy";
        default: return "Unknown";
        }
    }

    enum class ResourceAccessType : uint8_t
    {
        Read,
        Write,
    };

    inline const char* resource_access_type_to_string(ResourceAccessType type) noexcept
    {
        switch (type)
        {
        case ResourceAccessType::Read: return "Read";
        case ResourceAccessType::Write: return "Write";
        default: return "Unknown";
        }
    }

    enum class ResourceKind : uint8_t
    {
        Buffer,
        Texture,
        Pipeline
    };

    struct ResourceBarrierDesc final
    {
        ResourceKind kind = ResourceKind::Buffer;
        uint32_t index = 0;
        uint32_t generation = 0;
        ResourceState before = ResourceState::Common;
        ResourceState after = ResourceState::Common;
    };

    struct QueueSyncPoint final
    {
        CommandListType queueType = CommandListType::Graphics;
        uint64_t value = 0;
    };

    inline const char* resource_kind_to_string(ResourceKind kind) noexcept
    {
        switch (kind)
        {
        case ResourceKind::Buffer: return "Buffer";
        case ResourceKind::Texture: return "Texture";
        case ResourceKind::Pipeline: return "Pipeline";
        default: return "Unknown";
        }
    }

    class GpuResource
    {
    public:
        GpuResource() = default;
        virtual ~GpuResource() = default;
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

    // 文字列を整数で扱うためのハッシュ関数
    using ResourceNameId = uint64_t;
    constexpr ResourceNameId fnv1a64(std::string_view text) noexcept
    {
        ResourceNameId hash = 14695981039346656037ull;
        for (char c : text)
        {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    // 汎用リソースハンドル
    template <class Tag>
    struct Handle final
    {
        static constexpr uint32_t k_invalid = 0xFFFFFFFFu; // 無効なハンドルを示す値

        uint32_t index = k_invalid; // リソースのインデックス
        uint32_t generation = 0; // 世代管理用のカウンタ

        // ハンドルが有効かどうかをチェックする関数
        [[nodiscard]] bool valid() const noexcept
        {
            return index != k_invalid;
        }

        // ハンドル同士の比較演算子
        bool operator==(const Handle& other) const noexcept
        {
            return (index == other.index) && (generation == other.generation);
        }
    };

    // リソースハンドルのタグ型
    struct BufferTag {};
    struct TextureTag {};
    struct PipelineTag {};

    // エイリアス
    using BufferHandle = Handle<BufferTag>;
    using TextureHandle = Handle<TextureTag>;
    using PipelineHandle = Handle<PipelineTag>;

    // レジストリ
    template <class Tag, class Record>
    class Registry final
    {
    public:
        using handle_type = Handle<Tag>;

        // Recordは、ハンドルのインデックスに対応する実体の型
        static_assert(std::is_default_constructible_v<Record>,
            "Registry<Record> requires default-constructible Record for destroy().");
        // 破棄時に空きスロットを再利用するため、Recordはムーブ可能である必要がある
        static_assert(std::is_move_assignable_v<Record>,
            "Registry<Record> requires move-assignable Record for slot reuse.");

        [[nodiscard]] handle_type create(Record& record)
        {
            // 1) 空き再利用 or 末尾追加
            uint32_t idx = 0;

            if (!m_freeList.empty())
            {
                idx = m_freeList.back();
                m_freeList.pop_back();
                m_records[idx] = std::move(record);
            }
            else
            {
                if (m_records.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
                {
                    throw std::overflow_error("Registry index overflow");
                }

                idx = static_cast<uint32_t>(m_records.size());
                m_records.push_back(std::move(record));
                m_generations.push_back(0);
            }

            // 2) ハンドル発行
            return handle_type{ idx, m_generations[idx] };
        }

        bool destroy(handle_type h)
        {
            // 1) 妥当性チェック
            if (!is_alive(h))
            {
                return false;
            }

            const uint32_t idx = h.index;

            // 2) レコード初期化
            m_records[idx] = Record{};

            // 3) 世代更新で古いハンドルを殺す
            m_generations[idx] = m_generations[idx] + 1u;

            // 4) 空きに戻す
            m_freeList.push_back(idx);
            return true;
        }

        template <class F>
        [[nodiscard]] bool with(handle_type h, F&& f)
        {
            // 1) 妥当性チェック
            if (!is_alive(h))
            {
                return false;
            }

            // 2) ハンドルで再検証した上で、その場でアクセスさせる（ポインタを外へ出さない）
            std::forward<F>(f)(m_records[h.index]);
            return true;
        }

        template <class F>
        [[nodiscard]] bool with(handle_type h, F&& f) const
        {
            if (!is_alive(h))
            {
                return false;
            }
            std::forward<F>(f)(m_records[h.index]);
            return true;
        }

    private:
        [[nodiscard]] bool is_alive(handle_type h) const noexcept
        {
            // 1) invalid
            if (!h.valid())
            {
                return false;
            }

            // 2) 範囲
            if (h.index >= m_generations.size())
            {
                return false;
            }

            // 3) 世代一致
            return (m_generations[h.index] == h.generation);
        }

    private:
        std::vector<Record> m_records;
        std::vector<uint32_t> m_generations;
        std::vector<uint32_t> m_freeList;
    };

    struct BufferRecord
    {
        // バッファの実体（例: ID3D12Resource*）をここに持つ
        // 例: Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        GpuResource resource; // 仮のGpuResourceクラス
    };

    struct TextureRecord
    {
        // テクスチャの実体をここに持つ
        GpuResource resource; // 仮のGpuResourceクラス
    };

    struct PipelineRecord
    {
        // パイプラインの実体をここに持つ
    };

    // 具体的なレジストリの型エイリアス
    using BufferRegistry = Registry<BufferTag, BufferRecord>;
    using TextureRegistry = Registry<TextureTag, TextureRecord>;
    using PipelineRegistry = Registry<PipelineTag, PipelineRecord>;

    class ICommandContext
    {
    public:
        ICommandContext() = default;
        virtual ~ICommandContext() = default;

        virtual Result reset() = 0;
        virtual Result close() = 0;
        virtual CommandListType type() const = 0;

        bool is_list_empty() const
        {
            return m_listEmpty;
        }
    protected:
        bool m_listEmpty = true; // コマンドリストが空かどうか
    public:
        // Commands
        virtual void begin_event(const char* name) = 0;
        virtual void end_event() = 0;

        virtual Result resource_barrier(const ResourceBarrierDesc& barrier) = 0;
        virtual Result resource_barriers(const ResourceBarrierDesc* barriers, size_t count) = 0;
    };

    class IQueueContext
    {
    public:
        IQueueContext() = default;
        virtual ~IQueueContext() = default;

        virtual CommandListType type() const = 0;
        virtual Result submit(ICommandContext& cmd) = 0;
        virtual Result signal(QueueSyncPoint& outPoint) = 0;
        virtual Result wait(const QueueSyncPoint& point) = 0;
    };
}
