#include "ImGuiManager.h"

namespace Cue::Editor
{
    namespace
    {
        constexpr float k_uiFontSize = 18.0f;
        constexpr float k_codeFontSize = 16.0f;
        constexpr float k_japaneseRasterizerMultiply = 2.5f;
        constexpr ImWchar k_iconGlyphRanges[] = { 0xE000, 0xF8FF, 0 };

        struct EditorFontPaths final
        {
            Core::IO::Path uiFont{};
            Core::IO::Path fallbackFont{};
            Core::IO::Path codeFont{};
            Core::IO::Path iconFont{};
        };

        Core::IO::Path get_repository_layout_file_path()
        {
#ifdef CUE_PROJECT_ROOT_PATH
            return Core::IO::Path::join(
                Core::IO::Path(std::string(CUE_PROJECT_ROOT_PATH)),
                Core::IO::Path("config/editor/imgui.ini"));
#else
            return Core::IO::Path("config/editor/imgui.ini");
#endif
        }

        Core::IO::Path get_repository_font_root_path()
        {
#ifdef CUE_PROJECT_ROOT_PATH
            return Core::IO::Path::join(
                Core::IO::Path(std::string(CUE_PROJECT_ROOT_PATH)),
                Core::IO::Path("Engine/Fonts"));
#else
            return Core::IO::Path("Fonts");
#endif
        }

        Core::IO::Path get_module_directory_path()
        {
            constexpr DWORD k_modulePathCapacity = 4096;
            std::string modulePath(k_modulePathCapacity, '\0');
            const DWORD length =
                ::GetModuleFileNameA(nullptr, modulePath.data(), k_modulePathCapacity);
            if (length == 0 || length >= k_modulePathCapacity)
            {
                return Core::IO::Path();
            }

            modulePath.resize(length);
            return Core::IO::Path(modulePath).parent();
        }

        Core::IO::Path get_runtime_font_root_path()
        {
            const Core::IO::Path moduleDirectoryPath = get_module_directory_path();
            if (moduleDirectoryPath.is_empty())
            {
                return Core::IO::Path("EngineResources/Fonts");
            }

            return Core::IO::Path::join(
                moduleDirectoryPath,
                Core::IO::Path("EngineResources/Fonts"));
        }

        Result ensure_directory_exists(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_directoryPath)
        {
            const Result createResult =
                a_fileSystem.create_directories(a_directoryPath);
            if (!createResult)
            {
                Core::IO::log(
                    Core::IO::LogSink::debugConsole,
                    "Failed to create ImGui config directory: %s",
                    a_directoryPath.utf8().c_str());
            }

            return createResult;
        }

        Result copy_layout_file_if_needed(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath)
        {
            bool destinationExists = false;
            Result result = a_fileSystem.exists(a_destinationPath, &destinationExists);
            if (!result)
            {
                Core::IO::log(
                    Core::IO::LogSink::debugConsole,
                    "Failed to query ImGui destination ini: %s",
                    a_destinationPath.utf8().c_str());
                return result;
            }
            if (destinationExists)
            {
                return Result::ok();
            }

            bool sourceExists = false;
            result = a_fileSystem.exists(a_sourcePath, &sourceExists);
            if (!result)
            {
                Core::IO::log(
                    Core::IO::LogSink::debugConsole,
                    "Failed to query ImGui source ini: %s",
                    a_sourcePath.utf8().c_str());
                return result;
            }
            if (!sourceExists)
            {
                Core::IO::log(
                    Core::IO::LogSink::debugConsole,
                    "ImGui source ini was not found: %s",
                    a_sourcePath.utf8().c_str());
                return Result::ok();
            }

            std::vector<std::byte> fileBytes{};
            result = a_fileSystem.read_all(a_sourcePath, &fileBytes);
            if (!result)
            {
                Core::IO::log(
                    Core::IO::LogSink::debugConsole,
                    "Failed to read ImGui source ini: %s",
                    a_sourcePath.utf8().c_str());
                return result;
            }

            result = a_fileSystem.write_all(a_destinationPath, fileBytes, true);
            if (!result)
            {
                Core::IO::log(
                    Core::IO::LogSink::debugConsole,
                    "Failed to copy ImGui ini to: %s",
                    a_destinationPath.utf8().c_str());
            }

            return result;
        }

