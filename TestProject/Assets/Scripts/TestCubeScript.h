#pragma once

#include <ScriptFramework/Marionette.h>

MARIONETTE_DECLARE_SCRIPT_TYPE(TestCube, "TestCube");

class TestCube final : public Marionette::Behaviour<TestCube>
{
public:
    using StateBlob = Marionette::StateBlob<TestCube>;
    using Marionette::Behaviour<TestCube>::update;
    MARIONETTE_NO_FIELDS();
    MARIONETTE_NO_FUNCTIONS();

    void start();
    void update();
};

[[nodiscard]] Cue::Core::Native::ScriptClassDefinition
make_test_cube_script_definition() noexcept;
