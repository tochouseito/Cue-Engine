#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Foundation/Windows/UtfConversion.h>
#include <Cue/IO/Filesystem.h>
#include <Cue/ProjectHub/Windows/WindowsProjectHubPlatform.h>

#include <Windows.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class TestFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief 回復不能なTest失敗を固定Exit Codeへ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(75);
    }

    /// @brief Message付きTest失敗を固定Exit Codeへ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(76);
    }
};

class TestDirectory final
{
  public:
    /// @brief Project Hub Windows Adapter専用の一意な一時Rootを作成する
    TestDirectory()
    {
        std::array<wchar_t, MAX_PATH> temporary{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
        if (length == 0 || length >= temporary.size())
        {
            return;
        }
        m_root = temporary.data();
        m_root += L"CueProjectHubWindowsTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                  std::to_wstring(GetTickCount64());
        m_project = m_root + L"\\SampleGame";
        m_isCreated = CreateDirectoryW(m_root.c_str(), nullptr) != FALSE;
    }

    /// @brief Test Directoryの一意Cleanup責務を保つためCopy構築を禁止する
    TestDirectory(const TestDirectory &) = delete;
    /// @brief Test Directoryの一意Cleanup責務を保つためCopy代入を禁止する
    TestDirectory &operator=(const TestDirectory &) = delete;

    /// @brief Testが所有する一時ProjectとRootだけを終了時に削除する
    ~TestDirectory()
    {
        if (m_isCreated)
        {
            RemoveDirectoryW(m_project.c_str());
            RemoveDirectoryW(m_root.c_str());
        }
    }

    /// @brief 一時Rootの作成結果を返す
    [[nodiscard]] bool is_created() const noexcept
    {
        return m_isCreated;
    }

    /// @brief 一時RootのUTF-16 Pathを返す
    [[nodiscard]] std::wstring_view root() const noexcept
    {
        return m_root;
    }

    /// @brief Project DirectoryのUTF-16 Pathを返す
    [[nodiscard]] const std::wstring &project() const noexcept
    {
        return m_project;
    }

  private:
    std::wstring m_root;
    std::wstring m_project;
    bool m_isCreated = false;
};

/// @brief Test用Loggerを追加Sinkなしで生成する
[[nodiscard]] std::unique_ptr<cue::Logger> create_logger(TestFatalHandler &a_handler)
{
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    return std::make_unique<cue::Logger>(a_handler, std::move(sinks));
}

/// @brief UTF-16 Test PathをProject Hub入力用UTF-8へ変換する
[[nodiscard]] std::string to_utf8(std::wstring_view a_text, cue::AssertContext &a_context)
{
    std::string converted;
    const cue::WindowsUtfConversionResult result =
        cue::convert_windows_utf16_to_utf8(a_text, converted, a_context.fatal_handler());
    return result.status == cue::WindowsUtfConversionStatus::Success ? std::move(converted) : std::string();
}

/// @brief UUID文字列がVersion 4とRFC Variantを持つか判定する
[[nodiscard]] bool is_version_four_id(std::string_view a_text) noexcept
{
    if (a_text.size() != 36 || a_text[14] != '4')
    {
        return false;
    }
    return a_text[19] == '8' || a_text[19] == '9' || a_text[19] == 'a' || a_text[19] == 'b';
}

/// @brief Windows Project Hub AdapterのLocator、Root、ProjectId契約を検証する
[[nodiscard]] bool test_platform(cue::AssertContext &a_context)
{
    TestDirectory directory;
    if (!directory.is_created())
    {
        return false;
    }
    const std::string root = to_utf8(directory.root(), a_context);
    auto platformResult = cue::project_hub::create_windows_project_hub_platform(a_context);
    if (root.empty() || !platformResult)
    {
        return false;
    }
    std::unique_ptr<cue::project_hub::ProjectHubPlatform> platform = std::move(*platformResult.try_value());
    auto normalized = platform->normalize_project_locator(root);
    auto project = platform->compose_project_locator(root, "SampleGame");
    auto invalidProject = platform->compose_project_locator(root, "../Outside");
    if (!normalized || !project || invalidProject)
    {
        return false;
    }

    auto missing = platform->open_root(*project.try_value());
    if (!missing || *missing.try_value() != nullptr || CreateDirectoryW(directory.project().c_str(), nullptr) == FALSE)
    {
        return false;
    }
    auto opened = platform->open_root(*project.try_value());
    auto descriptor = platform->compose_descriptor_locator(*project.try_value());
    auto firstId = platform->next_project_id();
    auto secondId = platform->next_project_id();
    if (!opened || *opened.try_value() == nullptr || !descriptor || !firstId || !secondId)
    {
        return false;
    }
    opened.try_value()->reset();

    const std::string_view descriptorText = *descriptor.try_value();
    const bool hasDescriptor = descriptorText.ends_with("\\CueProject.json");
    return hasDescriptor && is_version_four_id(firstId.try_value()->text()) &&
           is_version_four_id(secondId.try_value()->text()) &&
           firstId.try_value()->text() != secondId.try_value()->text();
}
} // namespace

/// @brief Project Hub Windows本番Adapterの公開契約をProcess単位で検証する
int main()
{
    TestFatalHandler handler;
    std::unique_ptr<cue::Logger> logger = create_logger(handler);
    cue::AssertContext context(*logger, handler);
    return test_platform(context) ? 0 : 1;
}
