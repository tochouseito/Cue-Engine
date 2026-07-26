#include "EditorMcpBridge.h"

// === Windows includes ===
#include <winsock2.h>
#include <ws2tcpip.h>

// === C++ includes ===
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace Cue::Editor
{
    namespace
    {
        constexpr uintptr_t k_invalidSocket = ~uintptr_t{0u};

        [[nodiscard]] const char* playback_state_name(EditorMcpPlaybackState a_state) noexcept
        {
            switch (a_state)
            {
            case EditorMcpPlaybackState::playing:
                return "playing";
            case EditorMcpPlaybackState::paused:
                return "paused";
            case EditorMcpPlaybackState::editing:
            default:
                return "editing";
            }
        }

        [[nodiscard]] const char* script_open_state_name(EditorMcpScriptOpenState a_state) noexcept
        {
            switch (a_state)
            {
            case EditorMcpScriptOpenState::pending:
                return "pending";
            case EditorMcpScriptOpenState::succeeded:
                return "succeeded";
            case EditorMcpScriptOpenState::failed:
                return "failed";
            case EditorMcpScriptOpenState::idle:
            default:
                return "idle";
            }
        }

        [[nodiscard]] bool extract_asset_path(
            std::string_view a_request,
            std::string& a_outPath) noexcept
        {
            constexpr std::string_view k_pathPrefix = "\"path\":\"";
            const size_t pathStart = a_request.find(k_pathPrefix);
            if (pathStart == std::string_view::npos)
            {
                return false;
            }

            const size_t valueStart = pathStart + k_pathPrefix.size();
            const size_t valueEnd = a_request.find('"', valueStart);
            if (valueEnd == std::string_view::npos || valueEnd == valueStart)
            {
                return false;
            }

            a_outPath.assign(a_request.substr(valueStart, valueEnd - valueStart));
            return true;
        }

        [[nodiscard]] char to_ascii_lower(char a_character) noexcept
        {
            return a_character >= 'A' && a_character <= 'Z'
                       ? static_cast<char>(a_character - 'A' + 'a')
                       : a_character;
        }

        [[nodiscard]] bool contains_ascii_case_insensitive(
            std::string_view a_text,
            std::string_view a_filter) noexcept
        {
            if (a_filter.empty())
            {
                return true;
            }
            if (a_filter.size() > a_text.size())
            {
                return false;
            }

            for (size_t start = 0u; start <= a_text.size() - a_filter.size(); ++start)
            {
                bool matches = true;
                for (size_t index = 0u; index < a_filter.size(); ++index)
                {
                    if (to_ascii_lower(a_text[start + index]) !=
                        to_ascii_lower(a_filter[index]))
                    {
                        matches = false;
                        break;
                    }
                }
                if (matches)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] uint8_t hex_value(char a_character) noexcept
        {
            if (a_character >= '0' && a_character <= '9')
            {
                return static_cast<uint8_t>(a_character - '0');
            }
            const char lower = to_ascii_lower(a_character);
            return lower >= 'a' && lower <= 'f'
                       ? static_cast<uint8_t>(lower - 'a' + 10)
                       : (std::numeric_limits<uint8_t>::max)();
        }

        [[nodiscard]] std::string decode_url_component(std::string_view a_value)
        {
            std::string decoded{};
            decoded.reserve(a_value.size());
            for (size_t index = 0u; index < a_value.size(); ++index)
            {
                if (a_value[index] == '%' && index + 2u < a_value.size())
                {
                    const uint8_t high = hex_value(a_value[index + 1u]);
                    const uint8_t low = hex_value(a_value[index + 2u]);
                    if (high != (std::numeric_limits<uint8_t>::max)() &&
                        low != (std::numeric_limits<uint8_t>::max)())
                    {
                        decoded.push_back(static_cast<char>((high << 4u) | low));
                        index += 2u;
                        continue;
                    }
                }
                decoded.push_back(a_value[index] == '+' ? ' ' : a_value[index]);
            }
            return decoded;
        }

        [[nodiscard]] std::string query_value(
            std::string_view a_requestTarget,
            std::string_view a_name)
        {
            const size_t queryStart = a_requestTarget.find('?');
            if (queryStart == std::string_view::npos)
            {
                return {};
            }

            const std::string key = std::string(a_name) + "=";
            size_t valueStart = queryStart + 1u;
            while (valueStart < a_requestTarget.size())
            {
                const size_t valueEnd = a_requestTarget.find('&', valueStart);
                const std::string_view parameter = a_requestTarget.substr(
                    valueStart, valueEnd - valueStart);
                if (parameter.starts_with(key))
                {
                    return decode_url_component(parameter.substr(key.size()));
                }
                if (valueEnd == std::string_view::npos)
                {
                    break;
                }
                valueStart = valueEnd + 1u;
            }
            return {};
        }

        [[nodiscard]] size_t entity_limit(std::string_view a_requestTarget)
        {
            constexpr size_t k_defaultLimit = 100u;
            constexpr size_t k_maxLimit = 500u;
            const std::string limitText = query_value(a_requestTarget, "limit");
            if (limitText.empty())
            {
                return k_defaultLimit;
            }

            size_t limit = 0u;
            const auto [end, error] = std::from_chars(
                limitText.data(), limitText.data() + limitText.size(), limit);
            if (error != std::errc{} || end != limitText.data() + limitText.size() ||
                limit == 0u)
            {
                return k_defaultLimit;
            }
            return (std::min)(limit, k_maxLimit);
        }

        void append_json_string(std::string& a_json, std::string_view a_value)
        {
            a_json.push_back('"');
            for (const char character : a_value)
            {
                switch (character)
                {
                case '"':
                    a_json += "\\\"";
                    break;
                case '\\':
                    a_json += "\\\\";
                    break;
                case '\n':
                    a_json += "\\n";
                    break;
                case '\r':
                    a_json += "\\r";
                    break;
                case '\t':
                    a_json += "\\t";
                    break;
                default:
                    a_json.push_back(character);
                    break;
                }
            }
            a_json.push_back('"');
        }

        void append_entity_json(
            std::string& a_json,
            const EditorMcpEntitySnapshot& a_entity)
        {
            a_json += "{\"entityId\":" + std::to_string(a_entity.entityId) + ",\"name\":";
            append_json_string(a_json, a_entity.name);
            a_json += ",\"transform\":";
            if (!a_entity.hasTransform)
            {
                a_json += "null}";
                return;
            }

            const EditorMcpTransformSnapshot& transform = a_entity.transform;
            a_json +=
                "{\"position\":{\"x\":" + std::to_string(transform.positionX) +
                ",\"y\":" + std::to_string(transform.positionY) +
                ",\"z\":" + std::to_string(transform.positionZ) +
                "},\"rotation\":{\"x\":" + std::to_string(transform.rotationX) +
                ",\"y\":" + std::to_string(transform.rotationY) +
                ",\"z\":" + std::to_string(transform.rotationZ) +
                ",\"w\":" + std::to_string(transform.rotationW) +
                "},\"scale\":{\"x\":" + std::to_string(transform.scaleX) +
                ",\"y\":" + std::to_string(transform.scaleY) +
                ",\"z\":" + std::to_string(transform.scaleZ) + "}}}";
        }

        void send_response(SOCKET a_socket, int a_statusCode, std::string_view a_body) noexcept
        {
            const char* statusText = a_statusCode == 200 ? "OK" : "Not Found";
            const std::string response =
                "HTTP/1.1 " + std::to_string(a_statusCode) + " " + statusText + "\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: " + std::to_string(a_body.size()) + "\r\n"
                "Connection: close\r\n\r\n" + std::string(a_body);
            const char* data = response.data();
            size_t remaining = response.size();
            while (remaining > 0u)
            {
                const int sent = ::send(a_socket, data, static_cast<int>(remaining), 0);
                if (sent <= 0)
                {
                    return;
                }
                data += sent;
                remaining -= static_cast<size_t>(sent);
            }
        }

        [[nodiscard]] bool receive_request(SOCKET a_socket, std::string& a_outRequest) noexcept
        {
            constexpr size_t k_maxRequestSize = 4096u;
            constexpr uint32_t k_maxReceiveWaitCount = 100u;
            constexpr std::string_view k_headerEnd = "\r\n\r\n";
            constexpr std::string_view k_contentLength = "content-length:";
            constexpr char k_continueResponse[] = "HTTP/1.1 100 Continue\r\n\r\n";

            std::array<char, 1024u> buffer{};
            bool sentContinue = false;
            uint32_t receiveWaitCount = 0u;
            while (a_outRequest.size() < k_maxRequestSize)
            {
                const int received = ::recv(
                    a_socket, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (received == SOCKET_ERROR)
                {
                    if (::WSAGetLastError() == WSAEWOULDBLOCK &&
                        receiveWaitCount < k_maxReceiveWaitCount)
                    {
                        ++receiveWaitCount;
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    }
                    return false;
                }
                if (received == 0)
                {
                    return false;
                }

                receiveWaitCount = 0u;

                a_outRequest.append(buffer.data(), static_cast<size_t>(received));
                const size_t headerEnd = a_outRequest.find(k_headerEnd);
                if (headerEnd == std::string::npos)
                {
                    continue;
                }

                // HTTP header 名は大小文字を区別しないため、受信済み範囲だけ小文字化して
                // Node fetch と Windows HTTP client のどちらでも本文の終端を待つ。
                // headerEnd は空行の先頭を指すため、最後の header 行を終端する CRLF まで含める。
                std::string lowerCaseHeaders = a_outRequest.substr(0u, headerEnd + 2u);
                for (char& character : lowerCaseHeaders)
                {
                    if (character >= 'A' && character <= 'Z')
                    {
                        character = static_cast<char>(character - 'A' + 'a');
                    }
                }

                const size_t contentLengthStart = lowerCaseHeaders.find(k_contentLength);
                if (contentLengthStart == std::string::npos)
                {
                    return true;
                }

                const size_t valueStart = contentLengthStart + k_contentLength.size();
                size_t firstValueCharacter = valueStart;
                while (firstValueCharacter < lowerCaseHeaders.size() &&
                       lowerCaseHeaders[firstValueCharacter] == ' ')
                {
                    ++firstValueCharacter;
                }
                const size_t valueEnd = lowerCaseHeaders.find("\r\n", firstValueCharacter);
                if (valueEnd == std::string::npos)
                {
                    return false;
                }

                size_t contentLength = 0u;
                for (size_t index = firstValueCharacter; index < valueEnd; ++index)
                {
                    const char character = lowerCaseHeaders[index];
                    if (character < '0' || character > '9')
                    {
                        return false;
                    }
                    contentLength = contentLength * 10u +
                                    static_cast<size_t>(character - '0');
                }

                const size_t bodyStart = headerEnd + k_headerEnd.size();
                if (contentLength > k_maxRequestSize - bodyStart)
                {
                    return false;
                }
                if (a_outRequest.size() >= bodyStart + contentLength)
                {
                    return true;
                }

                if (!sentContinue &&
                    lowerCaseHeaders.find("expect: 100-continue") != std::string::npos)
                {
                    (void)::send(
                        a_socket, k_continueResponse,
                        static_cast<int>(sizeof(k_continueResponse) - 1u), 0);
                    sentContinue = true;
                }
            }

            return false;
        }
    } // namespace

    EditorMcpBridge::~EditorMcpBridge()
    {
        shutdown();
    }

    Result EditorMcpBridge::start(const EditorMcpBridgeSetupInfo& a_info)
    {
        if (m_isRunning.load())
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                                "CueEditor MCP bridge is already running.");
        }

        WSADATA data{};
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            return Result::fail(Code::InitializeFailed, Severity::Error,
                                "CueEditor MCP bridge WinSock initialization failed.");
        }
        m_isWinSockInitialized = true;

        const SOCKET listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == INVALID_SOCKET)
        {
            shutdown();
            return Result::fail(Code::CreateFailed, Severity::Error,
                                "CueEditor MCP bridge socket creation failed.");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(a_info.port);
        if (::bind(listenSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
            ::listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
        {
            ::closesocket(listenSocket);
            shutdown();
            return Result::fail(Code::InitializeFailed, Severity::Error,
                                "CueEditor MCP bridge listener startup failed.");
        }

        u_long nonBlocking = 1u;
        if (::ioctlsocket(listenSocket, FIONBIO, &nonBlocking) == SOCKET_ERROR)
        {
            ::closesocket(listenSocket);
            shutdown();
            return Result::fail(Code::InitializeFailed, Severity::Error,
                                "CueEditor MCP bridge listener configuration failed.");
        }

        m_listenSocket = static_cast<uintptr_t>(listenSocket);
        m_isRunning.store(true);
        m_thread = std::thread(&EditorMcpBridge::run, this);
        return Result::ok();
    }

    void EditorMcpBridge::shutdown() noexcept
    {
        if (!m_isRunning.exchange(false))
        {
            if (m_isWinSockInitialized)
            {
                ::WSACleanup();
                m_isWinSockInitialized = false;
            }
            return;
        }

        const SOCKET listenSocket = static_cast<SOCKET>(m_listenSocket);
        if (m_listenSocket != k_invalidSocket)
        {
            (void)::shutdown(listenSocket, SD_BOTH);
        }
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        if (m_listenSocket != k_invalidSocket)
        {
            (void)::closesocket(listenSocket);
            m_listenSocket = k_invalidSocket;
        }
        if (m_isWinSockInitialized)
        {
            ::WSACleanup();
            m_isWinSockInitialized = false;
        }
    }

    EditorMcpPlaybackRequest EditorMcpBridge::consume_playback_request() noexcept
    {
        return m_playbackRequest.exchange(EditorMcpPlaybackRequest::none);
    }

    void EditorMcpBridge::set_playback_state(EditorMcpPlaybackState a_state) noexcept
    {
        m_playbackState.store(a_state);
    }

    bool EditorMcpBridge::consume_script_open_request(std::string& a_outAssetPath) noexcept
    {
        std::scoped_lock lock(m_scriptOpenRequestMutex);
        if (m_scriptOpenRequestPath.empty())
        {
            return false;
        }

        a_outAssetPath = std::move(m_scriptOpenRequestPath);
        return true;
    }

    void EditorMcpBridge::set_script_open_state(EditorMcpScriptOpenState a_state) noexcept
    {
        m_scriptOpenState.store(a_state);
    }

    void EditorMcpBridge::set_entity_snapshot(
        std::vector<EditorMcpEntitySnapshot> a_entities)
    {
        std::scoped_lock lock(m_entitySnapshotMutex);
        m_entitySnapshots = std::move(a_entities);
    }

    void EditorMcpBridge::run() noexcept
    {
        const SOCKET listenSocket = static_cast<SOCKET>(m_listenSocket);
        while (m_isRunning.load())
        {
            const SOCKET connection = ::accept(listenSocket, nullptr, nullptr);
            if (connection == INVALID_SOCKET)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            handle_connection(static_cast<uintptr_t>(connection));
            (void)::closesocket(connection);
        }
    }

    void EditorMcpBridge::handle_connection(uintptr_t a_socket) noexcept
    {
        const SOCKET connection = static_cast<SOCKET>(a_socket);
        std::string request{};
        if (!receive_request(connection, request))
        {
            return;
        }

        if (request.starts_with("GET /health "))
        {
            const std::string body =
                "{\"status\":\"ok\",\"playback\":\"" +
                std::string(playback_state_name(m_playbackState.load())) +
                "\",\"scriptOpen\":\"" +
                std::string(script_open_state_name(m_scriptOpenState.load())) + "\"}";
            send_response(connection, 200, body);
            return;
        }
        if (request.starts_with("GET /entities?") ||
            request.starts_with("GET /entities "))
        {
            const size_t targetStart = sizeof("GET ") - 1u;
            const size_t targetEnd = request.find(' ', targetStart);
            if (targetEnd == std::string::npos)
            {
                send_response(connection, 404, "{\"error\":\"Request target is invalid.\"}");
                return;
            }
            const std::string_view requestTarget(
                request.data() + targetStart, targetEnd - targetStart);
            const std::string filter = query_value(requestTarget, "filter");
            const size_t limit = entity_limit(requestTarget);
            std::vector<EditorMcpEntitySnapshot> entities{};
            {
                std::scoped_lock lock(m_entitySnapshotMutex);
                entities = m_entitySnapshots;
            }

            std::string body{"{\"entities\":["};
            size_t appendedCount = 0u;
            for (const EditorMcpEntitySnapshot& entity : entities)
            {
                if (appendedCount >= limit ||
                    !contains_ascii_case_insensitive(entity.name, filter))
                {
                    continue;
                }
                if (appendedCount > 0u)
                {
                    body.push_back(',');
                }
                append_entity_json(body, entity);
                ++appendedCount;
            }
            body += "],\"count\":" + std::to_string(appendedCount) + "}";
            send_response(connection, 200, body);
            return;
        }
        if (request.starts_with("GET /entities/"))
        {
            constexpr size_t k_entityIdStart = sizeof("GET /entities/") - 1u;
            const size_t entityIdEnd = request.find(' ', k_entityIdStart);
            if (entityIdEnd == std::string::npos)
            {
                send_response(connection, 404, "{\"error\":\"Entity id is invalid.\"}");
                return;
            }
            uint64_t entityId = 0u;
            const auto [end, error] = std::from_chars(
                request.data() + k_entityIdStart,
                request.data() + entityIdEnd,
                entityId);
            if (error != std::errc{} || end != request.data() + entityIdEnd)
            {
                send_response(connection, 404, "{\"error\":\"Entity id is invalid.\"}");
                return;
            }

            std::scoped_lock lock(m_entitySnapshotMutex);
            for (const EditorMcpEntitySnapshot& entity : m_entitySnapshots)
            {
                if (entity.entityId != entityId)
                {
                    continue;
                }

                std::string body{};
                append_entity_json(body, entity);
                send_response(connection, 200, body);
                return;
            }
            send_response(connection, 404, "{\"error\":\"Entity was not found.\"}");
            return;
        }
        if (request.starts_with("POST /playback "))
        {
            if (request.find("\"state\":\"play\"") != std::string_view::npos)
            {
                m_playbackRequest.store(EditorMcpPlaybackRequest::play);
            }
            else if (request.find("\"state\":\"pause\"") != std::string_view::npos)
            {
                m_playbackRequest.store(EditorMcpPlaybackRequest::pause);
            }
            else if (request.find("\"state\":\"step\"") != std::string_view::npos)
            {
                m_playbackRequest.store(EditorMcpPlaybackRequest::step);
            }
            else
            {
                send_response(connection, 404, "{\"error\":\"Unsupported playback state.\"}");
                return;
            }

            send_response(connection, 200, "{\"accepted\":true}");
            return;
        }
        if (request.starts_with("POST /asset/open-script "))
        {
            std::string assetPath{};
            if (!extract_asset_path(request, assetPath))
            {
                send_response(connection, 404, "{\"error\":\"Script asset path is invalid.\"}");
                return;
            }

            {
                std::scoped_lock lock(m_scriptOpenRequestMutex);
                m_scriptOpenRequestPath = std::move(assetPath);
            }
            m_scriptOpenState.store(EditorMcpScriptOpenState::pending);
            send_response(connection, 200, "{\"accepted\":true}");
            return;
        }

        send_response(connection, 404, "{\"error\":\"Route not found.\"}");
    }
} // namespace Cue::Editor
