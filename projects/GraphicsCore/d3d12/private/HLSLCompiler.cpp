#include "HLSLCompiler.h"

namespace Cue::GraphicsCore::DX12
{
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
    HLSLCompiler::HLSLCompiler()
    {
        HRESULT hr = S_OK;
        // DXC ユーティリティの生成
        hr = DxcCreateInstance(
            CLSID_DxcUtils,
            IID_PPV_ARGS(&m_dxcUtils)
        );
        if (FAILED(hr))
        {
            Assert::cue_assert(false, "Failed to create DxcUtils instance.");
        }
        // DXC コンパイラの生成
        hr = DxcCreateInstance(
            CLSID_DxcCompiler,
            IID_PPV_ARGS(&m_dxcCompiler)
        );
        if (FAILED(hr))
        {
            Assert::cue_assert(false, "Failed to create DxcCompiler instance.");
        }
        // インクルードハンドラの生成
        hr = m_dxcUtils->CreateDefaultIncludeHandler(&m_dxcIncludeHandler);
        if (FAILED(hr))
        {
            Assert::cue_assert(false, "Failed to create default include handler.");
        }
    }
    ComPtr<IDxcBlob> HLSLCompiler::compile_shader_raw(const ShaderCompileDesc& desc)
    {
        HRESULT hr = {};
        ComPtr<IDxcBlobEncoding> pSource = nullptr;

        std::wstring name = to_utf16(desc.name);
        std::wstring filePath = to_utf16(desc.filePath);
        std::wstring entryPoint = to_utf16(desc.entryPoint);
        std::wstring targetProfile = to_utf16(desc.targetProfile);

        hr = m_dxcUtils.Get()->LoadFile(filePath.c_str(), nullptr, &pSource);
        if (FAILED(hr))
        {
            std::string errorMessage = "Failed to load shader file: " + to_utf8(filePath) + " (HRESULT: 0x" + std::to_string(hr) + ")";
            Assert::cue_assert(false, errorMessage.c_str());
        }
        DxcBuffer sourceBuffer;
        sourceBuffer.Ptr = pSource->GetBufferPointer();
        sourceBuffer.Size = pSource->GetBufferSize();
        sourceBuffer.Encoding = DXC_CP_UTF8;
        LPCWSTR arguments[] = {
            filePath.c_str(),              //コンパイル対象のhlslファイル名
            L"-E",entryPoint.c_str(),                      // エントリーポイントの指定。基本的にmain以外にはしない
            L"-T",targetProfile.c_str(),         // ShaderProfileの設定
            L"-Zi",L"-Qembed_debug",            // デバッグ用の情報を埋め込む
            L"-Od",                             // 最適化を外しておく
            L"-Zpr",                            // メモリレイアウトは行優先
        };

        std::vector<LPCWCH> args;
        args.push_back(filePath.c_str());// コンパイル対象のhlslファイル名
        args.push_back(L"-E");
        args.push_back(entryPoint.c_str());// エントリーポイントの指定。基本的にmain以外にはしない
        args.push_back(L"-T");
        args.push_back(targetProfile.c_str());// ShaderProfileの設定
        args.push_back(L"-Zpr");// メモリレイアウトは行優先
        if (desc.enableDebugInfo)
        {
            args.push_back(L"-Zi");
            args.push_back(L"-Qembed_debug");// デバッグ情報埋め込み
            args.push_back(L"-Od");// デバッグビルドなら最適化外す
        }
        else
        {
            args.push_back(L"-O3");// リリースビルドなら最適化最大
        }

        ComPtr<IDxcResult> pResult = nullptr;
        hr = m_dxcCompiler.Get()->Compile(
            &sourceBuffer,			// 読み込んだファイル
            arguments,				// コンパイルオプション
            _countof(arguments),	// コンパイル結果
            m_dxcIncludeHandler.Get(),// includeが含まれた諸々
            IID_PPV_ARGS(&pResult)	// コンパイル結果
        );
        if (FAILED(hr))
        {
            std::string errorMessage = "Failed to compile shader: " + to_utf8(filePath) + " (HRESULT: 0x" + std::to_string(hr) + ")";
            Assert::cue_assert(false, errorMessage.c_str());
        }
        ComPtr<IDxcBlobUtf8> pErrors = nullptr;
        ComPtr<IDxcBlobUtf16> pErrorsUtf16;
        hr = pResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), &pErrorsUtf16);
        if (pErrors != nullptr && pErrors->GetStringLength() != 0)
        {
            std::string str = pErrors->GetStringPointer();
            Assert::cue_assert(false, ("Shader compilation failed with errors:" + str).c_str());
            
        }
        IDxcBlob* pShader = nullptr;
        hr = pResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pShader), &pErrorsUtf16);
        if (FAILED(hr))
        {
            Assert::cue_assert(false, "Failed to get compiled shader object.");
        }
        return pShader;
    }
}
