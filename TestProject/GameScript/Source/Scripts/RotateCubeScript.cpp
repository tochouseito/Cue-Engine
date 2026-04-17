#include "RotateCubeScript.h"

// === C++ includes ===
#include <string_view>

namespace
{
    inline constexpr float k_rotationSpeedRadiansPerSecond = 0.78539816339f;
}

class RotateCube final : public Marionette::Behaviour<RotateCube>
{
public:
    using StateBlob = Marionette::StateBlob<RotateCube>;
    using Marionette::Behaviour<RotateCube>::update;
    MARIONETTE_FIELDS(
        CUE_FIELD_FLOAT("rotationSpeed", k_rotationSpeedRadiansPerSecond)
    );

    void bind_fields(const Marionette::ScriptFieldReader& a_reader)
    {
        (void)read_float(a_reader, "rotationSpeed", rotationSpeed);
    }

    void start()
    {
    }

    void update(float a_deltaTimeSeconds)
    {
        if (!is_entity_valid() || !has_transform())
        {
            return;
        }

        Transform transform{};
        if (get_transform(transform) != CueResult_Ok)
        {
            return;
        }

        transform.rotation.y += a_deltaTimeSeconds * rotationSpeed;
        (void)set_transform(transform);
    }

private:
    float rotationSpeed = k_rotationSpeedRadiansPerSecond;
};

Cue::Core::Native::ScriptClassDefinition
make_rotate_cube_script_definition() noexcept
{
    return Marionette::make_script_definition<RotateCube>();
}
