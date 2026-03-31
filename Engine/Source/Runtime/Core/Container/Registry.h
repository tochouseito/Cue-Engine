#pragma once

// === Core includes ===
#include "Native/Handle.h"

// === C++ includes ===
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace Cue::Core
{
    /// @brief 世代付きハンドルで実体を管理するレジストリです。
    /// @tparam Tag ハンドルを区別するタグ型です。
    /// @tparam Record 格納する実体型です。
    template <class Tag, class Record>
    class Registry final
    {
    public:
        using handle_type = Handle<Tag>;

        // Recordは、ハンドルのインデックスに対応する実体の型
        static_assert(std::is_default_constructible_v<Record>,
            "Registry<Record> requires default-constructible Record for destroy().");
        // 破棄済みスロットを再利用できるよう Record をムーブ可能として扱う
        static_assert(std::is_move_assignable_v<Record>,
            "Registry<Record> requires move-assignable Record for slot reuse.");

        /// @brief レコードを登録してハンドルを返します。
        /// @param a_record 登録するレコードです。
        /// @return 発行したハンドルです。
        [[nodiscard]] handle_type create(Record& a_record)
        {
            // 1) 空きスロット再利用または末尾追加
            uint32_t index = 0;

            if (!m_freeList.empty())
            {
                index = m_freeList.back();
                m_freeList.pop_back();
                m_records[index] = std::move(a_record);
            }
            else
            {
                if (m_records.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
                {
                    throw std::overflow_error("Registry index overflow");
                }

                index = static_cast<uint32_t>(m_records.size());
                m_records.push_back(std::move(a_record));
                m_generations.push_back(0);
            }

            // 2) ハンドル発行
            return handle_type{ index, m_generations[index] };
        }

        /// @brief ハンドルに対応するレコードを破棄します。
        /// @param a_handle 破棄対象のハンドルです。
        /// @return 破棄に成功した場合は `true` です。
        bool destroy(handle_type a_handle)
        {
            // 1) 妥当性チェック
            if (!is_alive(a_handle))
            {
                return false;
            }

            const uint32_t index = a_handle.index;

            // 2) レコード初期化
            m_records[index] = Record{};

            // 3) 世代更新で古いハンドルを殺す
            m_generations[index] = m_generations[index] + 1u;

            // 4) 空きに戻す
            m_freeList.push_back(index);
            return true;
        }

        template <class F>
        /// @brief 生存しているレコードに関数を適用します。
        /// @param a_handle 対象ハンドルです。
        /// @param a_func 実体へ適用する関数です。
        /// @return ハンドルが有効な場合は `true` です。
        [[nodiscard]] bool with(handle_type a_handle, F&& a_func)
        {
            // 1) 妥当性チェック
            if (!is_alive(a_handle))
            {
                return false;
            }

            // 2) ハンドルで再検証した上で、その場でアクセスさせる（ポインタを外へ出さない）
            std::forward<F>(a_func)(m_records[a_handle.index]);
            return true;
        }

        template <class F>
        /// @brief 生存しているレコードに読み取り関数を適用します。
        /// @param a_handle 対象ハンドルです。
        /// @param a_func 実体へ適用する関数です。
        /// @return ハンドルが有効な場合は `true` です。
        [[nodiscard]] bool with(handle_type a_handle, F&& a_func) const
        {
            if (!is_alive(a_handle))
            {
                return false;
            }
            std::forward<F>(a_func)(m_records[a_handle.index]);
            return true;
        }

        /// @brief レコードをコピー取得します。
        /// @param a_handle 対象ハンドルです。
        /// @param a_outRecord 取得先です。
        /// @return ハンドルが有効な場合は `true` です。
        [[nodiscard]] bool try_copy_get(handle_type a_handle, Record& a_outRecord) const
        {
            if (!is_alive(a_handle))
            {
                return false;
            }
            a_outRecord = m_records[a_handle.index];
            return true;
        }

        /// @brief レコードを参照取得します。
        /// @param a_handle 対象ハンドルです。
        /// @return ハンドルが有効な場合はレコードへの参照を返します。無効な場合は `nullptr` です。
        [[nodiscard]] Record* ref_get(handle_type a_handle)
        {
            if (!is_alive(a_handle))
            {
                return nullptr;
            }
            return &m_records[a_handle.index];
        }
        [[nodiscard]] const Record* ref_get(handle_type a_handle) const
        {
            if (!is_alive(a_handle))
            {
                return nullptr;
            }
            return &m_records[a_handle.index];
        }
    private:
        [[nodiscard]] bool is_alive(handle_type a_handle) const noexcept
        {
            // 1) 無効ハンドル
            if (!a_handle.valid())
            {
                return false;
            }

            // 2) 範囲
            if (a_handle.index >= m_generations.size())
            {
                return false;
            }

            // 3) 世代一致
            return (m_generations[a_handle.index] == a_handle.generation);
        }

    private:
        std::vector<Record> m_records;
        std::vector<uint32_t> m_generations;
        std::vector<uint32_t> m_freeList;
    };

    struct TestRecord final
    {
        int value = 0;
    };

    // 具体的なレジストリ型のエイリアス
    using TestRegistry = Registry<TestTag, TestRecord>;
}
