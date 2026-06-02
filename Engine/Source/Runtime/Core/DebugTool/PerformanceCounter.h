#pragma once

/// ********************************************************************************
/// パフォーマンスカウンター
/// ********************************************************************************

// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Core includes ===
#include "Time/Timer.h"

// === C++ includes ===
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace Cue::Core
{
    struct Snapshot
    {
        Snapshot(const Time::IClock& a_clock) : timer(a_clock)
        {}
        std::string name;
        Time::Timer timer;
        uint64_t frameIndex = 0;
        uint64_t threadId = 0;
    };

    /// @brief パフォーマンスカウンター
    class PerformanceCounter final
    {
    public:
        PerformanceCounter(const Time::IClock& a_clock) : m_clock(a_clock)
        {}
        ~PerformanceCounter() = default;

        /// @brief スナップショットの開始
        /// @param groupName グループ名
        /// @param snapshotName スナップショット名
        void begin(const std::string& a_groupName, const std::string& a_snapshotName)
        {
            // 引数のチェック
            if (a_groupName.empty() || a_snapshotName.empty())
            {
                CUE_ASSERT_MSG(false, "Invalid arguments");
                return;
            }

            std::lock_guard lock(m_mutex);

            // スナップショットの開始時間を取得
            Snapshot snapshot(m_clock);
            snapshot.name = a_snapshotName;
            snapshot.frameIndex = 0; // フレームインデックスの取得方法は未実装
            snapshot.threadId = static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
            snapshot.timer.reset();
            snapshot.timer.start();

            m_snapshots[a_groupName].insert_or_assign(a_snapshotName, snapshot);
        }

        /// @brief スナップショットの終了
        /// @param groupName グループ名
        /// @param snapshotName スナップショット名
        void end(const std::string& a_groupName, const std::string& a_snapshotName)
        {
            // 引数のチェック
            if (a_groupName.empty() || a_snapshotName.empty())
            {
                CUE_ASSERT_MSG(false, "Invalid arguments");
                return;
            }

            std::lock_guard lock(m_mutex);

            // グループとスナップショットの存在を確認
            auto groupIt = m_snapshots.find(a_groupName);
            if (groupIt == m_snapshots.end())
            {
                CUE_ASSERT_MSG(false, "Snapshot not found");
                return;
            }

            auto snapshotIt = groupIt->second.find(a_snapshotName);
            if (snapshotIt == groupIt->second.end())
            {
                CUE_ASSERT_MSG(false, "Snapshot not found");
                return;
            }

            // スナップショットの終了時間を取得
            snapshotIt->second.timer.stop();
        }

        std::optional<Snapshot> get_snapshot(const std::string& a_groupName, const std::string& a_snapshotName) const
        {
            // 引数のチェック
            if (a_groupName.empty() || a_snapshotName.empty())
            {
                CUE_ASSERT_MSG(false, "Invalid arguments");
                return std::nullopt;
            }

            std::lock_guard lock(m_mutex);

            // グループとスナップショットの存在を確認
            auto groupIt = m_snapshots.find(a_groupName);
            if (groupIt == m_snapshots.end())
            {
                CUE_ASSERT_MSG(false, "Snapshot not found");
                return std::nullopt;
            }

            auto snapshotIt = groupIt->second.find(a_snapshotName);
            if (snapshotIt == groupIt->second.end())
            {
                CUE_ASSERT_MSG(false, "Snapshot not found");
                return std::nullopt;
            }

            return snapshotIt->second;
        }
    private:
        const Time::IClock& m_clock; // クロック参照
        mutable std::mutex m_mutex;

        // グループ名 -> スナップショット名 -> スナップショット
        std::unordered_map<std::string, std::unordered_map<std::string, Snapshot>> m_snapshots;
    };
}
