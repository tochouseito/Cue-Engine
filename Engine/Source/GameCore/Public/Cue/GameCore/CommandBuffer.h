#pragma once

#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Result.h>
#include <Cue/GameCore/World.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cue::game_core
{
/// @brief 一つのCommand Buffer内だけでDeferred Entityを識別するCapability
class PendingEntityId final
{
  public:
    /// @brief Bufferから発行されていないPending IDの生成を禁止する
    PendingEntityId() = delete;
    /// @brief 同じBuffer内の後続CommandへPending IDを渡す
    PendingEntityId(const PendingEntityId &) noexcept = default;
    /// @brief Pending IDを複製代入する
    PendingEntityId &operator=(const PendingEntityId &) noexcept = default;
    /// @brief Pending IDを移動する
    PendingEntityId(PendingEntityId &&) noexcept = default;
    /// @brief Pending IDを移動代入する
    PendingEntityId &operator=(PendingEntityId &&) noexcept = default;
    /// @brief 非所有Capability値を破棄する
    ~PendingEntityId() = default;

    /// @brief Buffer内のnon-zero連番を診断用に返す
    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return m_value;
    }

  private:
    friend class StructuralCommandBuffer;

    /// @brief Bufferが現在Flush世代へ結び付けたPending IDを発行する
    PendingEntityId(const void *a_buffer, std::uint64_t a_generation,
                    std::uint64_t a_value) noexcept
        : m_buffer(a_buffer), m_generation(a_generation), m_value(a_value)
    {
    }

    const void *m_buffer;
    std::uint64_t m_generation;
    std::uint64_t m_value;
};

/// @brief Structural Commandの種類を順序付きReportで識別する
enum class StructuralCommandKind : std::uint8_t
{
    CreateEntity,
    DestroyEntity,
    AddComponent,
    RemoveComponent
};

/// @brief 一件のStructural Command適用結果を所有する
class StructuralCommandResult final
{
  public:
    /// @brief Command種類と任意の生成Entityを持つ成功結果を構築する
    [[nodiscard]] static StructuralCommandResult success(
        StructuralCommandKind a_kind,
        std::optional<EntityHandle> a_createdEntity = std::nullopt) noexcept
    {
        return StructuralCommandResult(a_kind, std::move(a_createdEntity),
                                       std::nullopt);
    }

    /// @brief Command種類と分類済みErrorを持つ失敗結果を構築する
    [[nodiscard]] static StructuralCommandResult failure(
        StructuralCommandKind a_kind, Error &&a_error) noexcept
    {
        return StructuralCommandResult(a_kind, std::nullopt,
                                       std::optional<Error>(std::move(a_error)));
    }

    /// @brief Move-only Error所有権の複製を禁止する
    StructuralCommandResult(const StructuralCommandResult &) = delete;
    /// @brief Move-only Error所有権の複製代入を禁止する
    StructuralCommandResult &operator=(const StructuralCommandResult &) = delete;
    /// @brief Command結果の所有権を移動する
    StructuralCommandResult(StructuralCommandResult &&) noexcept = default;
    /// @brief Command結果の所有権を移動代入する
    StructuralCommandResult &operator=(StructuralCommandResult &&) noexcept = default;
    /// @brief Command結果が所有するErrorを破棄する
    ~StructuralCommandResult() = default;

    /// @brief 適用したCommand種類を返す
    [[nodiscard]] StructuralCommandKind kind() const noexcept
    {
        return m_kind;
    }

    /// @brief Command適用が成功した場合にtrueを返す
    [[nodiscard]] bool succeeded() const noexcept
    {
        return !m_error.has_value();
    }

    /// @brief Create成功時のEntity Handleを返し、それ以外はnullptrを返す
    [[nodiscard]] const EntityHandle *try_created_entity() const noexcept
    {
        return m_createdEntity ? &m_createdEntity.value() : nullptr;
    }

    /// @brief 失敗時のErrorを返し、成功時はnullptrを返す
    [[nodiscard]] const Error *try_error() const noexcept
    {
        return m_error ? &m_error.value() : nullptr;
    }

