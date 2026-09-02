#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/GameCore/World.h>
#include <Cue/Schema/Descriptor.h>
#include <Cue/Schema/Registry.h>
#include <Cue/Schema/Types.h>

#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class ProcessFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief 期待した再入拒否を Process の非zero終了へ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(42);
    }

    /// @brief 診断Message付きの再入拒否を Process の非zero終了へ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(42);
    }
};

class ReentrantDestructorComponent final
{
  public:
    /// @brief Component 破棄中に同じ World の Structural API を呼ぶ検証対象を構築する
    ReentrantDestructorComponent(cue::game_core::World &a_world,
                                 cue::game_core::EntityHandle a_entity) noexcept
        : m_world(&a_world), m_entity(a_entity), m_isOwner(true)
    {
    }

    /// @brief 再入検証責任の複製を禁止する
    ReentrantDestructorComponent(const ReentrantDestructorComponent &) = delete;
    /// @brief 再入検証責任の複製代入を禁止する
    ReentrantDestructorComponent &operator=(const ReentrantDestructorComponent &) = delete;

    /// @brief Storageへの移動時に再入検証責任を一度だけ移す
    ReentrantDestructorComponent(ReentrantDestructorComponent &&a_other) noexcept
        : m_world(a_other.m_world), m_entity(a_other.m_entity),
          m_isOwner(a_other.m_isOwner)
    {
        a_other.m_isOwner = false;
    }

    /// @brief Component が Move 代入を要求されないことを固定する
    ReentrantDestructorComponent &operator=(ReentrantDestructorComponent &&) = delete;

    /// @brief 破棄中の再入が成功する誤実装を Process 成功として露出させる
    ~ReentrantDestructorComponent() noexcept
    {
        if (m_isOwner)
        {
            auto result = m_world->create_entity();
            static_cast<void>(result);
        }
    }

  private:
    cue::game_core::World *m_world;
    cue::game_core::EntityHandle m_entity;
    bool m_isOwner;
};

/// @brief Test用の検証済みTypeIdを生成する
[[nodiscard]] cue::schema::TypeId make_type_id(
    std::string_view a_text, const cue::AssertContext &a_assertContext)
{
    auto result = cue::schema::TypeId::parse(a_text, a_assertContext);

    if (!result)
    {
        std::_Exit(2);
    }

    return std::move(*result.try_value());
}

/// @brief Component DestructorからのStructural Mutation再入拒否をProcess単位で検証する
[[nodiscard]] int run_reentrant_destructor_test() noexcept
{
    ProcessFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    cue::schema::SchemaRegistryIdentitySource schemaIdentitySource;
    cue::schema::SchemaRegistryBuilder builder(schemaIdentitySource,
                                                assertContext);
    auto version = cue::schema::SchemaVersion::create(1U, assertContext);

    if (!version)
    {
        return 3;
    }

    std::vector<cue::schema::FieldDescriptor> fields;
    std::vector<cue::schema::FieldId> reservedFieldIds;
    const auto typeId = make_type_id(
        "30000000-0000-4000-8000-000000000003", assertContext);
    auto descriptor = cue::schema::create_type_descriptor(
        typeId, "Cue.Test.ReentrantDestructor", std::move(*version.try_value()),
        std::move(fields), std::move(reservedFieldIds), assertContext);

    if (!descriptor || !builder.add_type(std::move(*descriptor.try_value())))
    {
        return 4;
    }

    auto registry = builder.seal();

    if (!registry)
    {
        return 5;
    }

    cue::game_core::WorldIdentitySource worldIdentitySource;
    auto world = cue::game_core::World::create(
        worldIdentitySource, **registry.try_value(), assertContext);

    if (!world)
    {
        return 6;
    }

    auto componentType = (*world.try_value())->register_component<
        ReentrantDestructorComponent>(typeId);
    auto entity = (*world.try_value())->create_entity();

    if (!componentType || !entity)
    {
        return 7;
    }

    auto component = (*world.try_value())->add_component(
        *componentType.try_value(), *entity.try_value(), **world.try_value(),
        *entity.try_value());

    if (!component)
    {
        return 8;
    }

    auto destroy = (*world.try_value())->destroy_entity(*entity.try_value());
    static_cast<void>(destroy);
    return 0;
}
} // namespace

/// @brief GameCoreのProcess終了を伴う失敗契約を検証する
int main()
{
    return run_reentrant_destructor_test();
}
