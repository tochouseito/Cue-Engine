#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Scene/Error.h>
#include <Cue/Scene/SceneDocument.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
class TestFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief Test中の通常FatalをProcess失敗へ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::abort();
    }

    /// @brief Test中の予期しないFatalをProcess失敗へ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::abort();
    }
};

class SequentialIdentitySource final : public cue::scene::SceneIdentitySource
{
  public:
    /// @brief 末尾byteだけを進める有効なUUID Version 4を返す
    [[nodiscard]] cue::scene::IdentityBytes next_identity() noexcept override
    {
        cue::scene::IdentityBytes bytes{};
        bytes[6] = 0x40U;
        bytes[8] = 0x80U;
        bytes[14] = static_cast<std::uint8_t>((m_next >> 8U) & 0xFFU);
        bytes[15] = static_cast<std::uint8_t>(m_next & 0xFFU);
        ++m_next;
        return bytes;
    }

  private:
    std::uint16_t m_next = 1U;
};

/// @brief 条件が偽ならTest Processを失敗終了する
void require(bool a_condition) noexcept
{
    if (!a_condition)
    {
        std::abort();
    }
}

/// @brief 成功Resultから所有Valueを取り出す
template <typename T> T take_value(cue::Result<T> &&a_result) noexcept
{
    require(a_result.has_value());
    return std::move(*a_result.try_value());
}

/// @brief SceneDocumentのStable IDとHierarchy操作を検証する
void test_scene_document() noexcept
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    SequentialIdentitySource identitySource;

    auto sceneId = take_value(cue::scene::SceneAssetId::generate(
        identitySource, assertContext));
    const auto sceneText = sceneId.canonical_text();
    auto parsedSceneId = take_value(cue::scene::SceneAssetId::parse(
        std::string_view(sceneText.data(), sceneText.size()), assertContext));
    require(sceneId == parsedSceneId);

    auto rootId = take_value(cue::scene::ObjectId::generate(
        identitySource, assertContext));
    auto childId = take_value(cue::scene::ObjectId::generate(
        identitySource, assertContext));
    auto grandchildId = take_value(cue::scene::ObjectId::generate(
        identitySource, assertContext));
    auto missingId = take_value(cue::scene::ObjectId::generate(
        identitySource, assertContext));

    auto document = cue::scene::SceneDocument::create(std::move(sceneId),
                                                       assertContext);
    require(document.add_object(rootId, "Root", true, std::nullopt,
                                cue::math::Transform{})
                .has_value());
    require(document.add_object(childId, "Child", true, rootId,
                                cue::math::Transform{})
                .has_value());
    require(document.add_object(grandchildId, "Grandchild", false, childId,
                                cue::math::Transform{})
                .has_value());
    require(document.object_count() == 3U);
    require(document.validate().has_value());

    const auto duplicate = document.add_object(rootId, "Duplicate", true,
                                               std::nullopt,
                                               cue::math::Transform{});
    require(!duplicate.has_value());
    require(duplicate.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::scene::SceneError::DuplicateObjectId));
    require(document.object_count() == 3U);

    const auto dangling = document.set_parent(childId, missingId);
    require(!dangling.has_value());
    require(dangling.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::scene::SceneError::DanglingParent));
    require(*document.find_object(childId)->try_parent_id() == rootId);

    const auto cycle = document.set_parent(rootId, grandchildId);
    require(!cycle.has_value());
    require(cycle.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::scene::SceneError::HierarchyCycle));
    require(document.find_object(rootId)->try_parent_id() == nullptr);

    require(document.rename_object(childId, "Renamed").has_value());
    require(document.set_active(childId, false).has_value());
    require(document.find_object(childId)->name() == "Renamed");
    require(!document.find_object(childId)->is_active());

    auto checkpoint = document.create_checkpoint();
    require(document.rename_object(childId, "CheckpointChanged").has_value());
    require(document.restore_checkpoint(std::move(checkpoint)).has_value());
    require(document.find_object(childId)->name() == "Renamed");
    const auto consumedCheckpoint = document.restore_checkpoint(std::move(checkpoint));
    require(!consumedCheckpoint.has_value());
    require(consumedCheckpoint.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::scene::SceneError::InvalidCheckpoint));
    require(document.find_object(childId)->name() == "Renamed");
    require(document.validate().has_value());

    const auto removeParent = document.remove_object(rootId);
    require(!removeParent.has_value());
    require(removeParent.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::scene::SceneError::ChildObjectsExist));

    require(document.remove_object(grandchildId).has_value());
    require(document.remove_object(childId).has_value());
    require(document.remove_object(rootId).has_value());
    require(document.object_count() == 0U);
    require(document.validate().has_value());
}

