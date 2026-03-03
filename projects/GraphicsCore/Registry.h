#pragma once
#include "ResourceHandle.h"
#include <vector>

namespace Cue::GraphicsCore
{
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

        [[nodiscard]] bool try_get(handle_type h, Record& outRecord) const
        {
            if (!is_alive(h))
            {
                return false;
            }
            outRecord = m_records[h.index];
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
    };

    struct TextureRecord
    {
        // テクスチャの実体をここに持つ
    };

    struct PipelineRecord
    {
        // パイプラインの実体をここに持つ
    };

    // 具体的なレジストリの型エイリアス
    using BufferRegistry = Registry<BufferTag, BufferRecord>;
    using TextureRegistry = Registry<TextureTag, TextureRecord>;
    using PipelineRegistry = Registry<PipelineStateTag, PipelineRecord>;

} // namespace Cue::GraphicsCore
