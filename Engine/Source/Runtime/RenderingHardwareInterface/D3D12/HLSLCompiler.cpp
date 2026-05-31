#include "HLSLCompiler.h"

namespace Cue::RHI::DX12
{
    namespace
    {
        std::string get_module_directory_path()
        {
            constexpr DWORD k_modulePathCapacity = 4096;
            std::string modulePath(k_modulePathCapacity, '\0');
            const DWORD length =
                ::GetModuleFileNameA(nullptr, modulePath.data(), k_modulePathCapacity);
            if (length == 0 || length >= k_modulePathCapacity)
            {
                return {};
            }

            modulePath.resize(length);
            const size_t lastSeparator = modulePath.find_last_of("\\/");
            if (lastSeparator == std::string::npos)
            {
                return {};
            }

            return modulePath.substr(0, lastSeparator);
        }

        std::string join_path(std::string_view a_left, std::string_view a_right)
        {
            if (a_left.empty())
            {
                return std::string(a_right);
            }
            if (a_right.empty())
            {
                return std::string(a_left);
            }

            std::string result(a_left);
            if (result.back() != '/' && result.back() != '\\')
            {
                result += '/';
            }
            result += a_right;
            return result;
        }

        std::string_view strip_shader_root(std::string_view a_path)
        {
            constexpr std::string_view k_shaderRoot = "Shaders/";
            if (a_path.rfind(k_shaderRoot, 0) == 0)
            {
                return a_path.substr(k_shaderRoot.size());
            }

            return a_path;
        }

        std::string resolve_shader_path(std::string_view a_relativePath)
        {
            const std::string_view shaderRelativePath = strip_shader_root(a_relativePath);

#ifdef CUE_PROJECT_ROOT_PATH
            const std::string repositoryPath = join_path(
                join_path(std::string(CUE_PROJECT_ROOT_PATH), "Engine/Shaders"),
                shaderRelativePath);
#endif

            const std::string moduleDirectoryPath = get_module_directory_path();
            const std::string runtimePath =
                join_path(
                    join_path(moduleDirectoryPath, "EngineResources/Shaders"),
                    shaderRelativePath);

#ifdef CUE_DEBUG
            if (!repositoryPath.empty())
            {
                return repositoryPath;
            }
#endif

            if (!runtimePath.empty())
            {
                return runtimePath;
            }

#ifdef CUE_PROJECT_ROOT_PATH
            if (!repositoryPath.empty())
            {
                return repositoryPath;
            }
#endif

            return std::string(a_relativePath);
        }

        std::wstring shader_profile_to_wstring(D3D_SHADER_MODEL model)
        {
            switch (model)
            {
            case D3D_SHADER_MODEL_NONE:
                return L"Unknown Model";
                break;
            case D3D_SHADER_MODEL_5_1:
                return L"5_1";
                break;
            case D3D_SHADER_MODEL_6_0:
                return L"6_0";
                break;
            case D3D_SHADER_MODEL_6_1:
                return L"6_1";
                break;
            case D3D_SHADER_MODEL_6_2:
                return L"6_2";
                break;
            case D3D_SHADER_MODEL_6_3:
                return L"6_3";
                break;
            case D3D_SHADER_MODEL_6_4:
                return L"6_4";
                break;
            case D3D_SHADER_MODEL_6_5:
                return L"6_5";
                break;
            case D3D_SHADER_MODEL_6_6:
                return L"6_6";
                break;
            case D3D_SHADER_MODEL_6_7:
                return L"6_7";
                break;
            case D3D_SHADER_MODEL_6_8:
                return L"6_8";
                break;
            case D3D_SHADER_MODEL_6_9:
                return L"6_9";
                break;
            default:
                return L"Unknown Model";
                break;
            }
        }

    }

    HLSLCompiler::HLSLCompiler()
    {
        HRESULT hr = S_OK;
        // dxc utility 生成
        hr = DxcCreateInstance(
            CLSID_DxcUtils,
            IID_PPV_ARGS(&m_dxcUtils)
        );
        if (FAILED(hr))
        {
            CUE_ASSERT_MSG(false, "Failed to create DxcUtils instance.");
        }
        // dxc compiler 生成
        hr = DxcCreateInstance(
            CLSID_DxcCompiler,
            IID_PPV_ARGS(&m_dxcCompiler)
        );
        if (FAILED(hr))
        {
            CUE_ASSERT_MSG(false, "Failed to create DxcCompiler instance.");
        }
        // include handler 生成
        hr = m_dxcUtils->CreateDefaultIncludeHandler(&m_dxcIncludeHandler);
        if (FAILED(hr))
        {
            CUE_ASSERT_MSG(false, "Failed to create default include handler.");
        }
    }
    Result HLSLCompiler::compile_shader_raw(const ShaderCompileDesc& desc, ComPtr<IDxcBlob>* outBlob)
    {
        HRESULT hr = {};
        Result r{};
        ComPtr<IDxcBlobEncoding> pSource = nullptr;

        // 入力と出力先を検証し、失敗時は呼び出し側で扱える Result に正規化する
        if (outBlob == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Shader blob output must not be null.");
        }

        if (!desc)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Shader compile description is invalid.");
        }

