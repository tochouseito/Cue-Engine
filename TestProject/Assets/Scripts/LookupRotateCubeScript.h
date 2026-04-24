#pragma once

#include <ScriptFramework/Marionette.h>
#include "RotateCubeScript.h"

MARIONETTE_DECLARE_SCRIPT_TYPE(LookupRotateCube, "LookupRotateCube");

class LookupRotateCube final : public Marionette::Behaviour<LookupRotateCube>
{
public:
    using StateBlob = Marionette::StateBlob<LookupRotateCube>;
    using Marionette::Behaviour<LookupRotateCube>::update;
    MARIONETTE_FIELDS(
        MARIONETTE_OBJECT_PTR_FIELD(RotateCube, "target")
    );

    void bind_fields(const Marionette::ScriptFieldReader& a_reader);
    void start();
    void update();

private:
    Marionette::ScriptObjectPtr<RotateCube> target{};
    bool hasTriggered = false;
};

[[nodiscard]] Cue::Core::Native::ScriptClassDefinition
make_lookup_rotate_cube_script_definition() noexcept;
