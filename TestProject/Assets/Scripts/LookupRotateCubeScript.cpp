#include "LookupRotateCubeScript.h"

#include "RotateCubeScript.h"

void LookupRotateCube::bind_fields(
    const Marionette::ScriptFieldReader& a_reader)
{
    (void)read_object_ptr(a_reader, "target", target);
}

void LookupRotateCube::start()
{
}

void LookupRotateCube::update()
{
    if (hasTriggered)
    {
        return;
    }

    if (!target.is_set())
    {
        log_warning("LookupRotateCube.target is not assigned.");
        hasTriggered = true;
        return;
    }

    RotateCube* rotateCubeObject = target.get();
    if (rotateCubeObject == nullptr)
    {
        log_warning("LookupRotateCube.target could not resolve RotateCube.");
        hasTriggered = true;
        return;
    }

    const Marionette::ScriptRef<RotateCube> rotateCube = target.script_ref();
    if (!rotateCube.is_valid())
    {
        log_warning("LookupRotateCube.target script ref is invalid.");
        hasTriggered = true;
        return;
    }

    rotateCubeObject->toggle_direction();

    float rotationSpeed = 0.0f;
    if (!rotateCube.get_float("rotationSpeed", rotationSpeed))
    {
        hasTriggered = true;
        return;
    }

    if (rotationSpeed < 0.0f)
    {
        log_warning("RotateCube.rotationSpeed is negative.");
    }

    hasTriggered = true;
}

MARIONETTE_DEFINE_SCRIPT(lookup_rotate_cube, LookupRotateCube);
