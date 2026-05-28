#pragma once

/// *********************************************************************************
/// 停止要求トークン
/// *********************************************************************************

// === C++ includes ===
#include <atomic>

namespace Cue::Core::Threading
{
    /// @brief 停止要求の読み取り専用トークン
    class StopToken final
    {
    public:
        /// @brief デフォルトコンストラクタ
        StopToken() noexcept = default;

        /// @brief 停止フラグ参照からトークンを構築
        /// @param a_flag 非所有の停止フラグ
        explicit StopToken(const std::atomic<bool>* a_flag) noexcept
            : m_flag(a_flag)
        {
        }

        /// @brief 停止要求が出ているかを返す
        /// @return 停止要求済みなら true
        bool stop_requested() const noexcept
        {
            if (m_flag == nullptr)
            {
                return false;
            }

            return m_flag->load(std::memory_order_relaxed);
        }

    private:
        const std::atomic<bool>* m_flag = nullptr; // 非所有の停止フラグ
    };

    /// @brief 停止要求を書き込む側の発行元
    class StopSource final
    {
    public:
        /// @brief デフォルトコンストラクタ
        StopSource() noexcept = default;

        /// @brief 監視用トークンを返す
        /// @return この発行元を監視するトークン
        StopToken token() const noexcept
        {
            return StopToken(&m_flag);
        }

        /// @brief 停止要求を通知
        void request_stop() noexcept
        {
            m_flag.store(true, std::memory_order_relaxed);
        }

        /// @brief 停止要求状態を初期化
        void reset() noexcept
        {
            m_flag.store(false, std::memory_order_relaxed);
        }

    private:
        std::atomic<bool> m_flag{ false }; // 停止要求フラグ
    };
}
