#include <Windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef CUE_UPDATER_DEFAULT_REPOSITORY
#define CUE_UPDATER_DEFAULT_REPOSITORY "sinse/CueEngine"
#endif
#define CUE_UPDATER_WIDE_TEXT_INNER(a_value) L##a_value
#define CUE_UPDATER_WIDE_TEXT(a_value) CUE_UPDATER_WIDE_TEXT_INNER(a_value)

namespace
{
    struct Result final
    {
        bool ok = true;
        std::wstring message{};

        static Result success()
        {
            return {};
        }

        static Result failure(std::wstring a_message)
        {
            return Result{ false, std::move(a_message) };
        }
    };

    struct Options final
    {
        std::wstring repository =
            CUE_UPDATER_WIDE_TEXT(CUE_UPDATER_DEFAULT_REPOSITORY);
        std::filesystem::path installDirectory{};
        bool shouldLaunchEditor = true;
    };

    struct ReleaseInfo final
    {
        std::string version{};
        std::string tag{};
        std::string manifestUrl{};
        std::string packageUrl{};
        std::string packageSha256{};
        std::string packageName{};
    };

    struct InstalledInfo final
    {
        bool isInstalled = false;
        std::string version{};
    };

    [[nodiscard]] std::wstring utf8_to_wide(std::string_view a_text)
    {
        if (a_text.empty())
        {
            return {};
        }

        const int size = MultiByteToWideChar(CP_UTF8, 0, a_text.data(),
            static_cast<int>(a_text.size()), nullptr, 0);
        if (size <= 0)
        {
            return {};
        }

        std::wstring result(static_cast<size_t>(size), L'\0');
        (void)MultiByteToWideChar(CP_UTF8, 0, a_text.data(),
            static_cast<int>(a_text.size()), result.data(), size);
        return result;
    }