        Result resolve_font_path(
            Core::IO::IFileSystem& a_fileSystem,
            const char* a_fileName,
            Core::IO::Path& outPath)
        {
            const Core::IO::Path repositoryPath = Core::IO::Path::join(
                get_repository_font_root_path(),
                Core::IO::Path(a_fileName));
            const Core::IO::Path runtimePath = Core::IO::Path::join(
                get_runtime_font_root_path(),
                Core::IO::Path(a_fileName));

#ifdef CUE_DEBUG
            const Core::IO::Path candidates[] = { repositoryPath, runtimePath };
#else
            const Core::IO::Path candidates[] = { runtimePath, repositoryPath };
#endif

            for (const Core::IO::Path& candidatePath : candidates)
            {
                bool exists = false;
                const Result result = a_fileSystem.exists(candidatePath, &exists);
                if (!result)
                {
                    Core::IO::log(
                        Core::IO::LogSink::debugConsole,
                        "Failed to query font file: %s",
                        candidatePath.utf8().c_str());
                    return result;
                }
                if (exists)
                {
                    outPath = candidatePath;
                    return Result::ok();
                }
            }

            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Font file was not found.");
        }

        Result resolve_editor_font_paths(
            Core::IO::IFileSystem& a_fileSystem,
            EditorFontPaths& outPaths)
        {
            Result result = resolve_font_path(
                a_fileSystem,
                "Inter-VariableFont_opsz,wght.ttf",
                outPaths.uiFont);
            if (!result)
            {
                return result;
            }

            result = resolve_font_path(
                a_fileSystem,
                "NotoSansJP-VariableFont_wght.ttf",
                outPaths.fallbackFont);
            if (!result)
            {
                return result;
            }

            result = resolve_font_path(
                a_fileSystem,
                "JetBrainsMono-VariableFont_wght.ttf",
                outPaths.codeFont);
            if (!result)
            {
                return result;
            }

            result = resolve_font_path(
                a_fileSystem,
                "MaterialSymbolsOutlined-VariableFont_FILL,GRAD,opsz,wght.ttf",
                outPaths.iconFont);
            if (!result)
            {
                return result;
            }

            return Result::ok();
        }

        ImFont* load_font(
            const Core::IO::Path& a_fontPath,
            ImFontAtlas& a_fontAtlas,
            const float a_fontSize,
            ImFontConfig* a_config,
            const ImWchar* a_glyphRanges)
        {
            return a_fontAtlas.AddFontFromFileTTF(
                a_fontPath.utf8().c_str(),
                a_fontSize,
                a_config,
                a_glyphRanges);
        }

        Result load_editor_fonts(
            Core::IO::IFileSystem& a_fileSystem,
            ImGuiIO& a_io,
            ImFont*& outUiFont,
            ImFont*& outCodeFont)
        {
            EditorFontPaths fontPaths{};
            Result result = resolve_editor_font_paths(a_fileSystem, fontPaths);
            if (!result)
            {
                return result;
            }

            ImFontAtlas& fontAtlas = *a_io.Fonts;
            fontAtlas.Clear();

            const ImWchar* defaultGlyphRanges = fontAtlas.GetGlyphRangesDefault();
            const ImWchar* japaneseGlyphRanges = fontAtlas.GetGlyphRangesJapanese();

            ImFontConfig uiFontConfig{};
            uiFontConfig.GlyphExcludeRanges = k_iconGlyphRanges;
            outUiFont = load_font(
                fontPaths.uiFont,
                fontAtlas,
                k_uiFontSize,
                &uiFontConfig,
                defaultGlyphRanges);
            if (outUiFont == nullptr)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to load Inter font for ImGui.");
            }

            ImFontConfig fallbackFontConfig{};
            fallbackFontConfig.MergeMode = true;
            fallbackFontConfig.OversampleH = 2;
            fallbackFontConfig.OversampleV = 2;
            fallbackFontConfig.RasterizerMultiply = k_japaneseRasterizerMultiply;
            fallbackFontConfig.GlyphExcludeRanges = k_iconGlyphRanges;
            if (load_font(
                fontPaths.fallbackFont,
                fontAtlas,
                k_uiFontSize,
                &fallbackFontConfig,
                japaneseGlyphRanges) == nullptr)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to merge Noto Sans JP into ImGui UI font.");
            }

