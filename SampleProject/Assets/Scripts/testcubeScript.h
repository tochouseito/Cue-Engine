#pragma once

#include <Script/MarionnetteBehaviour.h>

namespace GameScript
{
    class testcubeScript final : public Cue::Script::MarionnetteBehaviour
    {
    public:
        using MarionnetteBehaviour::MarionnetteBehaviour;

        struct SavedState final
        {
            float elapsedSeconds = 0.0f;
        };

        static constexpr uint32_t k_stateVersion = 1u;

        float rotationSpeed = 1.0f;

        void start() noexcept override;
        void update(float a_deltaTimeSeconds) noexcept override;
        void reset_rotation() noexcept;
        void toggle_height() noexcept;
        void save_state(SavedState& a_outState) const noexcept;
        void restore_state(const SavedState& a_state) noexcept;

        MARIONETTE_FIELDS(
            MARIONETTE_FIELD(testcubeScript, rotationSpeed, 1.0f))
        MARIONETTE_FUNCTIONS(
            MARIONETTE_FUNCTION(testcubeScript, reset_rotation),
            MARIONETTE_FUNCTION(testcubeScript, toggle_height))

    private:
        float m_elapsedSeconds = 0.0f;
    };
} // namespace GameScript

MARIONETTE_DECLARE_SCRIPT_TYPE(
    GameScript::testcubeScript, "testcubeScript");
