#pragma once

// === Base Includes ===
#include <Result.h>
#include <CueAssert.h>

// === Core Includes ===
#include <Native/Handle.h>
#include <Native/EngineNativeStruct.h>
#include <Container/Registry.h>

// === C++ Includes ===
#include <cstdint>
#include <unordered_map>

namespace Cue
{
    struct ModelTag {};
    using ModelHandle = Core::Handle<ModelTag>;

    class AssetManager final
    {
    public:
        AssetManager() = default;
        ~AssetManager() = default;
        Result create_cube_model(ModelHandle& outHandle);
        Result get_model(ModelHandle handle, Core::Native::ModelData& outData) const
        {
            // asset manager の registry を唯一の原本として扱い、呼び出し側にはコピーだけ返す。
            if (!m_modelRegistry.try_copy_get(handle, outData))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Model not found for the given handle.");
            }
            return Result::ok();
        }
    private:
        Result add_model(std::string_view name, const Core::Native::ModelData& data, ModelHandle& outHandle)
        {
            // 同名モデルの重複登録を防ぎ、呼び出し側が安定した handle を扱えるようにする。
            const Core::ResourceNameId nameId = Core::fnv1a64(name);
            if (m_modelNameMap.contains(nameId))
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Model with the same name already exists.");
            }

            // Core 側の SoA model data をそのまま registry に保持し、GPU upload 前の原本として使う。
            Core::Native::ModelData modelCopy = data;
            outHandle = m_modelRegistry.create(modelCopy);
            m_modelNameMap.emplace(nameId, outHandle);
            return Result::ok();
        }
    private:
        Core::Registry<ModelTag, Core::Native::ModelData> m_modelRegistry;
        std::unordered_map<Core::ResourceNameId, ModelHandle> m_modelNameMap;
    };
}