        outBlob->Reset();

        // utf-8 からワイド文字列へ変換し、前段の入力不備を DXC 呼び出し前に止める
        std::wstring name = L"";
        r = PAL::Win::utf8_to_wide(desc.name, &name);
        if (!r)
        {
            return r;
        }
        std::wstring filePath = L"";
        const std::string resolvedFilePath = resolve_shader_path(desc.filePath);
        r = PAL::Win::utf8_to_wide(resolvedFilePath, &filePath);
        if (!r)
        {
            return r;
        }
        std::wstring entryPoint = L"";
        r = PAL::Win::utf8_to_wide(desc.entryPoint, &entryPoint);
        if (!r)
        {
            return r;
        }
        std::wstring targetProfile = L"";
        r = PAL::Win::utf8_to_wide(desc.targetProfile, &targetProfile);
        if (!r)
        {
            return r;
        }

        // シェーダーファイルの読込とコンパイルを行い、DXC の失敗を Result で返す
        hr = m_dxcUtils.Get()->LoadFile(filePath.c_str(), nullptr, &pSource);
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to load shader file.");
        }

        // コンパイル引数の設定
        DxcBuffer sourceBuffer;
        sourceBuffer.Ptr = pSource->GetBufferPointer();
        sourceBuffer.Size = pSource->GetBufferSize();
        sourceBuffer.Encoding = DXC_CP_UTF8;
        LPCWSTR arguments[] = {
            filePath.c_str(), // コンパイル対象 hlsl ファイル名
            L"-E", entryPoint.c_str(), // エントリーポイント指定
            L"-T", targetProfile.c_str(), // shader profile 設定
            L"-Zi", L"-Qembed_debug", // デバッグ情報埋込
            L"-Od", // 最適化無効
            L"-Zpr", // 行優先メモリレイアウト
        };

        // 可読性のために引数をベクターで管理
        std::vector<LPCWCH> args;
        args.push_back(filePath.c_str()); // コンパイル対象 hlsl ファイル名
        args.push_back(L"-E");
        args.push_back(entryPoint.c_str()); // エントリーポイント指定
        args.push_back(L"-T");
        args.push_back(targetProfile.c_str()); // shader profile 設定
        args.push_back(L"-Zpr"); // 行優先メモリレイアウト
        if (desc.enableDebugInfo)
        {
            args.push_back(L"-Zi");
            args.push_back(L"-Qembed_debug"); // デバッグ情報埋込
            args.push_back(L"-Od"); // デバッグビルド最適化無効
        }
        else
        {
            args.push_back(L"-O3"); // リリースビルド最適化最大
        }

        // シェーダーのコンパイル
        ComPtr<IDxcResult> pResult = nullptr;
        hr = m_dxcCompiler.Get()->Compile(
            &sourceBuffer, // 読込済みソース
            arguments, // コンパイル引数
            _countof(arguments), // 引数数
            m_dxcIncludeHandler.Get(), // include handler
            IID_PPV_ARGS(&pResult) // コンパイル結果
        );
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to compile shader.");
        }

        // コンパイル結果を検証し、成功時のみブロブを返す
        ComPtr<IDxcBlobUtf8> pErrors = nullptr;
        ComPtr<IDxcBlobUtf16> pErrorsUtf16;
        hr = pResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), &pErrorsUtf16);
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to read shader compiler errors.");
        }
        if (pErrors != nullptr && pErrors->GetStringLength() != 0)
        {
            return Result::fail(
                Code::CreateFailed,
                Severity::Error,
                pErrors->GetStringPointer());
        }

        // コンパイル結果からシェーダーオブジェクトを取得
        ComPtr<IDxcBlob> shaderBlob = nullptr;
        hr = pResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), &pErrorsUtf16);
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to get compiled shader object.");
        }

        *outBlob = shaderBlob;
        return Result::ok();
    }
}