    [[nodiscard]] std::string wide_to_utf8(std::wstring_view a_text)
    {
        if (a_text.empty())
        {
            return {};
        }

        const int size = WideCharToMultiByte(CP_UTF8, 0, a_text.data(),
            static_cast<int>(a_text.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return {};
        }

        std::string result(static_cast<size_t>(size), '\0');
        (void)WideCharToMultiByte(CP_UTF8, 0, a_text.data(),
            static_cast<int>(a_text.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    [[nodiscard]] std::filesystem::path executable_directory()
    {
        std::array<wchar_t, MAX_PATH> buffer{};
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
        {
            return std::filesystem::current_path();
        }

        return std::filesystem::path(std::wstring(buffer.data(), length))
            .parent_path();
    }

    [[nodiscard]] std::wstring last_error_message(std::wstring_view a_prefix)
    {
        const DWORD error = GetLastError();
        wchar_t* message = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<wchar_t*>(&message),
            0,
            nullptr);

        std::wstring result(a_prefix);
        result += L" ";
        if (length > 0 && message != nullptr)
        {
            result.append(message, length);
            LocalFree(message);
        }
        else
        {
            result += L"error=" + std::to_wstring(error);
        }

        return result;
    }

    void show_error(std::wstring_view a_message)
    {
        MessageBoxW(nullptr, std::wstring(a_message).c_str(), L"Cue Updater",
            MB_OK | MB_ICONERROR);
    }

    void show_info(std::wstring_view a_message)
    {
        MessageBoxW(nullptr, std::wstring(a_message).c_str(), L"Cue Updater",
            MB_OK | MB_ICONINFORMATION);
    }

    [[nodiscard]] bool ask_yes_no(std::wstring_view a_message)
    {
        return MessageBoxW(nullptr, std::wstring(a_message).c_str(), L"Cue Updater",
                   MB_YESNO | MB_ICONQUESTION) == IDYES;
    }

    [[nodiscard]] std::string read_text_file(const std::filesystem::path& a_path)
    {
        std::ifstream file(a_path, std::ios::binary);
        if (!file)
        {
            return {};
        }

        std::ostringstream stream{};
        stream << file.rdbuf();
        return stream.str();
    }

    [[nodiscard]] std::string unescape_json_string(std::string_view a_text)
    {
        std::string result{};
        result.reserve(a_text.size());
        for (size_t index = 0; index < a_text.size(); ++index)
        {
            const char c = a_text[index];
            if (c != '\\' || index + 1 >= a_text.size())
            {
                result.push_back(c);
                continue;
            }

            const char escaped = a_text[++index];
            switch (escaped)
            {
            case '"':
            case '\\':
            case '/':
                result.push_back(escaped);
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                result.push_back(escaped);
                break;
            }
        }

        return result;
    }

    [[nodiscard]] std::optional<std::string> find_json_string(
        std::string_view a_json,
        std::string_view a_key)
    {
        const std::string pattern = "\"" + std::string(a_key) + "\"";
        size_t keyPosition = a_json.find(pattern);
        if (keyPosition == std::string_view::npos)
        {
            return std::nullopt;
        }

        size_t colonPosition = a_json.find(':', keyPosition + pattern.size());
        if (colonPosition == std::string_view::npos)
        {
            return std::nullopt;
        }

        size_t valueStart = a_json.find('"', colonPosition + 1);
        if (valueStart == std::string_view::npos)
        {
            return std::nullopt;
        }
        ++valueStart;

        bool isEscaped = false;
        for (size_t index = valueStart; index < a_json.size(); ++index)
        {
            const char c = a_json[index];
            if (isEscaped)
            {
                isEscaped = false;
                continue;
            }
            if (c == '\\')
            {
                isEscaped = true;
                continue;
            }
            if (c == '"')
            {
                return unescape_json_string(a_json.substr(valueStart, index - valueStart));
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] std::vector<std::string> find_browser_download_urls(
        std::string_view a_json)
    {
        std::vector<std::string> urls{};
        size_t searchPosition = 0;
        while (searchPosition < a_json.size())
        {
            const size_t keyPosition =
                a_json.find("\"browser_download_url\"", searchPosition);
            if (keyPosition == std::string_view::npos)
            {
                break;
            }

            const std::string_view rest = a_json.substr(keyPosition);
            if (std::optional<std::string> url =
                    find_json_string(rest, "browser_download_url");
                url.has_value())
            {
                urls.push_back(*url);
            }

            searchPosition = keyPosition + 24;
        }

        return urls;
    }

    [[nodiscard]] bool ends_with(std::string_view a_value, std::string_view a_suffix)
    {
        return a_value.size() >= a_suffix.size() &&
            a_value.substr(a_value.size() - a_suffix.size()) == a_suffix;
    }

    [[nodiscard]] bool contains(std::string_view a_value, std::string_view a_needle)
    {
        return a_value.find(a_needle) != std::string_view::npos;
    }

    [[nodiscard]] std::vector<int> parse_version_numbers(std::string_view a_version)
    {
        std::vector<int> numbers{};
        int current = 0;
        bool hasNumber = false;
        for (const char c : a_version)
        {
            if (c >= '0' && c <= '9')
            {
                current = current * 10 + (c - '0');
                hasNumber = true;
                continue;
            }

            if (hasNumber)
            {
                numbers.push_back(current);
                current = 0;
                hasNumber = false;
            }
        }

        if (hasNumber)
        {
            numbers.push_back(current);
        }

        return numbers;
    }

    [[nodiscard]] int compare_versions(
        std::string_view a_left,
        std::string_view a_right)
    {
        std::vector<int> left = parse_version_numbers(a_left);
        std::vector<int> right = parse_version_numbers(a_right);
        const size_t count = (std::max)(left.size(), right.size());
        left.resize(count, 0);
        right.resize(count, 0);

        for (size_t index = 0; index < count; ++index)
        {
            if (left[index] < right[index])
            {
                return -1;
            }
            if (left[index] > right[index])
            {
                return 1;
            }
        }

        return 0;
    }

    [[nodiscard]] std::wstring make_api_url(std::wstring_view a_repository)
    {
        return L"https://api.github.com/repos/" + std::wstring(a_repository) +
            L"/releases/latest";
    }

    [[nodiscard]] Result parse_url(
        std::wstring_view a_url,
        URL_COMPONENTS& a_components,
        std::wstring& a_host,
        std::wstring& a_path)
    {
        std::wstring url(a_url);
        std::array<wchar_t, 2048> hostBuffer{};
        std::array<wchar_t, 8192> pathBuffer{};

        ZeroMemory(&a_components, sizeof(a_components));
        a_components.dwStructSize = sizeof(a_components);
        a_components.lpszHostName = hostBuffer.data();
        a_components.dwHostNameLength = static_cast<DWORD>(hostBuffer.size());
        a_components.lpszUrlPath = pathBuffer.data();
        a_components.dwUrlPathLength = static_cast<DWORD>(pathBuffer.size());
        a_components.dwSchemeLength = static_cast<DWORD>(-1);
        a_components.dwExtraInfoLength = static_cast<DWORD>(-1);

        if (!WinHttpCrackUrl(
                url.c_str(), static_cast<DWORD>(url.size()), 0, &a_components))
        {
            return Result::failure(last_error_message(L"URL の解析に失敗しました。"));
        }

        a_host.assign(
            a_components.lpszHostName,
            a_components.dwHostNameLength);
        a_path.assign(
            a_components.lpszUrlPath,
            a_components.dwUrlPathLength);
        if (a_components.lpszExtraInfo != nullptr &&
            a_components.dwExtraInfoLength > 0)
        {
            a_path.append(
                a_components.lpszExtraInfo,
                a_components.dwExtraInfoLength);
        }

        return Result::success();
    }

    [[nodiscard]] Result winhttp_get(
        std::wstring_view a_url,
        std::vector<std::uint8_t>& a_outBytes)
    {
        a_outBytes.clear();

        URL_COMPONENTS components{};
        std::wstring host{};
        std::wstring path{};
        Result parseResult = parse_url(a_url, components, host, path);
        if (!parseResult.ok)
        {
            return parseResult;
        }

        HINTERNET session = WinHttpOpen(
            L"CueUpdater/0.1",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (session == nullptr)
        {
            return Result::failure(last_error_message(L"HTTP session を開始できません。"));
        }

        const DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        (void)WinHttpSetOption(
            session,
            WINHTTP_OPTION_REDIRECT_POLICY,
            const_cast<DWORD*>(&redirectPolicy),
            sizeof(redirectPolicy));

        HINTERNET connection = WinHttpConnect(
            session,
            host.c_str(),
            components.nPort,
            0);
        if (connection == nullptr)
        {
            WinHttpCloseHandle(session);
            return Result::failure(last_error_message(L"HTTP connection を開始できません。"));
        }

        const DWORD flags =
            components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET request = WinHttpOpenRequest(
            connection,
            L"GET",
            path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags);
        if (request == nullptr)
        {
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return Result::failure(last_error_message(L"HTTP request を作成できません。"));
        }

        const wchar_t* headers =
            L"Accept: application/vnd.github+json\r\n"
            L"X-GitHub-Api-Version: 2022-11-28\r\n";
        if (!WinHttpSendRequest(
                request,
                headers,
                static_cast<DWORD>(-1),
                WINHTTP_NO_REQUEST_DATA,
                0,
                0,
                0) ||
            !WinHttpReceiveResponse(request, nullptr))
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return Result::failure(last_error_message(L"HTTP request に失敗しました。"));
        }

        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusCodeSize,
                WINHTTP_NO_HEADER_INDEX) &&
            (statusCode < 200 || statusCode >= 300))
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return Result::failure(
                L"GitHub からエラーが返りました。status=" +
                std::to_wstring(statusCode));
        }

        for (;;)
        {
            DWORD availableSize = 0;
            if (!WinHttpQueryDataAvailable(request, &availableSize))
            {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                return Result::failure(last_error_message(L"HTTP response を読めません。"));
            }
            if (availableSize == 0)
            {
                break;
            }

            const size_t previousSize = a_outBytes.size();
            a_outBytes.resize(previousSize + availableSize);

            DWORD readSize = 0;
            if (!WinHttpReadData(
                    request,
                    a_outBytes.data() + previousSize,
                    availableSize,
                    &readSize))
            {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                return Result::failure(last_error_message(L"HTTP response の読み込みに失敗しました。"));
            }
            a_outBytes.resize(previousSize + readSize);
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return Result::success();
    }

    [[nodiscard]] Result download_text(
        std::wstring_view a_url,
        std::string& a_outText)
    {
        std::vector<std::uint8_t> bytes{};
        Result result = winhttp_get(a_url, bytes);
        if (!result.ok)
        {
            return result;
        }

        a_outText.assign(
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size());
        return Result::success();
    }

    [[nodiscard]] Result download_file(
        std::wstring_view a_url,
        const std::filesystem::path& a_destination)
    {
        std::vector<std::uint8_t> bytes{};
        Result result = winhttp_get(a_url, bytes);
        if (!result.ok)
        {
            return result;
        }

        std::filesystem::create_directories(a_destination.parent_path());
        std::ofstream file(a_destination, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            return Result::failure(L"ダウンロード先ファイルを作成できません。");
        }

        file.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        return file ? Result::success()
                    : Result::failure(L"ダウンロード先ファイルへ書き込めません。");
    }

    [[nodiscard]] std::string bytes_to_hex(
        const std::vector<std::uint8_t>& a_bytes)
    {
        static constexpr char k_hex[] = "0123456789abcdef";

        std::string result{};
        result.reserve(a_bytes.size() * 2);
        for (const std::uint8_t byte : a_bytes)
        {
            result.push_back(k_hex[(byte >> 4) & 0x0f]);
            result.push_back(k_hex[byte & 0x0f]);
        }

        return result;
    }

    [[nodiscard]] Result calculate_sha256(
        const std::filesystem::path& a_path,
        std::string& a_outHash)
    {
        a_outHash.clear();

        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptOpenAlgorithmProvider(
                &algorithm,
                BCRYPT_SHA256_ALGORITHM,
                nullptr,
                0) != 0)
        {
            return Result::failure(L"SHA256 provider を開始できません。");
        }

        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) != 0)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return Result::failure(L"SHA256 hash を作成できません。");
        }