            ImFontConfig iconFontConfig{};
            iconFontConfig.MergeMode = true;
            iconFontConfig.PixelSnapH = true;
            iconFontConfig.GlyphMinAdvanceX = k_uiFontSize;
            if (load_font(
                fontPaths.iconFont,
                fontAtlas,
                k_uiFontSize,
                &iconFontConfig,
                k_iconGlyphRanges) == nullptr)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to merge Material Symbols into ImGui UI font.");
            }

            outCodeFont = load_font(
                fontPaths.codeFont,
                fontAtlas,
                k_codeFontSize,
                nullptr,
                defaultGlyphRanges);
            if (outCodeFont == nullptr)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to load JetBrains Mono font for ImGui.");
            }

            ImFontConfig codeFallbackFontConfig{};
            codeFallbackFontConfig.MergeMode = true;
            codeFallbackFontConfig.OversampleH = 2;
            codeFallbackFontConfig.OversampleV = 2;
            codeFallbackFontConfig.RasterizerMultiply = k_japaneseRasterizerMultiply;
            if (load_font(
                fontPaths.fallbackFont,
                fontAtlas,
                k_codeFontSize,
                &codeFallbackFontConfig,
                japaneseGlyphRanges) == nullptr)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to merge Noto Sans JP into ImGui code font.");
            }

            a_io.FontDefault = outUiFont;
            return Result::ok();
        }

        std::string resolve_layout_file_path(Core::IO::IFileSystem& a_fileSystem)
        {
            const Core::IO::Path repositoryLayoutPath = get_repository_layout_file_path();

#ifdef CUE_DEBUG
            ensure_directory_exists(a_fileSystem, repositoryLayoutPath.parent());
            return repositoryLayoutPath.utf8();
#else
            constexpr const char* k_fallbackPath = "config/editor/imgui.ini";
            constexpr DWORD k_modulePathCapacity = 4096;

            std::string modulePath(k_modulePathCapacity, '\0');
            const DWORD length =
                ::GetModuleFileNameA(nullptr, modulePath.data(), k_modulePathCapacity);
            if (length == 0 || length >= k_modulePathCapacity)
            {
                return k_fallbackPath;
            }

            modulePath.resize(length);

            const Core::IO::Path exePath(modulePath);
            const Core::IO::Path configDirectory =
                Core::IO::Path::join(
                    exePath.parent(),
                    Core::IO::Path("config/editor"));

            const Result createResult = ensure_directory_exists(a_fileSystem, configDirectory);
            if (!createResult)
            {
                return k_fallbackPath;
            }

            const Core::IO::Path runtimeLayoutPath = Core::IO::Path::join(
                configDirectory,
                Core::IO::Path("imgui.ini"));

            copy_layout_file_if_needed(
                a_fileSystem,
                repositoryLayoutPath,
                runtimeLayoutPath);

            return runtimeLayoutPath.utf8();
#endif
        }

        void imgui_srv_descriptor_alloc(
            ImGui_ImplDX12_InitInfo* a_info,
            D3D12_CPU_DESCRIPTOR_HANDLE* a_outCpuDescHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE* a_outGpuDescHandle)
        {
            auto* backend = static_cast<RHI::DX12::D3D12Backend*>(a_info->UserData);
            if (backend == nullptr || a_outCpuDescHandle == nullptr || a_outGpuDescHandle == nullptr)
            {
                CUE_ASSERT_MSG(false, "ImGui DX12 descriptor allocation callback received invalid arguments.");
                return;
            }

            const Result result = backend->allocate_imgui_srv_descriptor(
                *a_outCpuDescHandle,
                *a_outGpuDescHandle);
            CUE_ASSERT_MSG(result, "Failed to allocate ImGui DX12 descriptor.");
        }

        void imgui_srv_descriptor_free(
            ImGui_ImplDX12_InitInfo* a_info,
            D3D12_CPU_DESCRIPTOR_HANDLE a_cpuDescHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE a_gpuDescHandle)
        {
            auto* backend = static_cast<RHI::DX12::D3D12Backend*>(a_info->UserData);
            if (backend == nullptr)
            {
                CUE_ASSERT_MSG(false, "ImGui DX12 descriptor free callback received null backend.");
                return;
            }

            backend->free_imgui_srv_descriptor(a_cpuDescHandle, a_gpuDescHandle);
        }

        void setup_imgui_style()
        {
            // Fork of AdobeInspired style from ImThemes
            ImGuiStyle& style = ImGui::GetStyle();

            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.3f;
            style.WindowPadding = ImVec2(3.0f, 3.0f);
            style.WindowRounding = 4.0f;
            style.WindowBorderSize = 1.0f;
            style.WindowMinSize = ImVec2(20.0f, 32.0f);
            style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_None;
            style.ChildRounding = 4.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 4.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(4.0f, 3.0f);
            style.FrameRounding = 4.0f;
            style.FrameBorderSize = 1.0f;
            style.ItemSpacing = ImVec2(8.0f, 4.0f);
            style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
            style.CellPadding = ImVec2(4.0f, 2.0f);
            style.IndentSpacing = 21.0f;
            style.ColumnsMinSpacing = 6.0f;
            style.ScrollbarSize = 14.0f;
            style.ScrollbarRounding = 4.0f;
            style.GrabMinSize = 10.0f;
            style.GrabRounding = 20.0f;
            style.TabRounding = 4.0f;
            style.TabBorderSize = 1.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.49803922f, 0.49803922f, 0.49803922f, 1.0f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.11372549f, 0.11372549f, 0.11372549f, 1.0f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.078431375f, 0.078431375f, 0.078431375f, 0.94f);
            style.Colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.16309011f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.08627451f, 0.08627451f, 0.08627451f, 1.0f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15294118f, 0.15294118f, 0.15294118f, 1.0f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.1882353f, 0.1882353f, 0.1882353f, 1.0f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.11372549f, 0.11372549f, 0.11372549f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.105882354f, 0.105882354f, 0.105882354f, 1.0f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.51f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.11372549f, 0.11372549f, 0.11372549f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.019607844f, 0.019607844f, 0.019607844f, 0.53f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30980393f, 0.30980393f, 0.30980393f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40784314f, 0.40784314f, 0.40784314f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50980395f, 0.50980395f, 0.50980395f, 1.0f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.8784314f, 0.8784314f, 0.8784314f, 1.0f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.98039216f, 0.98039216f, 0.98039216f, 1.0f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.14901961f, 0.14901961f, 0.14901961f, 1.0f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24705882f, 0.24705882f, 0.24705882f, 1.0f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.32941177f, 0.32941177f, 0.32941177f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.9764706f, 0.9764706f, 0.9764706f, 0.30980393f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.9764706f, 0.9764706f, 0.9764706f, 0.8f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.9764706f, 0.9764706f, 0.9764706f, 1.0f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.42745098f, 0.42745098f, 0.49803922f, 0.5f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.7490196f, 0.7490196f, 0.7490196f, 0.78039217f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.7490196f, 0.7490196f, 0.7490196f, 1.0f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.9764706f, 0.9764706f, 0.9764706f, 0.2f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.9372549f, 0.9372549f, 0.9372549f, 0.67058825f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.9764706f, 0.9764706f, 0.9764706f, 0.9490196f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.22352941f, 0.22352941f, 0.22352941f, 0.8627451f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.32156864f, 0.32156864f, 0.32156864f, 0.8f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.27450982f, 0.27450982f, 0.27450982f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.14509805f, 0.14509805f, 0.14509805f, 0.972549f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.42352942f, 0.42352942f, 0.42352942f, 1.0f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.60784316f, 0.60784316f, 0.60784316f, 1.0f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.42745098f, 0.34901962f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8980392f, 0.69803923f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.6f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882353f, 0.1882353f, 0.2f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.30980393f, 0.30980393f, 0.34901962f, 1.0f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.22745098f, 0.22745098f, 0.24705882f, 1.0f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25882354f, 0.5882353f, 0.9764706f, 0.35f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.9f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.25882354f, 0.5882353f, 0.9764706f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.35f);
        }
    }

    ImGuiManager::ImGuiManager(const ImGuiSetupInfo& a_info)
    {
        // imgui バージョン確認
        IMGUI_CHECKVERSION();
        std::string version = ImGui::GetVersion();
        Core::IO::log(Core::IO::LogSink::debugConsole, "ImGui version: {}", version.c_str());

        // imgui コンテキストの作成
        ImGui::CreateContext();

        // オプションの設定
        CUE_ASSERT(a_info.fileSystem != nullptr);
        m_layoutFilePath = resolve_layout_file_path(*a_info.fileSystem);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = m_layoutFilePath.c_str(); // レイアウト保存先設定
        io.ConfigFlags |= a_info.enableDocking ? ImGuiConfigFlags_DockingEnable : 0;
        io.ConfigFlags |= a_info.enableMultiViewport ? ImGuiConfigFlags_ViewportsEnable : 0;
        io.ConfigFlags |= a_info.enableKeyboardNavigation ? ImGuiConfigFlags_NavEnableKeyboard : 0;

        Result fontLoadResult = load_editor_fonts(*a_info.fileSystem, io, m_uiFont, m_codeFont);
        if (!fontLoadResult)
        {
            Core::IO::log(
                Core::IO::LogSink::debugConsole,
                "Failed to load custom ImGui fonts: %s",
                fontLoadResult.message.data());

            io.Fonts->Clear();
            m_uiFont = io.Fonts->AddFontDefault();
            m_codeFont = m_uiFont;
            io.FontDefault = m_uiFont;
        }

        // スタイルの設定
        ImGui::StyleColorsDark();
        setup_imgui_style();
        ImGuiStyle& style = ImGui::GetStyle();
        style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesFull; // ツリーノード線描画

        // プラットフォーム/レンダラーの初期化
        ImGui_ImplWin32_Init(a_info.hwnd);

        ImGui_ImplDX12_InitInfo initInfo = {};
        initInfo.Device = a_info.device;
        initInfo.CommandQueue = a_info.commandQueue;
        initInfo.NumFramesInFlight = a_info.bufferCount;
        initInfo.RTVFormat = a_info.rtvFormat;
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN; // dsv フォーマット未使用値設定
        initInfo.SrvDescriptorHeap = a_info.srvDescHeap;
        initInfo.UserData = a_info.backend;
        initInfo.SrvDescriptorAllocFn = imgui_srv_descriptor_alloc;
        initInfo.SrvDescriptorFreeFn = imgui_srv_descriptor_free;
        ImGui_ImplDX12_Init(&initInfo);
        m_isInitialized = true;
    }
    ImGuiManager::~ImGuiManager()
    {
        shutdown();
    }
    void ImGuiManager::shutdown()
    {
        if (!m_isInitialized)
        {
            return;
        }

        m_isBeginFrameCalled = false;

        // プラットフォーム/レンダラーのシャットダウン
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();

        // imgui コンテキスト破棄
        ImGui::DestroyContext();
        m_isInitialized = false;
    }
    Result ImGuiManager::begin_frame()
    {
        // すでにフレームが開始されているならエラー
        if (m_isBeginFrameCalled)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Frame already begun. Call end_frame() before beginning a new frame.");
        }

        // フレーム開始
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        m_isBeginFrameCalled = true;
        return Result::ok();
    }
    Result ImGuiManager::end_frame()
    {
        // フレームが開始されていないならエラー
        if (!m_isBeginFrameCalled)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Frame not begun. Call begin_frame() before ending the frame.");
        }

        // フレーム終了
        ImGui::EndFrame();
        return Result::ok();
    }
    Result ImGuiManager::render(ID3D12GraphicsCommandList* commandList)
    {
        // フレームが開始されていないならエラー
        if (!m_isBeginFrameCalled)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Frame not begun. Call begin_frame() before rendering the frame.");
        }

        // 描画コマンドを生成
        ImGui::Render();
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        // 描画データ取得
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData != nullptr && drawData->CmdListsCount != 0)
        {
            ImGui_ImplDX12_RenderDrawData(drawData, commandList);
        }
        m_isBeginFrameCalled = false; // フレーム終了状態へ更新

        return Result::ok();
    }
}
