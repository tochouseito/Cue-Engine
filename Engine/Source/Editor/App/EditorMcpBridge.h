#pragma once

/// **********************************************************************
/// CueEditor MCP から Play 状態を要求する localhost bridge を定義する
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === C++ includes ===
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace Cue::Editor
{
    enum class EditorMcpPlaybackRequest : uint8_t
    {
        none = 0,
        play,
        pause,
        step,
    };

    enum class EditorMcpPlaybackState : uint8_t
    {
        editing = 0,
        playing,
        paused,
    };

    enum class EditorMcpScriptOpenState : uint8_t
    {
        idle = 0,
        pending,
        succeeded,
        failed,
    };

    struct EditorMcpBridgeSetupInfo final
    {
        uint16_t port = 18766u;
    };

    /// @brief loopback HTTP 要求を main thread の安全な Play 要求へ変換する
    class EditorMcpBridge final
    {
    public:
        EditorMcpBridge() = default;
        ~EditorMcpBridge();

        EditorMcpBridge(const EditorMcpBridge&) = delete;
        EditorMcpBridge& operator=(const EditorMcpBridge&) = delete;
        EditorMcpBridge(EditorMcpBridge&&) = delete;
        EditorMcpBridge& operator=(EditorMcpBridge&&) = delete;

        /// @brief loopback HTTP listener を開始する
        [[nodiscard]] Result start(const EditorMcpBridgeSetupInfo& a_info);

        /// @brief listener thread を停止して socket を解放する
        void shutdown() noexcept;

        /// @brief main thread で処理する次の Play 要求を取り出す
        [[nodiscard]] EditorMcpPlaybackRequest consume_playback_request() noexcept;

        /// @brief `/health` が返す Editor の現在実行状態を更新する
        void set_playback_state(EditorMcpPlaybackState a_state) noexcept;

        /// @brief main thread で開く Script asset の Project 相対 path を取り出す
        [[nodiscard]] bool consume_script_open_request(std::string& a_outAssetPath) noexcept;

        /// @brief `/health` が返す Script 起動処理の結果を更新する
        void set_script_open_state(EditorMcpScriptOpenState a_state) noexcept;

    private:
        void run() noexcept;
        void handle_connection(uintptr_t a_socket) noexcept;

        std::atomic<bool> m_isRunning = false;
        std::atomic<EditorMcpPlaybackRequest> m_playbackRequest = EditorMcpPlaybackRequest::none;
        std::atomic<EditorMcpPlaybackState> m_playbackState = EditorMcpPlaybackState::editing;
        std::atomic<EditorMcpScriptOpenState> m_scriptOpenState = EditorMcpScriptOpenState::idle;
        // HTTP thread と main thread の間で Project 相対 path を一度だけ受け渡す。
        std::mutex m_scriptOpenRequestMutex{};
        std::string m_scriptOpenRequestPath{};
        uintptr_t m_listenSocket = ~uintptr_t{0u};
        std::thread m_thread{};
        bool m_isWinSockInitialized = false;
    };
} // namespace Cue::Editor
