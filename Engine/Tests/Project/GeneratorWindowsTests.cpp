#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/IO/Windows/WindowsFilesystem.h>
#include <Cue/Project/Error.h>
#include <Cue/Project/Generator.h>

#include <Windows.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class TestFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief Test 中の回復不能失敗を即座に終了 Code へ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(90);
    }

    /// @brief Message 付き回復不能失敗を即座に終了 Code へ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(91);
    }
};

class TestDirectory final
{
  public:
    /// @brief Process と時刻から一意な実 Filesystem Test Root を作成する
    TestDirectory()
    {
        std::array<wchar_t, MAX_PATH> temporary{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
        if (length == 0U || length >= temporary.size())
        {
            return;
        }
        m_path = temporary.data();
        m_path += L"CueProjectGeneratorTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                  std::to_wstring(GetTickCount64());
        m_isCreated = CreateDirectoryW(m_path.c_str(), nullptr) != FALSE;
    }

    /// @brief Test Root の一意所有を保つため Copy 構築を禁止する
    TestDirectory(const TestDirectory &) = delete;
    /// @brief Test Root の一意所有を保つため Copy 代入を禁止する
    TestDirectory &operator=(const TestDirectory &) = delete;
    /// @brief Native Path の所有位置を固定するため Move 構築を禁止する
    TestDirectory(TestDirectory &&) = delete;
    /// @brief Native Path の所有位置を固定するため Move 代入を禁止する
    TestDirectory &operator=(TestDirectory &&) = delete;

    /// @brief Test 専用 Root だけを再帰 Cleanup する
    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    /// @brief Root 作成に成功したか返す
    [[nodiscard]] bool is_created() const noexcept
    {
        return m_isCreated;
    }

    /// @brief Windows Filesystem Factory へ渡す UTF-8 Absolute Path を返す
    [[nodiscard]] std::string utf8_path() const
    {
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, m_path.c_str(),
                                              static_cast<int>(m_path.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, m_path.c_str(), static_cast<int>(m_path.size()),
                            result.data(), count, nullptr, nullptr);
        return result;
    }

    /// @brief Root 配下の Native Path を存在確認へ返す
    [[nodiscard]] std::wstring child(std::wstring_view a_relative) const
    {
        return m_path + L"\\" + std::wstring(a_relative);
    }

  private:
    std::wstring m_path;
    bool m_isCreated = false;
};

/// @brief Native Directory が通常 Directory として存在するか判定する
[[nodiscard]] bool is_directory(const std::wstring &a_path) noexcept
{
    const DWORD attributes = GetFileAttributesW(a_path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U;
}

/// @brief Native File が通常 File として存在するか判定する
[[nodiscard]] bool is_file(const std::wstring &a_path) noexcept
{
    const DWORD attributes = GetFileAttributesW(a_path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U;
}

/// @brief 実 Windows IO で生成・再 Open・既存先拒否を一連の Process 契約として検証する
[[nodiscard]] bool test_windows_generation(const cue::AssertContext &a_assertContext)
{
    TestDirectory directory;
    if (!directory.is_created())
    {
        return false;
    }
    auto parent = cue::create_windows_filesystem_root(directory.utf8_path(), a_assertContext);
    auto projectId = cue::ProjectId::parse("12345678-1234-4abc-8def-1234567890ab", a_assertContext);
    cue::BlankProjectTemplate projectTemplate{
        cue::EngineCompatibility{cue::EngineVersion{0U, 1U, 0U}, std::nullopt}};
    if (!parent || !projectId)
    {
        return false;
    }
    auto generated = cue::generate_blank_project(**parent.try_value(), "SampleProject", "Sample Project",
                                                 *projectId.try_value(), projectTemplate, a_assertContext);
    if (!generated)
    {
        return false;
    }

    constexpr std::array paths = {std::wstring_view(L"SampleProject\\Assets\\Source"),
                                  std::wstring_view(L"SampleProject\\Assets\\Runtime"),
                                  std::wstring_view(L"SampleProject\\Generated"),
                                  std::wstring_view(L"SampleProject\\Saved")};
    for (const std::wstring_view path : paths)
    {
        if (!is_directory(directory.child(path)))
        {
            return false;
        }
    }
    if (!is_file(directory.child(L"SampleProject\\CueProject.json")) ||
        GetFileAttributesW(directory.child(L"SampleProject\\CMakeLists.txt").c_str()) != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(directory.child(L"SampleProject\\DefaultScene.cue").c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }

    auto projectRoot = cue::create_windows_filesystem_root(directory.utf8_path() + "/SampleProject", a_assertContext);
    auto loaded = projectRoot ? cue::load_project_descriptor(**projectRoot.try_value(), a_assertContext)
                              : cue::Result<cue::ProjectDescriptor>::failure(
                                    cue::make_project_error(a_assertContext, cue::ProjectError::IoFailure,
                                                            "Generated project root could not be opened"));
    if (!loaded || !generated.try_value()->equivalent_to(*loaded.try_value()))
    {
        return false;
    }

    auto duplicateId = cue::ProjectId::parse("87654321-4321-4abc-8def-ba0987654321", a_assertContext);
    auto duplicate = cue::generate_blank_project(**parent.try_value(), "SampleProject", "Replacement",
                                                 *duplicateId.try_value(), projectTemplate, a_assertContext);
    auto reloaded = cue::load_project_descriptor(**projectRoot.try_value(), a_assertContext);
    return !duplicate && reloaded && generated.try_value()->equivalent_to(*reloaded.try_value());
}
} // namespace

/// @brief Windows 実 Filesystem 上の Atomic Blank Project 生成契約を終了 Code で検証する
int main()
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    return test_windows_generation(assertContext) ? 0 : 1;
}
