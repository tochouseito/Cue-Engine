#pragma once
#include <atomic>

namespace Cue::Core::Threading
{
    class StopToken final
    {
    public:
        StopToken() noexcept = default;

        explicit StopToken(const std::atomic<bool>* flag) noexcept
            : m_flag(flag)
        {
            // 1) 参照先は非所有
        }

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

    class StopSource final
    {
    public:
        StopSource() noexcept = default;

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

        void reset() noexcept
        {
            // 1) 停止要求を下ろす（再利用用）
            m_flag.store(false, std::memory_order_relaxed);
        }

    private:
        std::atomic<bool> m_flag{ false };
    };
}