  private:
    /// @brief Command結果を成功ValueまたはErrorの一方から構築する
    StructuralCommandResult(StructuralCommandKind a_kind,
                            std::optional<EntityHandle> a_createdEntity,
                            std::optional<Error> a_error) noexcept
        : m_kind(a_kind), m_createdEntity(std::move(a_createdEntity)),
          m_error(std::move(a_error))
    {
    }

    StructuralCommandKind m_kind;
    std::optional<EntityHandle> m_createdEntity;
    std::optional<Error> m_error;
};

/// @brief FIFO適用した全Structural Command結果を順序通り所有する
class StructuralCommandReport final
{
  public:
    /// @brief 空の順序付きReportを構築する
    StructuralCommandReport() noexcept = default;
    /// @brief Move-only Command結果の複製を禁止する
    StructuralCommandReport(const StructuralCommandReport &) = delete;
    /// @brief Move-only Command結果の複製代入を禁止する
    StructuralCommandReport &operator=(const StructuralCommandReport &) = delete;
    /// @brief 順序付きCommand結果の所有権を移動する
    StructuralCommandReport(StructuralCommandReport &&) noexcept = default;
    /// @brief 順序付きCommand結果の所有権を移動代入する
    StructuralCommandReport &operator=(StructuralCommandReport &&) noexcept = default;
    /// @brief Reportが所有するCommand結果を破棄する
    ~StructuralCommandReport() = default;

    /// @brief Command登録順を維持した結果Viewを返す
    [[nodiscard]] std::span<const StructuralCommandResult> results() const noexcept
    {
        return m_results;
    }

  private:
    friend class World;

    std::vector<StructuralCommandResult> m_results;
};

/// @brief Query中のStructural MutationをSafe PointまでFIFO遅延するBuffer
///
/// Bufferは結び付けたWorldより先に同じOwner Threadで破棄する
class StructuralCommandBuffer final
{
  public:
    /// @brief 指定World専用の空Command BufferをOwner Thread上で構築する
    explicit StructuralCommandBuffer(World &a_world) noexcept;
    /// @brief Command所有権の複製を禁止する
    StructuralCommandBuffer(const StructuralCommandBuffer &) = delete;
    /// @brief Command所有権の複製代入を禁止する
    StructuralCommandBuffer &operator=(const StructuralCommandBuffer &) = delete;
    /// @brief Buffer AddressをCapabilityへ保持するためMove構築を禁止する
    StructuralCommandBuffer(StructuralCommandBuffer &&) = delete;
    /// @brief Buffer AddressをCapabilityへ保持するためMove代入を禁止する
    StructuralCommandBuffer &operator=(StructuralCommandBuffer &&) = delete;
    /// @brief 未Flush Commandを再入防止区間で破棄する
    ~StructuralCommandBuffer() noexcept;

    /// @brief Deferred Entity作成を記録して後続Command用Pending IDを返す
    [[nodiscard]] Result<PendingEntityId> create_entity() noexcept;
    /// @brief 既存Entity破棄を登録順の末尾へ記録する
    [[nodiscard]] Result<void> destroy_entity(EntityHandle a_entity) noexcept;
    /// @brief Pending Entity破棄を登録順の末尾へ記録する
    [[nodiscard]] Result<void> destroy_entity(PendingEntityId a_entity) noexcept;

    /// @brief 既存EntityへのComponent追加を完全構築済みValueとして記録する
    template <typename T, typename... Args>
    [[nodiscard]] Result<void> add_component(
        ComponentType<T> a_type, EntityHandle a_entity,
        Args &&...a_arguments) noexcept
    {
        return record_add(a_type, Target(a_entity),
                          std::forward<Args>(a_arguments)...);
    }

    /// @brief Pending EntityへのComponent追加を完全構築済みValueとして記録する
    template <typename T, typename... Args>
    [[nodiscard]] Result<void> add_component(
        ComponentType<T> a_type, PendingEntityId a_entity,
        Args &&...a_arguments) noexcept
    {
        assert_recordable();

        if (!validate_pending(a_entity))
        {
            return invalid_pending_result();
        }

        return record_add(a_type, Target(a_entity),
                          std::forward<Args>(a_arguments)...);
    }

