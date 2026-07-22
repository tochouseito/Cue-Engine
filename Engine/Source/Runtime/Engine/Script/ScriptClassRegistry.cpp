#include "ScriptClassRegistry.h"

// === C++ includes ===
#include <algorithm>

namespace Cue::Script
{
    ScriptClassRegistry& ScriptClassRegistry::instance() noexcept
    {
        static ScriptClassRegistry registry{};
        return registry;
    }

    void ScriptClassRegistry::register_class(Core::Native::ScriptClassDefinition a_definition)
    {
        if (a_definition.className.data == nullptr || a_definition.className.size == 0u)
        {
            m_isValid = false;
            return;
        }

        const std::string_view className(
            a_definition.className.data, a_definition.className.size);
        if (className.empty() || has_class(className))
        {
            m_isValid = false;
            return;
        }

        m_classDefinitions.push_back(a_definition);
    }

    bool ScriptClassRegistry::is_valid() const noexcept
    {
        return m_isValid;
    }

    bool ScriptClassRegistry::has_class(std::string_view a_className) const noexcept
    {
        return std::any_of(
            m_classDefinitions.begin(),
            m_classDefinitions.end(),
            [a_className](const Core::Native::ScriptClassDefinition& a_definition)
            {
                if (a_definition.className.data == nullptr)
                {
                    return false;
                }

                return std::string_view(
                           a_definition.className.data,
                           a_definition.className.size) == a_className;
            });
    }

    uint32_t ScriptClassRegistry::class_count() const noexcept
    {
        return static_cast<uint32_t>(m_classDefinitions.size());
    }

    const char* ScriptClassRegistry::class_name(uint32_t a_index) const noexcept
    {
        return a_index < m_classDefinitions.size()
                   ? m_classDefinitions[a_index].className.data
                   : nullptr;
    }

    std::span<const Core::Native::ScriptClassDefinition>
    ScriptClassRegistry::class_definitions() const noexcept
    {
        return std::span<const Core::Native::ScriptClassDefinition>(
            m_classDefinitions.data(), m_classDefinitions.size());
    }

    ScriptClassRegistration::ScriptClassRegistration(
        Core::Native::ScriptClassDefinition a_definition)
    {
        ScriptClassRegistry::instance().register_class(a_definition);
    }
} // namespace Cue::Script
