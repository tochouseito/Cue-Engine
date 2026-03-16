#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Cue::GraphicsCore
{
    template<typename T>
    class SlotUploader
    {
        // Tがトリビアルコピーであることを静的アサート
        static_assert(std::is_trivially_copyable_v<T>, "SlotUploadBuffer requires trivial types");
        [[nodiscard]] static constexpr size_t round_up_to_multiple(size_t value, size_t alignment) noexcept
        {
            return alignment == 0 ? value : ((value + alignment - 1u) / alignment) * alignment;
        }
    public:
        SlotUploader() = default;
        ~SlotUploader() = default;

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
            m_alignedStride = round_up_to_multiple(m_stride, m_alignment);
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
} // namespace Cue::GraphicsCore