    /// @brief 既存EntityからのComponent削除を登録順の末尾へ記録する
    template <typename T>
    [[nodiscard]] Result<void> remove_component(
        ComponentType<T> a_type, EntityHandle a_entity) noexcept
    {
        return record_remove(a_type, Target(a_entity));
    }

    /// @brief Pending EntityからのComponent削除を登録順の末尾へ記録する
    template <typename T>
    [[nodiscard]] Result<void> remove_component(
        ComponentType<T> a_type, PendingEntityId a_entity) noexcept
    {
        assert_recordable();

        if (!validate_pending(a_entity))
        {
            return invalid_pending_result();
        }

        return record_remove(a_type, Target(a_entity));
    }

    /// @brief 現在未FlushのCommand数を返す
    [[nodiscard]] std::size_t size() const noexcept
    {
        assert_recordable();
        return m_commands.size();
    }

  private:
    friend class World;

    using Target = std::variant<EntityHandle, PendingEntityId>;

    class Command
    {
      public:
        /// @brief 型消去したCommandを基底Pointerから正しく破棄する
        virtual ~Command() = default;
        /// @brief CommandをWorldへ適用し一件分の順序付き結果を返す
        [[nodiscard]] virtual StructuralCommandResult apply(
            StructuralCommandBuffer &a_buffer, World &a_world,
            std::vector<std::optional<EntityHandle>> &a_pendingEntities) noexcept = 0;
    };

    class CreateCommand final : public Command
    {
      public:
        /// @brief Deferred Createと解決先Pending IDを結び付ける
        explicit CreateCommand(PendingEntityId a_pendingId) noexcept
            : m_pendingId(a_pendingId)
        {
        }

        /// @brief Entityを生成してPending解決表とReportへ公開する
        [[nodiscard]] StructuralCommandResult apply(
            StructuralCommandBuffer &a_buffer, World &a_world,
            std::vector<std::optional<EntityHandle>> &a_pendingEntities) noexcept override;

      private:
        PendingEntityId m_pendingId;
    };

    class DestroyCommand final : public Command
    {
      public:
        /// @brief Destroy対象を既存またはPending Entityとして保持する
        explicit DestroyCommand(Target a_target) noexcept
            : m_target(std::move(a_target))
        {
        }

        /// @brief 対象を解決してEntity破棄結果を返す
        [[nodiscard]] StructuralCommandResult apply(
            StructuralCommandBuffer &a_buffer, World &a_world,
            std::vector<std::optional<EntityHandle>> &a_pendingEntities) noexcept override;

      private:
        Target m_target;
    };

    template <typename T> class AddCommand final : public Command
    {
      public:
        /// @brief 型付きCapability、対象、完全構築済みComponentを保持する
        AddCommand(ComponentType<T> a_type, Target a_target,
                   T &&a_component) noexcept
            : m_type(a_type), m_target(std::move(a_target)),
              m_component(std::move(a_component))
        {
        }

        /// @brief 対象を解決してComponent追加結果を返す
        [[nodiscard]] StructuralCommandResult apply(
            StructuralCommandBuffer &a_buffer, World &a_world,
            std::vector<std::optional<EntityHandle>> &a_pendingEntities) noexcept override
        {
            auto entity = a_buffer.resolve_target(m_target, a_pendingEntities);

            if (!entity)
            {
                return StructuralCommandResult::failure(
                    StructuralCommandKind::AddComponent,
                    std::move(*entity.try_error()));
            }

            auto result = a_world.add_component(
                m_type, *entity.try_value(), std::move(m_component));

            if (!result)
            {
                return StructuralCommandResult::failure(
                    StructuralCommandKind::AddComponent,
                    std::move(*result.try_error()));
            }

            return StructuralCommandResult::success(
                StructuralCommandKind::AddComponent);
        }

      private:
        ComponentType<T> m_type;
        Target m_target;
        T m_component;
    };

