#pragma once

// === Core includes ===
#include <IO/IFileSystem.h>

// === D3D12 includes ===
#include <D3D12Backend.h>

// === Engine includes ===
#include <Asset/AssetManager.h>

// === Editor includes ===
#include "AssetDragDrop.h"
#include "Icon.h"

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <memory>
#include <span>
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

        void set_texture_preview_dependencies(
            AssetManager* a_assetManager,
            RHI::DX12::D3D12Backend* a_backend) noexcept
        {
            m_assetManager = a_assetManager;
            m_backend = a_backend;
            clear_texture_preview_cache();
        }

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

        void set_selected_asset_path(Core::IO::Path* a_selectedAssetPath) noexcept
        {
            m_selectedAssetPath = a_selectedAssetPath;
        }

        void refresh()
        {
            clear_cache();
            clear_texture_preview_cache();
        }

        [[nodiscard]] Core::IO::Path current_asset_folder_path() const noexcept
        {
            const Core::IO::Path assetRootPath = m_assetRootPath.normalize();
            if (assetRootPath.is_empty())
            {
                return {};
            }

            const Core::IO::Path selectedFolderPath =
                m_selectedFolderPath.normalize();
            if (selectedFolderPath.is_empty())
            {
                return assetRootPath;
            }

            const std::string assetRoot = assetRootPath.utf8();
            const std::string selectedFolder = selectedFolderPath.utf8();
            if (selectedFolder == assetRoot ||
                selectedFolder.rfind(assetRoot + "/", 0) == 0)
            {
                return selectedFolderPath;
            }

            return assetRootPath;
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

        void clear_texture_preview_cache()
        {
            RHI::IViewManager* viewManager =
                m_backend != nullptr ? m_backend->get_view_manager() : nullptr;
            if (viewManager != nullptr)
            {
                for (const auto& [_, preview] : m_texturePreviewCache)
                {
                    if (preview.srvHandle.valid())
                    {
                        (void)viewManager->destroy_view(preview.srvHandle);
                    }
                }
            }

            m_texturePreviewCache.clear();
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

            m_fileDrawIndex = 0;
            for (const Core::IO::Path& filePath : files)
            {
                draw_file_button(filePath);
            }
        }

        void draw_file_button(const Core::IO::Path& a_filePath)
        {
            constexpr float k_tileWidth = 96.0f;
            constexpr float k_buttonSize = 64.0f;
            constexpr float k_tileSpacing = 12.0f;

            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const int columnCount = (std::max)(
                1,
                static_cast<int>(availableWidth / (k_tileWidth + k_tileSpacing)));
            const int columnIndex = m_fileDrawIndex % columnCount;

            ImGui::PushID(a_filePath.utf8().c_str());
            ImGui::BeginGroup();

            const float tileStartX = ImGui::GetCursorPosX();
            ImGui::SetCursorPosX(
                tileStartX + (k_tileWidth - k_buttonSize) * 0.5f);
            const bool isPressed = ImGui::InvisibleButton(
                "##FileButton",
                ImVec2(k_buttonSize, k_buttonSize));

            const ImVec2 buttonMin = ImGui::GetItemRectMin();
            const ImVec2 buttonMax = ImGui::GetItemRectMax();
            const bool isMaterial = is_material_file(a_filePath);
            if (isPressed && isMaterial && m_selectedAssetPath != nullptr)
            {
                *m_selectedAssetPath = a_filePath.normalize();
            }
            const bool isHovered = ImGui::IsItemHovered();
            const bool isActive = ImGui::IsItemActive();
            const ImU32 buttonColor = ImGui::GetColorU32(
                isActive ? ImGuiCol_ButtonActive
                         : (isHovered ? ImGuiCol_ButtonHovered
                                      : ImGuiCol_Button));
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                buttonMin,
                buttonMax,
                buttonColor,
                ImGui::GetStyle().FrameRounding);
            drawList->AddRect(
                buttonMin,
                buttonMax,
                ImGui::GetColorU32(ImGuiCol_Border),
                ImGui::GetStyle().FrameRounding);

            const TexturePreview* texturePreview =
                get_texture_preview(a_filePath);
            if (texturePreview != nullptr && texturePreview->isReady)
            {
                draw_texture_preview(*texturePreview, buttonMin, buttonMax);
            }
            else
            {
                constexpr float k_iconFontSize = k_buttonSize;
                const char* icon = isMaterial
                    ? CUE_ICON_MATERIAL
                    : (is_texture_file(a_filePath) ? CUE_ICON_IMAGE
                                                   : CUE_ICON_Unknown);
                ImFont* font = ImGui::GetFont();
                const ImVec2 iconSize = font->CalcTextSizeA(
                    k_iconFontSize,
                    FLT_MAX,
                    0.0f,
                    icon);
                const ImVec2 iconPos(
                    buttonMin.x + (k_buttonSize - iconSize.x) * 0.5f,
                    buttonMin.y + (k_buttonSize - iconSize.y) * 0.5f);
                drawList->AddText(
                    font,
                    k_iconFontSize,
                    iconPos,
                    ImGui::GetColorU32(ImGuiCol_Text),
                    icon);
            }

            if (isMaterial && ImGui::BeginDragDropSource())
            {
                const std::string payloadPath = a_filePath.normalize().utf8();
                ImGui::SetDragDropPayload(
                    k_materialAssetPayloadType,
                    payloadPath.c_str(),
                    payloadPath.size() + 1);
                ImGui::Text("%s %s", CUE_ICON_MATERIAL,
                    a_filePath.filename().c_str());
                ImGui::EndDragDropSource();
            }
            else if (is_texture_file(a_filePath) &&
                ImGui::BeginDragDropSource())
            {
                Core::IO::Path texturePath{};
                if (resolve_texture_preview_path(a_filePath, texturePath))
                {
                    const std::string payloadPath =
                        texturePath.normalize().utf8();
                    ImGui::SetDragDropPayload(
                        k_textureAssetPayloadType,
                        payloadPath.c_str(),
                        payloadPath.size() + 1);
                    ImGui::Text("%s %s", CUE_ICON_IMAGE,
                        texturePath.filename().c_str());
                }
                else
                {
                    ImGui::TextUnformatted(
                        "Cooked texture が見つかりません。");
                }
                ImGui::EndDragDropSource();
            }

            const std::string filename = a_filePath.filename();
            ImGui::PushTextWrapPos(tileStartX + k_tileWidth);
            ImGui::SetCursorPosX(tileStartX);
            ImGui::TextUnformatted(filename.c_str());
            ImGui::PopTextWrapPos();

            ImGui::EndGroup();
            ImGui::PopID();

            ++m_fileDrawIndex;
            if (columnIndex + 1 < columnCount)
            {
                ImGui::SameLine(0.0f, k_tileSpacing);
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

        [[nodiscard]] static bool is_material_file(
            const Core::IO::Path& a_filePath) noexcept
        {
            return to_lower_ascii(a_filePath.extension()) == ".cuematerial";
        }

        [[nodiscard]] static bool is_texture_file(
            const Core::IO::Path& a_filePath) noexcept
        {
            const std::string extension = to_lower_ascii(a_filePath.extension());
            return extension == ".cuetexture" || extension == ".png";
        }

        [[nodiscard]] static std::string to_lower_ascii(
            std::string a_text) noexcept
        {
            for (char& character : a_text)
            {
                character = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(character)));
            }
            return a_text;
        }

        [[nodiscard]] bool resolve_texture_preview_path(
            const Core::IO::Path& a_filePath,
            Core::IO::Path& outTexturePath) const
        {
            const std::string extension = to_lower_ascii(a_filePath.extension());
            if (extension == ".cuetexture")
            {
                outTexturePath = a_filePath.normalize();
                return true;
            }

            if (extension != ".png" || m_fileSystem == nullptr)
            {
                return false;
            }

            const Core::IO::Path cookedPath = Core::IO::Path::join(
                a_filePath.parent(),
                Core::IO::Path(a_filePath.stem() + ".cuetexture"));
            bool exists = false;
            const Result result = m_fileSystem->exists(cookedPath, &exists);
            if (!result || !exists)
            {
                return false;
            }

            outTexturePath = cookedPath.normalize();
            return true;
        }

        [[nodiscard]] std::string make_asset_relative_name(
            const Core::IO::Path& a_assetPath) const
        {
            const std::string assetRoot = m_assetRootPath.normalize().utf8();
            const std::string assetPath = a_assetPath.normalize().utf8();
            if (!assetRoot.empty() && assetPath.rfind(assetRoot + "/", 0) == 0)
            {
                return assetPath.substr(assetRoot.size() + 1);
            }

            return a_assetPath.filename();
        }

        struct TexturePreviewHeader final
        {
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t mipLevels = 1;
            RHI::ColorFormat format = RHI::ColorFormat::R8G8B8A8_UNORM;
        };

        struct TexturePreview final
        {
            RHI::ViewHandle srvHandle{};
            uint32_t width = 0;
            uint32_t height = 0;
            bool isReady = false;
            bool hasFailed = false;
        };

        [[nodiscard]] Result read_texture_preview_header(
            const Core::IO::Path& a_texturePath,
            TexturePreviewHeader& outHeader) const
        {
            if (m_fileSystem == nullptr)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "FileSystem is not initialized.");
            }

            Core::IO::FileOpenDesc openDesc{};
            openDesc.access = Core::IO::OpenAccess::read;
            openDesc.create = Core::IO::OpenCreate::open_existing;
            openDesc.flags = Core::IO::OpenFlags::sequential;

            std::unique_ptr<Core::IO::IFile> file = nullptr;
            Result result = m_fileSystem->open(a_texturePath, openDesc, &file);
            if (!result)
            {
                return result;
            }

            std::array<std::byte, sizeof(CueTextureHeader)> data{};
            uint64_t readSize = 0;
            result = file->read(
                std::span<std::byte>(data.data(), data.size()),
                &readSize);
            (void)file->close();
            if (!result)
            {
                return result;
            }
            if (readSize != data.size())
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Cooked texture header is incomplete.");
            }

            CueTextureHeader header{};
            std::memcpy(&header, data.data(), sizeof(CueTextureHeader));
            if (header.magic != k_cueTextureMagic ||
                header.version != k_cueTextureVersion ||
                header.width == 0 ||
                header.height == 0 ||
                header.mipCount == 0 ||
                header.arraySize != 1)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Cooked texture header is invalid.");
            }

            outHeader.width = header.width;
            outHeader.height = header.height;
            outHeader.mipLevels = header.mipCount;
            outHeader.format = static_cast<RHI::ColorFormat>(header.format);
            return Result::ok();
        }

        [[nodiscard]] Result load_texture_preview(
            const Core::IO::Path& a_texturePath,
            TexturePreview& outPreview)
        {
            if (m_fileSystem == nullptr || m_assetManager == nullptr ||
                m_backend == nullptr || m_backend->get_view_manager() == nullptr)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "Texture preview dependencies are not initialized.");
            }

            TexturePreviewHeader header{};
            Result result = read_texture_preview_header(a_texturePath, header);
            if (!result)
            {
                return result;
            }

            uint32_t textureId = AssetManager::k_errorTextureId;
            result = m_assetManager->register_texture_from_cuetexture(
                *m_fileSystem,
                make_asset_relative_name(a_texturePath),
                a_texturePath,
                textureId);
            if (!result)
            {
                return result;
            }

            RHI::TextureHandle textureHandle{};
            result = m_assetManager->get_texture_handle(textureId, textureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc viewDesc{};
            viewDesc.name = "AssetBrowserPreview/" +
                make_asset_relative_name(a_texturePath);
            viewDesc.type = RHI::ViewType::ShaderResourceTexture2D;
            viewDesc.bufferKind = RHI::BufferKind::Texture;
            viewDesc.textureHandle = textureHandle;
            viewDesc.colorFormat = header.format;
            viewDesc.mipSlice = 0;
            viewDesc.mipLevels = header.mipLevels;

            RHI::ViewHandle srvHandle{};
            result = m_backend->get_view_manager()->create_view(viewDesc, srvHandle);
            if (!result)
            {
                return result;
            }

            outPreview.srvHandle = srvHandle;
            outPreview.width = header.width;
            outPreview.height = header.height;
            outPreview.isReady = true;
            outPreview.hasFailed = false;
            return Result::ok();
        }

        [[nodiscard]] TexturePreview* get_texture_preview(
            const Core::IO::Path& a_filePath)
        {
            Core::IO::Path texturePath{};
            if (!resolve_texture_preview_path(a_filePath, texturePath))
            {
                return nullptr;
            }

            const std::string cacheKey = texturePath.normalize().utf8();
            if (auto cacheIt = m_texturePreviewCache.find(cacheKey);
                cacheIt != m_texturePreviewCache.end())
            {
                return &cacheIt->second;
            }

            TexturePreview preview{};
            const Result result = load_texture_preview(texturePath, preview);
            if (!result)
            {
                preview.isReady = false;
                preview.hasFailed = true;
            }

            auto [it, _] = m_texturePreviewCache.emplace(cacheKey, preview);
            return &it->second;
        }

        void draw_texture_preview(
            const TexturePreview& a_preview,
            const ImVec2& a_buttonMin,
            const ImVec2& a_buttonMax)
        {
            const D3D12_GPU_DESCRIPTOR_HANDLE srvGpuDescHandle =
                m_backend->get_gpu_descriptor_handle(
                    a_preview.srvHandle,
                    m_backend->current_back_buffer_index(),
                    m_backend->buffer_count());
            if (srvGpuDescHandle.ptr == 0 ||
                a_preview.width == 0 ||
                a_preview.height == 0)
            {
                return;
            }

            const float buttonWidth = a_buttonMax.x - a_buttonMin.x;
            const float buttonHeight = a_buttonMax.y - a_buttonMin.y;
            float imageWidth = buttonWidth;
            float imageHeight = imageWidth *
                static_cast<float>(a_preview.height) /
                static_cast<float>(a_preview.width);
            if (imageHeight > buttonHeight)
            {
                const float scale = buttonHeight / imageHeight;
                imageWidth *= scale;
                imageHeight = buttonHeight;
            }

            const ImVec2 imageMin(
                a_buttonMin.x + (buttonWidth - imageWidth) * 0.5f,
                a_buttonMin.y + (buttonHeight - imageHeight) * 0.5f);
            const ImVec2 imageMax(
                imageMin.x + imageWidth,
                imageMin.y + imageHeight);
            ImGui::GetWindowDrawList()->AddImage(
                static_cast<ImTextureID>(srvGpuDescHandle.ptr),
                imageMin,
                imageMax);
        }

        Core::IO::IFileSystem* m_fileSystem = nullptr;
        AssetManager* m_assetManager = nullptr;
        RHI::DX12::D3D12Backend* m_backend = nullptr;
        Core::IO::Path* m_selectedAssetPath = nullptr;
        Core::IO::Path m_assetRootPath{};
        Core::IO::Path m_selectedFolderPath{};
        std::unordered_map<std::string, std::vector<Core::IO::Path>> m_folderCache{};
        std::unordered_map<std::string, std::vector<Core::IO::Path>> m_fileCache{};
        std::unordered_map<std::string, TexturePreview> m_texturePreviewCache{};
        int m_fileDrawIndex = 0;
    };
}
