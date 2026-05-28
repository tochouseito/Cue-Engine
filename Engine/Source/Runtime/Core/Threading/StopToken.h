#pragma once

// === C++ includes ===
#include <atomic>

namespace Cue::Core::Threading
{
    /// @brief 停止要求の読み取り専用トークンです。
    class StopToken final
    {
    public:
        StopToken() noexcept = default;

        /// @brief 停止フラグ参照からトークンを構築します。
        /// @param a_flag 非所有の停止フラグです。
        explicit StopToken(const std::atomic<bool>* a_flag) noexcept
            : m_flag(a_flag)
        {
            // 1) 参照先は非所有
        }

        /// @brief 停止要求が出ているかを返します。
        /// @return 停止要求済みなら `true` です。
        bool stop_requested() const noexcept
        {
            // 1) フラグが無いなら停止要求なし
            if (m_flag == nullptr)
            {
                return false;
            }

            // 2) 読み取り（緩めでOK）
            return m_flag->load(std::memory_order_relaxed);
        }

    private:
        const std::atomic<bool>* m_flag = nullptr;
    };

    /// @brief 停止要求を書き込む側の発行元です。
    class StopSource final
    {
    public:
        StopSource() noexcept = default;

        /// @brief 監視用トークンを返します。
        /// @return この発行元を監視するトークンです。
        StopToken token() const noexcept
        {
            // 1) 自分のフラグを指すトークンを返す
            return StopToken(&m_flag);
        }

        void request_stop() noexcept
        {
            // 1) 停止要求を立てる
            m_flag.store(true, std::memory_order_relaxed);
        }

        /// @brief 停止要求状態を初期化します。
        void reset() noexcept
        {
            // 1) 停止要求を下ろす（再利用用）
            m_flag.store(false, std::memory_order_relaxed);
        }

    private:
        std::atomic<bool> m_flag{ false };
    };
}
