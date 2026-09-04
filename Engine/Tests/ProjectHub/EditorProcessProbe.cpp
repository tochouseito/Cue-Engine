#include <string>
#include <string_view>

#include <Windows.h>

namespace
{
/// @brief 指定Optionに続くCommand Line値を返す
[[nodiscard]] std::wstring_view find_value(int a_argumentCount, wchar_t **a_arguments,
                                           std::wstring_view a_option) noexcept
{
    for (int index = 1; index + 1 < a_argumentCount; ++index)
    {
        if (a_arguments[index] == a_option)
        {
            return a_arguments[index + 1];
        }
    }
    return {};
}
} // namespace

/// @brief Editor Process契約を検証し、Project Rootへ起動成功Markerを残す
int wmain(int a_argumentCount, wchar_t **a_arguments)
{
    const std::wstring_view protocol = find_value(a_argumentCount, a_arguments, L"--protocol-version");
    const std::wstring_view descriptor = find_value(a_argumentCount, a_arguments, L"--project-descriptor");
    const std::wstring_view projectId = find_value(a_argumentCount, a_arguments, L"--expected-project-id");
    const std::wstring_view compatibility = find_value(a_argumentCount, a_arguments, L"--engine-compatibility-id");
    if (protocol != L"1" || descriptor.empty() || projectId.size() != 36 || compatibility.empty())
    {
        return 1;
    }

    const std::size_t separator = descriptor.find_last_of(L"\\/");
    if (separator == std::wstring_view::npos)
    {
        return 2;
    }
    std::wstring marker(descriptor.substr(0, separator + 1));
    marker.append(L"EditorProcessProbe.ok");
    HANDLE file = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return 3;
    }
    constexpr char k_content[] = "ok";
    DWORD written = 0;
    const BOOL didWrite = WriteFile(file, k_content, static_cast<DWORD>(sizeof(k_content) - 1), &written, nullptr);
    const BOOL didClose = CloseHandle(file);
    return didWrite && written == sizeof(k_content) - 1 && didClose ? 0 : 4;
}
