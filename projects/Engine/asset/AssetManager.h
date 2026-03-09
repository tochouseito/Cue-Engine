#pragma once
#include <Result.h>
#include <CueAssert.h>
#include <Registry.h>
#include <Native/EngineNativeStruct.h>

#include <unordered_map>

namespace Cue::Asset
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
            // 1) asset manager の registry を唯一の原本として扱い、呼び出し側にはコピーだけ返す。
            if (!m_modelRegistry.try_get(handle, outData))
            {
                return Result::fail(Facility::Core, Code::NotFound, Severity::Warning, 0, "Model handle is not alive.");
            }
            return Result::ok();
        }
    private:
        Result add_model(std::string_view name, const Core::Native::ModelData& data, ModelHandle& outHandle)
        {
            // 1) 同名モデルの重複登録を防ぎ、呼び出し側が安定した handle を扱えるようにする。
            const Core::ResourceNameId nameId = Core::fnv1a64(name);
            if (m_modelNameMap.contains(nameId))
            {
                return Result::fail(Facility::Core, Code::InvalidArg, Severity::Warning, 0, "Model name already exists.");
            }

            // 2) Core 側の SoA model data をそのまま registry に保持し、GPU upload 前の原本として使う。
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
