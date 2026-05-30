#pragma once

/// ********************************************************************************
/// CPU プロファイラー
/// ********************************************************************************

// === Base includes ===
#include <CueResult.h>
#include <CueAssert.h>

// === Core includes ===
#include "Time/Timer.h"

// === C++ includes ===
#include <cstdint>
#include <unordered_map>
#include <string>

namespace Cue::Core
{
    struct Snapshot
    {
        Snapshot(const Time::IClock& a_clock) : timer(a_clock)
        {}
        std::string name;
        Time::Timer timer;
        uint64_t frameIndex;
        uint64_t threadId;
    };

    /// @brief CPU プロファイラー
    class Profiler final
    {
    public:
        Profiler(const Time::IClock& a_clock) : m_clock(a_clock)
        {}
        ~Profiler()
        {

        }

        /// @brief スナップショットの開始
        /// @param groupName グループ名
        /// @param snapshotName スナップショット名
        void begin(const std::string& a_groupName, const std::string& a_snapshotName)
        {
            // 引数のチェック
            if(a_groupName.empty() || a_snapshotName.empty())
            {
                CUE_ASSERT_MSG(false, "Invalid arguments");
                return;
            }

            // スナップショットの開始時間を取得
            Snapshot snapshot(m_clock);
            snapshot.name = a_snapshotName;
            snapshot.frameIndex = 0; // フレームインデックスの取得方法は未実装
            snapshot.threadId = 0;   // スレッドIDの取得方法は未実装
            snapshot.timer.reset();
            snapshot.timer.start();

            m_snapshots[a_groupName].insert({a_snapshotName, snapshot});
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

            // グループとスナップショットの存在を確認
            if (!m_snapshots.contains(a_groupName) || !m_snapshots[a_groupName].contains(a_snapshotName))
            {
                CUE_ASSERT_MSG(false, "Snapshot not found");
                return;
            }

            // スナップショットの終了時間を取得
            Snapshot& snapshot = m_snapshots.at(a_groupName).at(a_snapshotName);
            snapshot.timer.stop();
        }

        Snapshot get_snapshot(const std::string& a_groupName, const std::string& a_snapshotName) const
        {
            // 引数のチェック
            if (a_groupName.empty() || a_snapshotName.empty())
            {
                CUE_ASSERT_MSG(false, "Invalid arguments");
                return Snapshot(m_clock); // デフォルトのスナップショットを返す
            }
            // グループとスナップショットの存在を確認
            if (!m_snapshots.contains(a_groupName) || !m_snapshots.at(a_groupName).contains(a_snapshotName))
            {
                CUE_ASSERT_MSG(false, "Snapshot not found");
                return Snapshot(m_clock); // デフォルトのスナップショットを返す
            }
            return m_snapshots.at(a_groupName).at(a_snapshotName);
        }
    private:
        const Time::IClock& m_clock; // クロック参照

        // グループ名 -> スナップショット名 -> スナップショット
        std::unordered_map<std::string, std::unordered_map<std::string, Snapshot>> m_snapshots;
    };
}