    template <typename T> class RemoveCommand final : public Command
    {
      public:
        /// @brief 型付きCapabilityと既存またはPending対象を保持する
        RemoveCommand(ComponentType<T> a_type, Target a_target) noexcept
            : m_type(a_type), m_target(std::move(a_target))
        {
        }

        /// @brief 対象を解決してComponent削除結果を返す
        [[nodiscard]] StructuralCommandResult apply(
            StructuralCommandBuffer &a_buffer, World &a_world,
            std::vector<std::optional<EntityHandle>> &a_pendingEntities) noexcept override
        {
            auto entity = a_buffer.resolve_target(m_target, a_pendingEntities);

            if (!entity)
            {
                return StructuralCommandResult::failure(
                    StructuralCommandKind::RemoveComponent,
                    std::move(*entity.try_error()));
            }

            auto result = a_world.remove_component(m_type, *entity.try_value());

            if (!result)
            {
                return StructuralCommandResult::failure(
                    StructuralCommandKind::RemoveComponent,
                    std::move(*result.try_error()));
            }

            return StructuralCommandResult::success(
                StructuralCommandKind::RemoveComponent);
        }

      private:
        ComponentType<T> m_type;
        Target m_target;
    };

    /// @brief 完全構築済みComponentのAdd Commandを登録する
    template <typename T, typename... Args>
    [[nodiscard]] Result<void> record_add(
        ComponentType<T> a_type, Target a_target,
        Args &&...a_arguments) noexcept
    {
        static_assert(std::is_nothrow_constructible_v<T, Args...>);
        assert_recordable();
        T component(std::forward<Args>(a_arguments)...);

        try
        {
            m_commands.push_back(std::make_unique<AddCommand<T>>(
                a_type, std::move(a_target), std::move(component)));
            return Result<void>::success();
        }
        catch (const std::bad_alloc &)
        {
            terminate_allocation();
        }
        catch (...)
        {
            terminate_exception();
        }
    }

    /// @brief Component Remove Commandを型付きCapabilityと対象から登録する
    template <typename T>
    [[nodiscard]] Result<void> record_remove(
        ComponentType<T> a_type, Target a_target) noexcept
    {
        assert_recordable();

        try
        {
            m_commands.push_back(std::make_unique<RemoveCommand<T>>(
                a_type, std::move(a_target)));
            return Result<void>::success();
        }
        catch (const std::bad_alloc &)
        {
            terminate_allocation();
        }
        catch (...)
        {
            terminate_exception();
        }
    }

    /// @brief BufferとFlush世代が一致するPending IDか検証する
    [[nodiscard]] bool validate_pending(PendingEntityId a_pendingId) const noexcept;
    /// @brief Invalid Command Buffer Errorを持つ記録失敗を返す
    [[nodiscard]] Result<void> invalid_pending_result() const noexcept;
    /// @brief 既存またはPending対象を現在のEntity Handleへ解決する
    [[nodiscard]] Result<EntityHandle> resolve_target(
        const Target &a_target,
        const std::vector<std::optional<EntityHandle>> &a_pendingEntities) const noexcept;
    /// @brief Record APIがOwner Threadかつ非Flush中であることを検証する
    void assert_recordable() const noexcept;
    /// @brief 未Flush CommandをWorldの再入防止区間で破棄する
    void discard_commands() noexcept;
    /// @brief Command Buffer内Allocation失敗をFatalへ変換する
    [[noreturn]] void terminate_allocation() const noexcept;
    /// @brief Command Buffer境界の予期しない例外をFatalへ変換する
    [[noreturn]] void terminate_exception() const noexcept;

    World *m_world;
    const AssertContext *m_assertContext;
    std::thread::id m_ownerThread;
    std::vector<std::unique_ptr<Command>> m_commands;
    std::uint64_t m_generation = 1U;
    std::uint64_t m_nextPendingId = 1U;
    bool m_isFlushing = false;
};
} // namespace cue::game_core
