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
    private:
        Result add_model(std::string_view name, const Core::Native::ModelData& data, ModelHandle& outHandle)
        {
            return Result::ok();
        }
    private:
        Core::Registry<ModelTag, Core::Native::ModelData> m_modelRegistry;
        std::unordered_map<Core::ResourceNameId, ModelHandle> m_modelNameMap;
    };
}