/// @brief 不正UUID入力と不正Identity Sourceを拒否することを検証する
void test_identity_validation() noexcept
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);

    const auto uppercase = cue::scene::ObjectId::parse(
        "00000000-0000-4000-8000-00000000000A", assertContext);
    require(!uppercase.has_value());
    const auto nil = cue::scene::ObjectId::parse(
        "00000000-0000-0000-0000-000000000000", assertContext);
    require(!nil.has_value());
}

/// @brief 公開操作がHierarchy Depth上限を超える変更を拒否することを検証する
void test_hierarchy_depth_limit() noexcept
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    SequentialIdentitySource identitySource;
    auto sceneId = take_value(cue::scene::SceneAssetId::generate(
        identitySource, assertContext));
    auto document = cue::scene::SceneDocument::create(std::move(sceneId),
                                                       assertContext);
    auto parentId = take_value(cue::scene::ObjectId::generate(
        identitySource, assertContext));
    require(document.add_object(parentId, "Root", true, std::nullopt,
                                cue::math::Transform{})
                .has_value());
    std::optional<cue::scene::ObjectId> depth255Id;

    for (std::size_t depth = 2U;
         depth <= cue::scene::SceneDocument::maximum_hierarchy_depth();
         ++depth)
    {
        auto childId = take_value(cue::scene::ObjectId::generate(
            identitySource, assertContext));
        require(document.add_object(childId, "Nested", true, parentId,
                                    cue::math::Transform{})
                    .has_value());
        if (depth == cue::scene::SceneDocument::maximum_hierarchy_depth() - 1U)
        {
            depth255Id = childId;
        }
        parentId = std::move(childId);
    }

    auto tooDeepId = take_value(cue::scene::ObjectId::generate(
        identitySource, assertContext));
    const auto tooDeep = document.add_object(
        tooDeepId, "TooDeep", true, parentId, cue::math::Transform{});
    require(!tooDeep.has_value());
    require(tooDeep.try_error()->code().value() == static_cast<std::int64_t>(
               cue::scene::SceneError::HierarchyDepthExceeded));
    require(document.object_count() ==
            cue::scene::SceneDocument::maximum_hierarchy_depth());

    auto subtreeRootId = take_value(cue::scene::ObjectId::generate(
        identitySource, assertContext));
    auto subtreeChildId = take_value(cue::scene::ObjectId::generate(
        identitySource, assertContext));
    require(document.add_object(subtreeRootId, "SubtreeRoot", true,
                                std::nullopt, cue::math::Transform{})
                .has_value());
    require(document.add_object(subtreeChildId, "SubtreeChild", true,
                                subtreeRootId, cue::math::Transform{})
                .has_value());
    require(depth255Id.has_value());
    const auto deepReparent = document.set_parent(subtreeRootId, depth255Id);
    require(!deepReparent.has_value());
    require(deepReparent.try_error()->code().value() ==
            static_cast<std::int64_t>(
                cue::scene::SceneError::HierarchyDepthExceeded));
    require(document.find_object(subtreeRootId)->try_parent_id() == nullptr);
    require(document.validate().has_value());
}

/// @brief Object上限到達後の追加を診断付きで拒否しDocumentを維持することを検証する
void test_object_count_limit() noexcept
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    SequentialIdentitySource identitySource;
    auto document = cue::scene::SceneDocument::create(
        take_value(cue::scene::SceneAssetId::generate(
            identitySource, assertContext)),
        assertContext);

    for (std::size_t index = 0U;
         index < cue::scene::k_maximumSceneObjectCount; ++index)
    {
        auto objectId = take_value(cue::scene::ObjectId::generate(
            identitySource, assertContext));
        require(document.add_object(objectId, "Object", true, std::nullopt,
                                    cue::math::Transform{})
                    .has_value());
    }

    auto rejectedId = take_value(cue::scene::ObjectId::generate(
        identitySource, assertContext));
    const auto rejected = document.add_object(
        rejectedId, "Rejected", true, std::nullopt, cue::math::Transform{});
    require(!rejected.has_value());
    require(rejected.try_error()->code().value() ==
            static_cast<std::int64_t>(
                cue::scene::SceneError::ResourceLimitExceeded));
    require(!rejected.try_error()->summary().empty());
    require(document.object_count() == cue::scene::k_maximumSceneObjectCount);
    require(document.find_object(rejectedId) == nullptr);
    require(document.validate().has_value());
}
} // namespace

/// @brief Cue.Scene Stable IDとSceneDocumentのUnit Testを実行する
int main()
{
    test_scene_document();
    test_identity_validation();
    test_hierarchy_depth_limit();
    test_object_count_limit();
    return 0;
}
