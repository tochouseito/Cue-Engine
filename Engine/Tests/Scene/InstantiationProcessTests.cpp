#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Scene/Instantiation.h>
#include <Cue/Schema/Descriptor.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class ProcessFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief 期待したFatalをProcess終了Code 42へ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(42);
    }

    /// @brief 期待したMessage付きFatalをProcess終了Code 42へ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(42);
    }
};

class FixedIdentitySource final : public cue::scene::SceneIdentitySource
{
  public:
    /// @brief 末尾byteを進めた有効なUUID Version 4を返す
    [[nodiscard]] cue::scene::IdentityBytes next_identity() noexcept override
    {
        cue::scene::IdentityBytes bytes{};
        bytes[6] = 0x40U;
        bytes[8] = 0x80U;
        bytes[15] = m_next;
        ++m_next;
        return bytes;
    }

  private:
    std::uint8_t m_next = 1U;
};

/// @brief 失敗時に指定Exit CodeでProcessを終了して成功値を取り出す
template <typename T>
T take_value(cue::Result<T> &&a_result, int a_exitCode) noexcept
{
    if (!a_result)
    {
        std::_Exit(a_exitCode);
    }

    return std::move(*a_result.try_value());
}

/// @brief Test用Stable TypeIdを生成する
[[nodiscard]] cue::schema::TypeId make_type_id(
    std::string_view a_text, const cue::AssertContext &a_assertContext) noexcept
{
    return take_value(cue::schema::TypeId::parse(a_text, a_assertContext), 2);
}

/// @brief FieldなしTest用Type Descriptorを生成する
[[nodiscard]] cue::schema::TypeDescriptor make_descriptor(
    std::string_view a_typeId, std::string_view a_name,
    const cue::AssertContext &a_assertContext) noexcept
{
    std::vector<cue::schema::FieldDescriptor> fields;
    std::vector<cue::schema::FieldId> reservedFields;
    return take_value(cue::schema::create_type_descriptor(
                          make_type_id(a_typeId, a_assertContext), a_name,
                          take_value(cue::schema::SchemaVersion::create(
                                         1U, a_assertContext),
                                     3),
                          std::move(fields), std::move(reservedFields),
                          a_assertContext),
                      4);
}

/// @brief SceneInstance DestructorとMove所有権のProcess契約を実行する
[[nodiscard]] int run_process_test(std::string_view a_mode) noexcept
{
    ProcessFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    cue::schema::SchemaRegistryIdentitySource registryIdentitySource;
    cue::schema::SchemaRegistryBuilder registryBuilder(registryIdentitySource,
                                                       assertContext);

    if (!registryBuilder.add_type(make_descriptor(
            "10000000-0000-4000-8000-000000000001",
            "Cue.Scene.SceneObjectState", assertContext)) ||
        !registryBuilder.add_type(make_descriptor(
            "50000000-0000-4000-8000-000000000005",
            "Cue.Core.Transform", assertContext)))
    {
        return 5;
    }

    auto registry = registryBuilder.seal();

    if (!registry)
    {
        return 6;
    }

    FixedIdentitySource sceneIdentitySource;
    auto document = cue::scene::SceneDocument::create(
        take_value(cue::scene::SceneAssetId::generate(
                       sceneIdentitySource, assertContext),
                   7),
        assertContext);
    const auto objectId = take_value(cue::scene::ObjectId::generate(
                                         sceneIdentitySource, assertContext),
                                     8);

    if (!document.add_object(objectId, "Object", true, std::nullopt,
                             cue::math::Transform{}))
    {
        return 9;
    }

    auto snapshot = cue::scene::create_scene_snapshot(document, assertContext);

    if (!snapshot)
    {
        return 10;
    }

    cue::game_core::WorldIdentitySource worldIdentitySource;
    auto runtime = cue::game_core::RuntimeWorld::create(
        worldIdentitySource, **registry.try_value(),
        make_type_id("50000000-0000-4000-8000-000000000005",
                     assertContext),
        assertContext);

    if (runtime == nullptr || !runtime->initialize())
    {
        return 11;
    }

    auto *world = runtime->try_world();

    if (world == nullptr)
    {
        return 12;
    }

    auto stateType = world->register_component<cue::scene::SceneObjectState>(
        make_type_id("10000000-0000-4000-8000-000000000001",
                     assertContext));

    if (!stateType)
    {
        return 13;
    }

    std::vector<cue::scene::RuntimeComponentBuilder *> builders;
    auto firstResult = cue::scene::SceneInstantiator::instantiate(
        *snapshot.try_value(), *runtime, *stateType.try_value(), builders,
        assertContext);

    if (!firstResult)
    {
        return 14;
    }

    auto first = std::move(*firstResult.try_value());

    if (a_mode == "LiveDestructor")
    {
        return 0;
    }

    if (a_mode == "LiveMoveAssignment")
    {
        auto secondResult = cue::scene::SceneInstantiator::instantiate(
            *snapshot.try_value(), *runtime, *stateType.try_value(), builders,
            assertContext);

        if (!secondResult)
        {
            return 15;
        }

        auto second = std::move(*secondResult.try_value());
        first = std::move(second);
        return 0;
    }

    if (a_mode == "EndedDestructor")
    {
        auto endResult = first.end(*runtime, assertContext);
        return endResult && runtime->shutdown() ? 0 : 16;
    }

    if (a_mode == "MovedFromDestructor")
    {
        auto second = std::move(first);
        auto endResult = second.end(*runtime, assertContext);
        return endResult && runtime->shutdown() ? 0 : 17;
    }

    auto cleanup = first.end(*runtime, assertContext);
    static_cast<void>(cleanup);
    return 18;
}
} // namespace

/// @brief SceneInstanceのProcess終了を伴う所有権契約を検証する
int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 2)
    {
        return 19;
    }

    return run_process_test(a_arguments[1]);
}
