// MarionnetteObject の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace Cue
{
    class MarionnetteObject;

    enum class MarionnettePropertyType : uint8_t
    {
        Float,
        Int32,
        Bool,
        EntityRef,
        ClassRef,
    };

    enum class MarionnettePropertyReferenceRole : uint8_t
    {
        None,
        ScriptReferenceEntity,
        ScriptReferenceClass,
    };

    enum MarionnettePropertyFlags : uint32_t
    {
        MarionnettePropertyFlag_None = 0,
        MarionnettePropertyFlag_EditAnywhere = 1u << 0,
        MarionnettePropertyFlag_Serialize = 1u << 1,
        MarionnettePropertyFlag_ReadOnly = 1u << 2,
    };

    inline constexpr MarionnettePropertyFlags operator|(
        MarionnettePropertyFlags a_left,
        MarionnettePropertyFlags a_right) noexcept
    {
        return static_cast<MarionnettePropertyFlags>(
            static_cast<uint32_t>(a_left) |
            static_cast<uint32_t>(a_right));
    }

    [[nodiscard]] inline constexpr bool has_any_flags(
        MarionnettePropertyFlags a_flags,
        MarionnettePropertyFlags a_testFlags) noexcept
    {
        return (static_cast<uint32_t>(a_flags) &
                   static_cast<uint32_t>(a_testFlags)) !=
            0u;
    }

    struct MarionnetteProperty final
    {
        const char* name = nullptr;
        MarionnettePropertyType type = MarionnettePropertyType::Float;
        uint32_t offset = 0;
        MarionnettePropertyFlags flags = MarionnettePropertyFlag_None;
        const char* groupName = nullptr;
        MarionnettePropertyReferenceRole referenceRole =
            MarionnettePropertyReferenceRole::None;
    };

    struct MarionnetteFunction final
    {
        const char* name = nullptr;
        uint32_t flags = 0;
    };

    using MarionnetteCreateInstanceFn = MarionnetteObject* (*)();

    struct MarionnetteClass final
    {
        const char* name = nullptr;
        const MarionnetteClass* parent = nullptr;
        MarionnetteCreateInstanceFn createInstance = nullptr;
        const MarionnetteProperty* properties = nullptr;
        uint32_t propertyCount = 0;
        const MarionnetteFunction* functions = nullptr;
        uint32_t functionCount = 0;

        [[nodiscard]] bool is_child_of(
            const MarionnetteClass* a_baseClass) const noexcept
        {
            if (a_baseClass == nullptr)
            {
                return false;
            }

            const MarionnetteClass* currentClass = this;
            while (currentClass != nullptr)
            {
                if (currentClass == a_baseClass)
                {
                    return true;
                }

                if (currentClass->name != nullptr &&
                    a_baseClass->name != nullptr)
                {
                    const size_t currentNameLength =
                        std::strlen(currentClass->name);
                    const size_t baseNameLength =
                        std::strlen(a_baseClass->name);
                    if (currentNameLength == baseNameLength &&
                        std::strncmp(
                            currentClass->name,
                            a_baseClass->name,
                            currentNameLength) == 0)
                    {
                        return true;
                    }
                }

                currentClass = currentClass->parent;
            }

            return false;
        }
        [[nodiscard]] const MarionnetteProperty* find_property(
            std::string_view a_propertyName) const noexcept
        {
            if (a_propertyName.empty())
            {
                return nullptr;
            }

            for (const MarionnetteClass* currentClass = this;
                 currentClass != nullptr;
                 currentClass = currentClass->parent)
            {
                if (currentClass->properties == nullptr ||
                    currentClass->propertyCount == 0)
                {
                    continue;
                }

                for (uint32_t propertyIndex = 0;
                     propertyIndex < currentClass->propertyCount;
                     ++propertyIndex)
                {
                    const MarionnetteProperty& property =
                        currentClass->properties[propertyIndex];
                    if (property.name == nullptr)
                    {
                        continue;
                    }

                    const size_t propertyNameLength = std::strlen(property.name);
                    if (propertyNameLength != a_propertyName.size())
                    {
                        continue;
                    }

                    if (std::strncmp(
                            property.name,
                            a_propertyName.data(),
                            propertyNameLength) == 0)
                    {
                        return &property;
                    }
                }
            }

            return nullptr;
        }
        [[nodiscard]] const MarionnetteFunction* find_function(
            std::string_view a_functionName) const noexcept
        {
            if (a_functionName.empty())
            {
                return nullptr;
            }

            for (const MarionnetteClass* currentClass = this;
                 currentClass != nullptr;
                 currentClass = currentClass->parent)
            {
                if (currentClass->functions == nullptr ||
                    currentClass->functionCount == 0)
                {
                    continue;
                }

                for (uint32_t functionIndex = 0;
                     functionIndex < currentClass->functionCount;
                     ++functionIndex)
                {
                    const MarionnetteFunction& function =
                        currentClass->functions[functionIndex];
                    if (function.name == nullptr)
                    {
                        continue;
                    }

                    const size_t functionNameLength = std::strlen(function.name);
                    if (functionNameLength != a_functionName.size())
                    {
                        continue;
                    }

                    if (std::strncmp(
                            function.name,
                            a_functionName.data(),
                            functionNameLength) == 0)
                    {
                        return &function;
                    }
                }
            }

            return nullptr;
        }
    };

    class MarionnetteObject
    {
    public:
        virtual ~MarionnetteObject() = default;

        [[nodiscard]] static const MarionnetteClass* static_class() noexcept
        {
            static const MarionnetteClass k_marionnetteObjectClass{
                "MarionnetteObject",
                nullptr,
                nullptr,
                nullptr,
                0,
                nullptr,
                0
            };
            return &k_marionnetteObjectClass;
        }

        [[nodiscard]] virtual const MarionnetteClass* get_class() const noexcept
        {
            return static_class();
        }

        [[nodiscard]] bool is_a(
            const MarionnetteClass* a_baseClass) const noexcept
        {
            const MarionnetteClass* objectClass = get_class();
            if (objectClass == nullptr)
            {
                return false;
            }

            return objectClass->is_child_of(a_baseClass);
        }

        template <typename T>
        [[nodiscard]] bool is_a() const noexcept
        {
            return is_a(T::static_class());
        }
    };

    template <typename T>
    [[nodiscard]] T* cue_cast(MarionnetteObject* a_object) noexcept
    {
        if (a_object == nullptr || !a_object->is_a(T::static_class()))
        {
            return nullptr;
        }

        return static_cast<T*>(a_object);
    }

    template <typename T>
    [[nodiscard]] const T* cue_cast(const MarionnetteObject* a_object) noexcept
    {
        if (a_object == nullptr || !a_object->is_a(T::static_class()))
        {
            return nullptr;
        }

        return static_cast<const T*>(a_object);
    }
}