        std::ifstream file(a_path, std::ios::binary);
        if (!file)
        {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return Result::failure(L"SHA256 対象ファイルを開けません。");
        }

        std::array<char, 1024 * 1024> buffer{};
        while (file)
        {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize readSize = file.gcount();
            if (readSize <= 0)
            {
                continue;
            }

            if (BCryptHashData(
                    hash,
                    reinterpret_cast<PUCHAR>(buffer.data()),
                    static_cast<ULONG>(readSize),
                    0) != 0)
            {
                BCryptDestroyHash(hash);
                BCryptCloseAlgorithmProvider(algorithm, 0);
                return Result::failure(L"SHA256 の計算に失敗しました。");
            }
        }

        std::vector<std::uint8_t> digest(32);
        if (BCryptFinishHash(
                hash,
                digest.data(),
                static_cast<ULONG>(digest.size()),
                0) != 0)
        {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return Result::failure(L"SHA256 の確定に失敗しました。");
        }

        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        a_outHash = bytes_to_hex(digest);
        return Result::success();
    }

    [[nodiscard]] Result query_latest_release(
        std::wstring_view a_repository,
        ReleaseInfo& a_outRelease)
    {
        a_outRelease = {};

        std::string releaseJson{};
        Result result = download_text(make_api_url(a_repository), releaseJson);
        if (!result.ok)
        {
            return result;
        }

        a_outRelease.tag = find_json_string(releaseJson, "tag_name").value_or("");

        const std::vector<std::string> urls =
            find_browser_download_urls(releaseJson);
        for (const std::string& url : urls)
        {
            if (contains(url, "CueEngineEditor_") &&
                ends_with(url, "_win-x64.json"))
            {
                a_outRelease.manifestUrl = url;
            }
            else if (contains(url, "CueEngineEditor_") &&
                ends_with(url, "_win-x64.zip"))
            {
                a_outRelease.packageUrl = url;
            }
        }

        if (a_outRelease.manifestUrl.empty())
        {
            return Result::failure(L"Release manifest が見つかりません。");
        }

        std::string manifestJson{};
        result = download_text(utf8_to_wide(a_outRelease.manifestUrl), manifestJson);
        if (!result.ok)
        {
            return result;
        }

        a_outRelease.version =
            find_json_string(manifestJson, "version").value_or("");
        a_outRelease.packageSha256 =
            find_json_string(manifestJson, "sha256").value_or("");
        a_outRelease.packageName =
            find_json_string(manifestJson, "assetName").value_or("");

        if (std::optional<std::string> packageUrl =
                find_json_string(manifestJson, "downloadUrl");
            packageUrl.has_value() && !packageUrl->empty())
        {
            a_outRelease.packageUrl = *packageUrl;
        }

        if (a_outRelease.version.empty())
        {
            a_outRelease.version = a_outRelease.tag;
            if (!a_outRelease.version.empty() && a_outRelease.version.front() == 'v')
            {
                a_outRelease.version.erase(a_outRelease.version.begin());
            }
        }

        if (a_outRelease.packageUrl.empty() ||
            a_outRelease.packageSha256.empty() ||
            a_outRelease.version.empty())
        {
            return Result::failure(L"Release manifest の内容が不足しています。");
        }

        return Result::success();
    }

    [[nodiscard]] InstalledInfo read_installed_info(
        const std::filesystem::path& a_installDirectory)
    {
        InstalledInfo info{};
        info.isInstalled =
            std::filesystem::exists(a_installDirectory / L"Editor" / L"Editor.exe") ||
            std::filesystem::exists(a_installDirectory / L"Sdk");

        const std::string versionJson =
            read_text_file(a_installDirectory / L"version.json");
        if (std::optional<std::string> version =
                find_json_string(versionJson, "version");
            version.has_value())
        {
            info.version = *version;
        }

        return info;
    }

    [[nodiscard]] Result run_process_and_wait(std::wstring a_commandLine)
    {
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        std::vector<wchar_t> commandBuffer(
            a_commandLine.begin(), a_commandLine.end());
        commandBuffer.push_back(L'\0');

        if (!CreateProcessW(
                nullptr,
                commandBuffer.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &startupInfo,
                &processInfo))
        {
            return Result::failure(last_error_message(L"外部プロセスを開始できません。"));
        }

        WaitForSingleObject(processInfo.hProcess, INFINITE);
        DWORD exitCode = 1;
        (void)GetExitCodeProcess(processInfo.hProcess, &exitCode);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);

        return exitCode == 0
            ? Result::success()
            : Result::failure(L"外部プロセスが失敗しました。exitCode=" +
                std::to_wstring(exitCode));
    }

    [[nodiscard]] Result extract_zip(
        const std::filesystem::path& a_zipPath,
        const std::filesystem::path& a_extractDirectory)
    {
        std::filesystem::remove_all(a_extractDirectory);
        std::filesystem::create_directories(a_extractDirectory);

        const std::wstring command =
            L"tar.exe -xf \"" + a_zipPath.wstring() + L"\" -C \"" +
            a_extractDirectory.wstring() + L"\"";
        return run_process_and_wait(command);
    }

    [[nodiscard]] std::wstring timestamp_text()
    {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        tm localTime{};
        (void)localtime_s(&localTime, &time);

        wchar_t buffer[32]{};
        (void)wcsftime(buffer, 32, L"%Y%m%d_%H%M%S", &localTime);
        return buffer;
    }

    [[nodiscard]] std::vector<std::filesystem::path> install_root_entries()
    {
        return {
            std::filesystem::path(L"Editor"),
            std::filesystem::path(L"Sdk"),
            std::filesystem::path(L"version.json")
        };
    }

    [[nodiscard]] Result install_package(
        const std::filesystem::path& a_installDirectory,
        const std::filesystem::path& a_extractDirectory)
    {
        const std::filesystem::path backupDirectory =
            a_installDirectory / L".cue_update_backup" / timestamp_text();

        try
        {
            std::filesystem::create_directories(a_installDirectory);
            std::filesystem::create_directories(backupDirectory);

            for (const std::filesystem::path& entry : install_root_entries())
            {
                const std::filesystem::path destination = a_installDirectory / entry;
                if (!std::filesystem::exists(destination))
                {
                    continue;
                }

                std::filesystem::rename(destination, backupDirectory / entry);
            }

            for (const std::filesystem::path& entry : install_root_entries())
            {
                const std::filesystem::path source = a_extractDirectory / entry;
                if (!std::filesystem::exists(source))
                {
                    continue;
                }

                const std::filesystem::path destination = a_installDirectory / entry;
                if (std::filesystem::is_directory(source))
                {
                    std::filesystem::copy(
                        source,
                        destination,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing);
                }
                else
                {
                    std::filesystem::copy_file(
                        source,
                        destination,
                        std::filesystem::copy_options::overwrite_existing);
                }
            }
        }
        catch (const std::exception&)
        {
            for (const std::filesystem::path& entry : install_root_entries())
            {
                std::error_code error{};
                std::filesystem::remove_all(a_installDirectory / entry, error);

                const std::filesystem::path backup = backupDirectory / entry;
                if (std::filesystem::exists(backup))
                {
                    std::filesystem::rename(backup, a_installDirectory / entry, error);
                }
            }

            return Result::failure(L"インストールに失敗したため、可能な範囲で復元しました。");
        }

        return Result::success();
    }

    [[nodiscard]] Options parse_options()
    {
        Options options{};
        options.installDirectory = executable_directory();

        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv == nullptr)
        {
            return options;
        }

        for (int index = 1; index < argc; ++index)
        {
            const std::wstring arg = argv[index];
            if (arg == L"--repo" && index + 1 < argc)
            {
                options.repository = argv[++index];
            }
            else if (arg == L"--install-dir" && index + 1 < argc)
            {
                options.installDirectory = argv[++index];
            }
            else if (arg == L"--no-launch")
            {
                options.shouldLaunchEditor = false;
            }
        }

        LocalFree(argv);
        return options;
    }

    [[nodiscard]] Result perform_update(const Options& a_options)
    {
        ReleaseInfo latest{};
        Result result = query_latest_release(a_options.repository, latest);
        if (!result.ok)
        {
            return result;
        }

        const InstalledInfo installed =
            read_installed_info(a_options.installDirectory);
        if (!installed.isInstalled)
        {
            const std::wstring message =
                L"Cue Engine Editor が見つかりません。\n" +
                a_options.installDirectory.wstring() +
                L"\n\n最新版 " + utf8_to_wide(latest.version) +
                L" をインストールしますか？";
            if (!ask_yes_no(message))
            {
                return Result::success();
            }
        }
        else if (compare_versions(installed.version, latest.version) >= 0)
        {
            show_info(
                L"Cue Engine Editor は最新です。\n現在: " +
                utf8_to_wide(installed.version) +
                L"\n最新: " + utf8_to_wide(latest.version));
            return Result::success();
        }
        else
        {
            const std::wstring message =
                L"Cue Engine Editor の更新があります。\n現在: " +
                utf8_to_wide(installed.version) +
                L"\n最新: " + utf8_to_wide(latest.version) +
                L"\n\n更新しますか？\nEditor を起動中の場合は閉じてください。";
            if (!ask_yes_no(message))
            {
                return Result::success();
            }
        }

        const std::filesystem::path workDirectory =
            a_options.installDirectory / L".cue_update";
        const std::filesystem::path zipPath = workDirectory /
            (latest.packageName.empty()
                    ? std::filesystem::path(L"CueEngineEditor.zip")
                    : std::filesystem::path(utf8_to_wide(latest.packageName)));
        const std::filesystem::path extractDirectory = workDirectory / L"extract";

        std::filesystem::remove_all(workDirectory);
        std::filesystem::create_directories(workDirectory);

        result = download_file(utf8_to_wide(latest.packageUrl), zipPath);
        if (!result.ok)
        {
            return result;
        }

        std::string actualHash{};
        result = calculate_sha256(zipPath, actualHash);
        if (!result.ok)
        {
            return result;
        }
        if (actualHash != latest.packageSha256)
        {
            return Result::failure(L"ダウンロードした package の SHA256 が一致しません。");
        }

        result = extract_zip(zipPath, extractDirectory);
        if (!result.ok)
        {
            return result;
        }

        result = install_package(a_options.installDirectory, extractDirectory);
        if (!result.ok)
        {
            return result;
        }

        std::filesystem::remove_all(workDirectory);
        show_info(L"Cue Engine Editor を更新しました。");

        if (a_options.shouldLaunchEditor &&
            std::filesystem::exists(
                a_options.installDirectory / L"Editor" / L"Editor.exe") &&
            ask_yes_no(L"Editor を起動しますか？"))
        {
            const std::filesystem::path editorPath =
                a_options.installDirectory / L"Editor" / L"Editor.exe";
            ShellExecuteW(nullptr, L"open", editorPath.c_str(), nullptr,
                editorPath.parent_path().c_str(), SW_SHOWNORMAL);
        }

        return Result::success();
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        const Options options = parse_options();
        const Result result = perform_update(options);
        if (!result.ok)
        {
            show_error(result.message);
            return 1;
        }

        return 0;
    }
    catch (const std::exception& exception)
    {
        show_error(utf8_to_wide(exception.what()));
        return 1;
    }
}
