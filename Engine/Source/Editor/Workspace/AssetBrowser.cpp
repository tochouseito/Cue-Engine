#include "AssetBrowser.h"

// === Runtime includes ===
#include <IO/IFileSystem.h>

// === C++ includes ===
#include <algorithm>
#include <utility>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    AssetBrowser::AssetBrowser(Core::IO::IFileSystem& a_fileSystem,
                               AssetSelection* a_selectedAsset) noexcept
        : m_fileSystem(&a_fileSystem), m_selectedAsset(a_selectedAsset)
    {
    }

    void AssetBrowser::set_asset_root_path(const Core::IO::Path& a_path)
    {
        const Core::IO::Path normalizedPath = a_path.normalize();
        if (m_assetRootPath.utf8() == normalizedPath.utf8())
        {
            return;
        }

        m_assetRootPath = normalizedPath;
        m_currentScenePath = {};
        // refresh は失敗内容を AssetBrowser 内に保持するため、更新失敗後も UI で原因を確認できます
        const Result result = refresh();
        if (!result)
        {
            return;
        }
    }

    void AssetBrowser::set_current_scene_path(
        const Core::IO::Path& a_path) noexcept
    {
        m_currentScenePath = a_path.normalize();
    }

    Result AssetBrowser::refresh()
    {
        m_entries.clear();
        m_errorMessage.clear();
        if (m_assetRootPath.is_empty())
        {
            return Result::ok();
        }
        if (m_fileSystem == nullptr)
        {
            m_errorMessage = "FileSystem が初期化されていません。";
            return Result::fail(Code::InvalidState, Severity::Error,
                                "AssetBrowser file system is not initialized.");
        }

        const Result result = collect_entries(m_assetRootPath, m_entries);
        if (!result)
        {
            m_entries.clear();
            m_errorMessage = result.message;
        }
        return result;
    }

    void AssetBrowser::update()
    {
        ImGui::Begin("Asset Browser");

        if (m_assetRootPath.is_empty())
        {
            ImGui::TextUnformatted("Project を選択してください。");
            ImGui::End();
            return;
        }
        if (!m_errorMessage.empty())
        {
            ImGui::TextWrapped("%s", m_errorMessage.c_str());
            ImGui::End();
            return;
        }

        ImGui::PushID(m_assetRootPath.utf8().c_str());
        const bool isOpen = ImGui::TreeNodeEx(m_assetRootPath.filename().c_str(),
                                              ImGuiTreeNodeFlags_DefaultOpen |
                                                  ImGuiTreeNodeFlags_OpenOnArrow |
                                                  ImGuiTreeNodeFlags_SpanAvailWidth);
        if (ImGui::BeginPopupContextItem("AssetBrowserRootContext"))
        {
            if (ImGui::MenuItem("新規 Scene"))
            {
                m_pendingNewSceneDirectory = m_assetRootPath;
                m_hasNewSceneRequest = true;
            }
            ImGui::EndPopup();
        }
        if (isOpen)
        {
            draw_entries(m_entries);
            ImGui::TreePop();
        }
        ImGui::PopID();

        ImGui::End();
    }

    bool AssetBrowser::consume_open_scene_request(
        Core::IO::Path& a_outPath) noexcept
    {
        if (!m_hasOpenSceneRequest)
        {
            return false;
        }

        a_outPath = m_pendingOpenScenePath;
        m_pendingOpenScenePath = {};
        m_hasOpenSceneRequest = false;
        return true;
    }

    bool AssetBrowser::consume_new_scene_request(
        Core::IO::Path& a_outDirectory) noexcept
    {
        if (!m_hasNewSceneRequest)
        {
            return false;
        }

        a_outDirectory = m_pendingNewSceneDirectory;
        m_pendingNewSceneDirectory = {};
        m_hasNewSceneRequest = false;
        return true;
    }

    bool AssetBrowser::consume_asset_selection(
        AssetSelection& a_outSelection) noexcept
    {
        if (!m_hasAssetSelectionRequest)
        {
            return false;
        }

        a_outSelection = m_pendingAssetSelection;
        m_pendingAssetSelection = {};
        m_hasAssetSelectionRequest = false;
        return true;
    }

    Result AssetBrowser::collect_entries(const Core::IO::Path& a_directory,
                                         std::vector<Entry>& a_outEntries)
    {
        std::vector<Core::IO::Path> paths{};
        Result result = m_fileSystem->list_directory(a_directory, &paths);
        if (!result)
        {
            return result;
        }

        a_outEntries.reserve(a_outEntries.size() + paths.size());
        for (const Core::IO::Path& path : paths)
        {
            Core::IO::FileStat stat{};
            result = m_fileSystem->stat(path, &stat);
            if (!result)
            {
                return result;
            }

            Entry entry{};
            entry.path = path;
            entry.name = path.filename();
            entry.isDirectory = stat.type == Core::IO::FileType::directory;
            entry.sizeBytes = stat.size_bytes;
            entry.kind = classify_asset_kind(entry.path);
            if (entry.isDirectory)
            {
                result = collect_entries(entry.path, entry.children);
                if (!result)
                {
                    return result;
                }
            }

            a_outEntries.push_back(std::move(entry));
        }

        // Project asset は増えるため、folder を先に揃えて走査対象を見つけやすくする。
        std::sort(a_outEntries.begin(), a_outEntries.end(),
                  [](const Entry& a_left, const Entry& a_right) {
                      if (a_left.isDirectory != a_right.isDirectory)
                      {
                          return a_left.isDirectory;
                      }

                      return a_left.name < a_right.name;
                  });
        return Result::ok();
    }

    void AssetBrowser::draw_entries(const std::vector<Entry>& a_entries)
    {
        for (const Entry& entry : a_entries)
        {
            draw_entry(entry);
        }
    }

    void AssetBrowser::draw_entry(const Entry& a_entry)
    {
        ImGui::PushID(a_entry.path.utf8().c_str());

        if (a_entry.isDirectory)
        {
            const bool isOpen = ImGui::TreeNodeEx(
                a_entry.name.c_str(),
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);
            if (ImGui::BeginPopupContextItem("AssetBrowserDirectoryContext"))
            {
                if (ImGui::MenuItem("新規 Scene"))
                {
                    m_pendingNewSceneDirectory = a_entry.path;
                    m_hasNewSceneRequest = true;
                }
                ImGui::EndPopup();
            }
            if (isOpen)
            {
                draw_entries(a_entry.children);
                ImGui::TreePop();
            }

            ImGui::PopID();
            return;
        }

        const bool isScene = is_scene_file(a_entry.kind);
        const bool hasAssetSelection =
            m_selectedAsset != nullptr && !m_selectedAsset->path.is_empty();
        const bool isSelected =
            hasAssetSelection && a_entry.path.utf8() == m_selectedAsset->path.utf8();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                                   ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (isSelected || (!hasAssetSelection && isScene &&
                           a_entry.path.utf8() == m_currentScenePath.utf8()))
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::TreeNodeEx(a_entry.name.c_str(), flags);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            m_pendingAssetSelection = {a_entry.path, a_entry.sizeBytes, a_entry.kind};
            m_hasAssetSelectionRequest = true;
        }
        if (isScene && ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            m_pendingOpenScenePath = a_entry.path;
            m_hasOpenSceneRequest = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", asset_kind_name(a_entry.kind));

        ImGui::PopID();
    }

    bool AssetBrowser::is_scene_file(const AssetKind a_kind) noexcept
    {
        return a_kind == AssetKind::scene;
    }
} // namespace Cue::Editor
