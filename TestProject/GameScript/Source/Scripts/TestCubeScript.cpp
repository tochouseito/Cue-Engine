#include <Framework/Marionette.h>

// === C++ includes ===
#include <string_view>

namespace
{
    class TestCube final : public Marionette::Behaviour<TestCube>
    {
    public:
        using StateBlob = Marionette::StateBlob<TestCube>;
        using Marionette::Behaviour<TestCube>::update;
        static constexpr std::string_view k_className = "TestCube";
        MARIONETTE_NO_FIELDS();

        void start()
        {
        }

        void update(float)
        {
        }
    };
}

Cue::Core::Native::ScriptClassDefinition
make_test_cube_script_definition() noexcept
{
    return Marionette::make_script_definition<TestCube>();
}
