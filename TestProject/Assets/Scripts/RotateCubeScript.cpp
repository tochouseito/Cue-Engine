#include "RotateCubeScript.h"

void RotateCube::bind_fields(const Marionette::ScriptFieldReader& a_reader)
{
    (void)read_float(a_reader, "rotationSpeed", rotationSpeed);
}

void RotateCube::start()
{
}

void RotateCube::update()
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

    transform.rotation.y +=
        delta_time() * rotationSpeed * rotationDirection;
    (void)set_transform(transform);
}

void RotateCube::reset_rotation()
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

    transform.rotation = { 0.0f, 0.0f, 0.0f };
    (void)set_transform(transform);
}

void RotateCube::toggle_direction()
{
    rotationDirection *= -1.0f;
}

MARIONETTE_DEFINE_SCRIPT(rotate_cube, RotateCube);
