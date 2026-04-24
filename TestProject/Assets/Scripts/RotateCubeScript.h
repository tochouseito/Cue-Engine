#pragma once

#include <ScriptFramework/Marionette.h>

MARIONETTE_DECLARE_SCRIPT_TYPE(RotateCube, "RotateCube");

class RotateCube final : public Marionette::Behaviour<RotateCube>
{
public:
    using StateBlob = Marionette::StateBlob<RotateCube>;
    using Marionette::Behaviour<RotateCube>::update;
    MARIONETTE_FIELDS(
        CUE_FIELD_FLOAT_META(
            "Rotation",
            "rotationSpeed",
            0.78539816339f,
            Marionette::EditAnywhere | Marionette::Serialize)
    );
    MARIONETTE_FUNCTIONS(
        MARIONETTE_FUNCTION(RotateCube, reset_rotation),
        MARIONETTE_FUNCTION(RotateCube, toggle_direction)
    );

    void bind_fields(const Marionette::ScriptFieldReader& a_reader);
    void start();
    void update();
    void reset_rotation();
    void toggle_direction();

private:
    float rotationSpeed = 0.78539816339f;
    float rotationDirection = 1.0f;
};

[[nodiscard]] Cue::Core::Native::ScriptClassDefinition
make_rotate_cube_script_definition() noexcept;
