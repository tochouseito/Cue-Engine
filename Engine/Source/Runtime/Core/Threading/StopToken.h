// StopToken の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <atomic>

namespace Cue::Core::Threading
{
    /// @brief 停止要求の読み取り専用トークン
    class StopToken final
    {
    public:
        StopToken() noexcept = default;

        /// @brief 停止フラグ参照からトークンを構築する
        /// @param a_flag 非所有の停止フラグ
        explicit StopToken(const std::atomic<bool>* a_flag) noexcept
            : m_flag(a_flag)
        {
            // - 参照先は非所有
        }

        /// @brief 停止要求が出ているかを返す
        /// @return 停止要求済みなら `true` 
        bool stop_requested() const noexcept
        {
            // - フラグが無いなら停止要求なし
            if (m_flag == nullptr)
            {
                return false;
            }

            // - 読み取り（緩めでOK）
            return m_flag->load(std::memory_order_relaxed);
        }

    private:
        const std::atomic<bool>* m_flag = nullptr;
    };

    /// @brief 停止要求を書き込む側の発行元
    class StopSource final
    {
    public:
        StopSource() noexcept = default;

        /// @brief 監視用トークンを返す
        /// @return この発行元を監視するトークン
        StopToken token() const noexcept
        {
            // - 自分のフラグを指すトークンを返す
            return StopToken(&m_flag);
        }

        void request_stop() noexcept
        {
            // - 停止要求を立てる
            m_flag.store(true, std::memory_order_relaxed);
        }

        /// @brief 停止要求状態を初期化する
        void reset() noexcept
        {
            // - 停止要求を下ろす（再利用用）
            m_flag.store(false, std::memory_order_relaxed);
        }

    private:
        std::atomic<bool> m_flag{ false };
    };
}
