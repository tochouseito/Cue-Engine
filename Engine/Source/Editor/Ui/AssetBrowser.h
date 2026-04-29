#pragma once

// === Core includes ===
#include <IO/IFileSystem.h>

// === C++ includes ===
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class AssetBrowser final
    {
    public:
        explicit AssetBrowser(Core::IO::IFileSystem* a_fileSystem)
            : m_fileSystem(a_fileSystem)
        {
        }
        ~AssetBrowser() = default;

        void set_asset_root_path(const Core::IO::Path& a_assetRootPath)
        {
            m_assetRootPath = a_assetRootPath.normalize();
            m_selectedFolderPath = m_assetRootPath;
            clear_cache();
        }

        void clear_asset_root_path()
        {
            m_assetRootPath = {};
            m_selectedFolderPath = {};
            clear_cache();
        }

        void update()
        {
            ImGui::Begin("Asset Browser");

            if (m_fileSystem == nullptr)
            {
                ImGui::TextUnformatted("FileSystem が初期化されていません。");
                ImGui::End();
                return;
            }

            if (m_assetRootPath.is_empty())
            {
                ImGui::TextUnformatted("プロジェクトの Assets フォルダが未設定です。");
                ImGui::End();
                return;
            }

            bool rootExists = false;
            Result existsResult =
                m_fileSystem->exists(m_assetRootPath, &rootExists);
            if (!existsResult || !rootExists)
            {
                ImGui::TextUnformatted("Assets フォルダが見つかりません。");
                ImGui::End();
                return;
            }

            if (m_selectedFolderPath.is_empty())
            {
                m_selectedFolderPath = m_assetRootPath;
            }

            if (ImGui::Button("Refresh"))
            {
                clear_cache();
            }

            ImGui::BeginChild("AssetFolderTree", ImVec2(260.0f, 0.0f), true);
            draw_folder_node(m_assetRootPath);
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("AssetFileList", ImVec2(0.0f, 0.0f), true);
            draw_file_list();
            ImGui::EndChild();

            ImGui::End();
        }

    private:
        void clear_cache()
        {
            m_folderCache.clear();
            m_fileCache.clear();
        }

        void draw_folder_node(const Core::IO::Path& a_folderPath)
        {
            const std::string normalizedPath = a_folderPath.normalize().utf8();
            const bool isRoot = normalizedPath == m_assetRootPath.utf8();
            const bool isSelected =
                normalizedPath == m_selectedFolderPath.normalize().utf8();
            const std::vector<Core::IO::Path>& childFolders =
                collect_child_folders(a_folderPath);

            ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_OpenOnArrow |
                ImGuiTreeNodeFlags_SpanAvailWidth;
            if (isSelected)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (childFolders.empty())
            {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (isRoot)
            {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }

            const std::string label = isRoot
                ? std::string("Assets")
                : a_folderPath.filename();

            ImGui::PushID(normalizedPath.c_str());
            const bool isOpen = ImGui::TreeNodeEx(
                label.c_str(), flags);

            if (ImGui::IsItemClicked())
            {
                m_selectedFolderPath = a_folderPath.normalize();
            }

            if (isOpen)
            {
                for (const Core::IO::Path& childFolder : childFolders)
                {
                    draw_folder_node(childFolder);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        void draw_file_list()
        {
            ImGui::Text("Folder: %s", display_folder_name(
                m_selectedFolderPath).c_str());
            ImGui::Separator();

            const std::vector<Core::IO::Path>& files =
                collect_child_files(m_selectedFolderPath);
            if (files.empty())
            {
                ImGui::TextUnformatted("ファイルはありません。");
                return;
            }

            for (const Core::IO::Path& filePath : files)
            {
                ImGui::BulletText("%s", filePath.filename().c_str());
            }
        }

        [[nodiscard]] const std::vector<Core::IO::Path>& collect_child_folders(
            const Core::IO::Path& a_folderPath)
        {
            const std::string cacheKey = a_folderPath.normalize().utf8();
            if (const auto cacheIt = m_folderCache.find(cacheKey);
                cacheIt != m_folderCache.end())
            {
                return cacheIt->second;
            }

            std::vector<Core::IO::Path> folders{};
            std::vector<Core::IO::Path> entries{};
            const Result result = m_fileSystem->list_directory(
                a_folderPath, &entries);
            if (!result)
            {
                auto [it, _] = m_folderCache.emplace(cacheKey, std::move(folders));
                return it->second;
            }

            for (const Core::IO::Path& entryPath : entries)
            {
                Core::IO::FileStat stat{};
                if (!m_fileSystem->stat(entryPath, &stat) ||
                    stat.type != Core::IO::FileType::directory)
                {
                    continue;
                }

                folders.push_back(entryPath.normalize());
            }

            std::sort(folders.begin(), folders.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.filename() < a_right.filename();
                });
            auto [it, _] = m_folderCache.emplace(cacheKey, std::move(folders));
            return it->second;
        }

        [[nodiscard]] const std::vector<Core::IO::Path>& collect_child_files(
            const Core::IO::Path& a_folderPath)
        {
            const std::string cacheKey = a_folderPath.normalize().utf8();
            if (const auto cacheIt = m_fileCache.find(cacheKey);
                cacheIt != m_fileCache.end())
            {
                return cacheIt->second;
            }

            std::vector<Core::IO::Path> files{};
            std::vector<Core::IO::Path> entries{};
            const Result result = m_fileSystem->list_directory(
                a_folderPath, &entries);
            if (!result)
            {
                auto [it, _] = m_fileCache.emplace(cacheKey, std::move(files));
                return it->second;
            }

            for (const Core::IO::Path& entryPath : entries)
            {
                Core::IO::FileStat stat{};
                if (!m_fileSystem->stat(entryPath, &stat) ||
                    stat.type != Core::IO::FileType::regular)
                {
                    continue;
                }

                files.push_back(entryPath.normalize());
            }

            std::sort(files.begin(), files.end(),
                [](const Core::IO::Path& a_left, const Core::IO::Path& a_right)
                {
                    return a_left.filename() < a_right.filename();
                });
            auto [it, _] = m_fileCache.emplace(cacheKey, std::move(files));
            return it->second;
        }

        [[nodiscard]] std::string display_folder_name(
            const Core::IO::Path& a_folderPath) const
        {
            const std::string folderPath = a_folderPath.normalize().utf8();
            const std::string assetRootPath = m_assetRootPath.utf8();
            if (folderPath == assetRootPath)
            {
                return "Assets";
            }

            if (folderPath.rfind(assetRootPath + "/", 0) == 0)
            {
                return "Assets/" +
                    folderPath.substr(assetRootPath.size() + 1);
            }

            return a_folderPath.filename();
        }

        Core::IO::IFileSystem* m_fileSystem = nullptr;
        Core::IO::Path m_assetRootPath{};
        Core::IO::Path m_selectedFolderPath{};
        std::unordered_map<std::string, std::vector<Core::IO::Path>> m_folderCache{};
        std::unordered_map<std::string, std::vector<Core::IO::Path>> m_fileCache{};
    };
}
