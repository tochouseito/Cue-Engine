#pragma once

/// **********************************************************************
/// Command 定義
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === GameCore includes ===
#include <GameCore/Components.h>
#include <GameCore/GameCoreTypes.h>
#include <GameCore/ObjectSnapshot.h>

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace Cue
{
    enum class ComponentKind : uint8_t
    {
        Transform,
        Camera,
        MeshFilter,
        StaticMeshRenderer,
    };

    class IGameCommandContext : public virtual Core::CQRS::ICommandContext
    {
    public:
        ~IGameCommandContext() override = default;

        virtual Result create_object(std::string_view a_name, GameCore::EntityId& a_outObjectId) = 0;
        virtual Result destroy_object(GameCore::EntityId a_objectId) = 0;
        virtual Result add_component(GameCore::EntityId a_objectId, ComponentKind a_kind) = 0;
        virtual Result remove_component(GameCore::EntityId a_objectId, ComponentKind a_kind) = 0;

        /// @brief 現在の描画 Camera Entity を取得する
        virtual Result get_render_camera(GameCore::EntityId& a_outObjectId) = 0;

        /// @brief 描画に使用する Camera Entity を設定する
        virtual Result set_render_camera(GameCore::EntityId a_objectId) = 0;
        virtual Result capture_object_snapshot(
            GameCore::EntityId a_objectId,
            GameCore::ObjectSnapshot& a_outSnapshot) = 0;
        virtual Result restore_object_snapshot(
            const GameCore::ObjectSnapshot& a_snapshot,
            GameCore::EntityId& a_outObjectId) = 0;
        virtual Result get_object_name(GameCore::EntityId a_objectId, std::string& a_outName) = 0;
        virtual Result rename_object(GameCore::EntityId a_objectId, std::string_view a_name) = 0;

        /// @brief GameObject の Tag を取得する
        virtual Result get_object_tag(GameCore::EntityId a_objectId, std::string& a_outTag) = 0;

        /// @brief GameObject の Tag を変更する
        virtual Result set_object_tag(GameCore::EntityId a_objectId, std::string_view a_tag) = 0;

        /// @brief GameObject の ActiveSelf 状態を取得する
        virtual Result get_object_active(GameCore::EntityId a_objectId, bool& a_outIsActive) = 0;

        /// @brief GameObject の ActiveSelf 状態を変更する
        virtual Result set_object_active(GameCore::EntityId a_objectId, bool a_isActive) = 0;

        /// @brief GameObject の Persistent 状態を取得する
        virtual Result get_object_persistent(GameCore::EntityId a_objectId, bool& a_outIsPersistent) = 0;

        /// @brief GameObject の Persistent 状態を変更する
        virtual Result set_object_persistent(GameCore::EntityId a_objectId, bool a_isPersistent) = 0;
        virtual Result get_parent(GameCore::EntityId a_objectId, GameCore::EntityId& a_outParentId) = 0;
        virtual Result set_parent(
            GameCore::EntityId a_objectId,
            GameCore::EntityId a_parentId,
            bool a_keepsWorldTransform) = 0;

        /// @brief Editor からの Component 編集を GameWorld の安全な更新フェーズへ遅延する。
        virtual Result get_transform_component(
            GameCore::EntityId a_objectId,
            ECS::TransformComponent& a_outComponent) = 0;
        virtual Result set_transform_component(
            GameCore::EntityId a_objectId,
            const ECS::TransformComponent& a_component) = 0;

        /// @brief CameraComponent を描画入力へ変換する前の GameCore 状態として編集する。
        virtual Result get_camera_component(
            GameCore::EntityId a_objectId,
            ECS::CameraComponent& a_outComponent) = 0;
        virtual Result set_camera_component(
            GameCore::EntityId a_objectId,
            const ECS::CameraComponent& a_component) = 0;

        /// @brief MeshFilterComponent の mesh 参照を描画抽出前の GameCore 状態として編集する。
        virtual Result get_mesh_filter_component(
            GameCore::EntityId a_objectId,
            ECS::MeshFilterComponent& a_outComponent) = 0;
        virtual Result set_mesh_filter_component(
            GameCore::EntityId a_objectId,
            const ECS::MeshFilterComponent& a_component) = 0;

        /// @brief Renderable 収集前の描画設定を GameObject 単位で編集する。
        virtual Result get_static_mesh_renderer_component(
            GameCore::EntityId a_objectId,
            ECS::StaticMeshRendererComponent& a_outComponent) = 0;
        virtual Result set_static_mesh_renderer_component(
            GameCore::EntityId a_objectId,
            const ECS::StaticMeshRendererComponent& a_component) = 0;
    };

    class CreateObjectCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        explicit CreateObjectCommand(std::string a_name)
            : m_name(std::move(a_name))
        {
        }

        Result execute(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IGameCommandContext* gameCommandContext =
                dynamic_cast<IGameCommandContext*>(&a_commandContext);
            if (gameCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support object creation.");
            }

            Result result = gameCommandContext->create_object(m_name, m_objectId);
            if (result)
            {
                m_hasObject = true;
            }

            return result;
        }

        Result undo(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IGameCommandContext* gameCommandContext =
                dynamic_cast<IGameCommandContext*>(&a_commandContext);
            if (gameCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support object creation undo.");
            }

            if (!m_hasObject)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Create object command has not been executed.");
            }

            Result result = gameCommandContext->destroy_object(m_objectId);
            if (result)
            {
                m_objectId = GameCore::k_invalidEntityId;
                m_hasObject = false;
            }

            return result;
        }

    private:
        std::string m_name{};
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        bool m_hasObject = false;
    };

    class AddComponentCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        AddComponentCommand(GameCore::EntityId a_objectId, ComponentKind a_kind) noexcept
            : m_objectId(a_objectId)
            , m_kind(a_kind)
        {
        }

        Result execute(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IGameCommandContext* gameCommandContext =
                dynamic_cast<IGameCommandContext*>(&a_commandContext);
            if (gameCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support component addition.");
            }

            return gameCommandContext->add_component(m_objectId, m_kind);
        }

        Result undo(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IGameCommandContext* gameCommandContext =
                dynamic_cast<IGameCommandContext*>(&a_commandContext);
            if (gameCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support component addition undo.");
            }

            return gameCommandContext->remove_component(m_objectId, m_kind);
        }

    private:
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        ComponentKind m_kind = ComponentKind::Transform;
    };

    class RemoveComponentCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        RemoveComponentCommand(GameCore::EntityId a_objectId, ComponentKind a_kind) noexcept
            : m_objectId(a_objectId)
            , m_kind(a_kind)
        {
        }

        Result execute(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IGameCommandContext* gameCommandContext =
                dynamic_cast<IGameCommandContext*>(&a_commandContext);
            if (gameCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support component removal.");
            }

            if (!m_hasSnapshot)
            {
                Result result = capture_component(*gameCommandContext);
                if (!result)
                {
                    return result;
                }

                m_hasSnapshot = true;
            }

            return gameCommandContext->remove_component(m_objectId, m_kind);
        }

        Result undo(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IGameCommandContext* gameCommandContext =
                dynamic_cast<IGameCommandContext*>(&a_commandContext);
            if (gameCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support component removal undo.");
            }
            if (!m_hasSnapshot)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Remove component command has not been executed.");
            }

            Result result = gameCommandContext->add_component(m_objectId, m_kind);
            if (!result)
            {
                return result;
            }

            result = restore_component(*gameCommandContext);
            if (!result)
            {
                // Component が既定値のまま残ると次回の Redo/Undo で状態が崩れるため復元失敗時は取り消す。
                const Result rollbackResult =
                    gameCommandContext->remove_component(m_objectId, m_kind);
                if (!rollbackResult)
                {
                    return rollbackResult;
                }
                return result;
            }

            if (m_kind == ComponentKind::Camera && m_wasRenderCamera)
            {
                return gameCommandContext->set_render_camera(m_objectId);
            }

            return Result::ok();
        }

    private:
        Result capture_component(IGameCommandContext& a_commandContext)
        {
            switch (m_kind)
            {
            case ComponentKind::Camera: {
                Result result = a_commandContext.get_camera_component(
                    m_objectId, m_cameraComponent);
                if (!result)
                {
                    return result;
                }

                GameCore::EntityId renderCameraId = GameCore::k_invalidEntityId;
                result = a_commandContext.get_render_camera(renderCameraId);
                if (result)
                {
                    m_wasRenderCamera = renderCameraId == m_objectId;
                }
                return result;
            }
            case ComponentKind::MeshFilter:
                return a_commandContext.get_mesh_filter_component(
                    m_objectId, m_meshFilterComponent);
            case ComponentKind::StaticMeshRenderer:
                return a_commandContext.get_static_mesh_renderer_component(
                    m_objectId, m_staticMeshRendererComponent);
            case ComponentKind::Transform:
                return Result::fail(
                    Code::InvalidState,
                    Severity::Warning,
                    "TransformComponent is required and cannot be removed.");
            default:
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Unknown component kind.");
            }
        }

        Result restore_component(IGameCommandContext& a_commandContext)
        {
            switch (m_kind)
            {
            case ComponentKind::Camera:
                return a_commandContext.set_camera_component(m_objectId, m_cameraComponent);
            case ComponentKind::MeshFilter:
                return a_commandContext.set_mesh_filter_component(
                    m_objectId, m_meshFilterComponent);
            case ComponentKind::StaticMeshRenderer:
                return a_commandContext.set_static_mesh_renderer_component(
                    m_objectId, m_staticMeshRendererComponent);
            case ComponentKind::Transform:
                return Result::fail(
                    Code::InvalidState,
                    Severity::Warning,
                    "TransformComponent is required and cannot be restored.");
            default:
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Unknown component kind.");
            }
        }

        ECS::CameraComponent m_cameraComponent{};
        ECS::MeshFilterComponent m_meshFilterComponent{};
        ECS::StaticMeshRendererComponent m_staticMeshRendererComponent{};
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        ComponentKind m_kind = ComponentKind::Transform;
        bool m_hasSnapshot = false;
        bool m_wasRenderCamera = false;
    };

    class RenameObjectCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        RenameObjectCommand(GameCore::EntityId a_objectId, std::string a_newName)
            : m_objectId(a_objectId), m_newName(std::move(a_newName))
        {
        }

        Result execute(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IGameCommandContext* gameCommandContext =
                dynamic_cast<IGameCommandContext*>(&a_commandContext);
            if (gameCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support object rename.");
            }

            if (!m_hasCapturedOldName)
            {
                Result captureResult =
                    gameCommandContext->get_object_name(m_objectId, m_oldName);
                if (!captureResult)
                {
                    return captureResult;
                }

                m_hasCapturedOldName = true;
            }

            return gameCommandContext->rename_object(m_objectId, m_newName);
        }

        Result undo(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IGameCommandContext* gameCommandContext =
                dynamic_cast<IGameCommandContext*>(&a_commandContext);
            if (gameCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support object rename undo.");
            }

            if (!m_hasCapturedOldName)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Rename command old name was not captured.");
            }

            return gameCommandContext->rename_object(m_objectId, m_oldName);
        }

    private:
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        std::string m_oldName{};
        std::string m_newName{};
        bool m_hasCapturedOldName = false;
    };

    class DeleteObjectCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        explicit DeleteObjectCommand(GameCore::EntityId a_objectId) noexcept
            : m_objectId(a_objectId)
        {
        }

        Result execute(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IGameCommandContext* gameCommandContext =
                dynamic_cast<IGameCommandContext*>(&a_commandContext);
            if (gameCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support object deletion.");
            }

            if (!m_hasSnapshot)
            {
                Result captureResult =
                    gameCommandContext->capture_object_snapshot(m_objectId, m_snapshot);
                if (!captureResult)
                {
                    return captureResult;
                }

                m_hasSnapshot = true;
            }

            return gameCommandContext->destroy_object(m_objectId);
        }

        Result undo(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IGameCommandContext* gameCommandContext =
                dynamic_cast<IGameCommandContext*>(&a_commandContext);
            if (gameCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support object deletion undo.");
            }
            if (!m_hasSnapshot)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Delete object command has not been executed.");
            }

            GameCore::EntityId restoredObjectId = GameCore::k_invalidEntityId;
            const Result result =
                gameCommandContext->restore_object_snapshot(m_snapshot, restoredObjectId);
            if (!result)
            {
                return result;
            }
            if (restoredObjectId != m_objectId)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Delete object undo restored an unexpected entity ID.");
            }

            return Result::ok();
        }

    private:
        GameCore::ObjectSnapshot m_snapshot{};
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        bool m_hasSnapshot = false;
    };

    class SetParentCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        SetParentCommand(
            GameCore::EntityId a_objectId,
            GameCore::EntityId a_newParentId,
            bool a_keepsWorldTransform = true) noexcept
            : m_objectId(a_objectId)
            , m_newParentId(a_newParentId)
            , m_keepsWorldTransform(a_keepsWorldTransform)
        {
        }

        Result execute(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IGameCommandContext* gameCommandContext =
                dynamic_cast<IGameCommandContext*>(&a_commandContext);
            if (gameCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support parent updates.");
            }

            if (!m_hasOldParent)
            {
                Result captureResult =
                    gameCommandContext->get_parent(m_objectId, m_oldParentId);
                if (!captureResult)
                {
                    return captureResult;
                }

                m_hasOldParent = true;
            }

            return gameCommandContext->set_parent(
                m_objectId, m_newParentId, m_keepsWorldTransform);
        }

        Result undo(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IGameCommandContext* gameCommandContext =
                dynamic_cast<IGameCommandContext*>(&a_commandContext);
            if (gameCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support parent update undo.");
            }

            if (!m_hasOldParent)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Set parent command has not been executed.");
            }

            return gameCommandContext->set_parent(
                m_objectId, m_oldParentId, m_keepsWorldTransform);
        }

    private:
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        GameCore::EntityId m_oldParentId = GameCore::k_invalidEntityId;
        GameCore::EntityId m_newParentId = GameCore::k_invalidEntityId;
        bool m_keepsWorldTransform = true;
        bool m_hasOldParent = false;
    };

    /// @brief Inspector で編集した Tag を undo 可能な GameWorld 更新として扱う。
    [[nodiscard]] std::unique_ptr<Core::CQRS::ICommand> make_set_object_tag_command(
        GameCore::EntityId a_objectId,
        std::string a_tag);

    /// @brief Inspector で編集した ActiveSelf を undo 可能な GameWorld 更新として扱う。
    [[nodiscard]] std::unique_ptr<Core::CQRS::ICommand> make_set_object_active_command(
        GameCore::EntityId a_objectId,
        bool a_isActive);

    /// @brief Inspector で編集した Persistent を undo 可能な GameWorld 更新として扱う。
    [[nodiscard]] std::unique_ptr<Core::CQRS::ICommand> make_set_object_persistent_command(
        GameCore::EntityId a_objectId,
        bool a_isPersistent);

    /// @brief Inspector で編集した local Transform を undo 可能な GameWorld 更新として扱う。
    [[nodiscard]] std::unique_ptr<Core::CQRS::ICommand> make_set_transform_component_command(
        GameCore::EntityId a_objectId,
        const ECS::TransformComponent& a_component);

    /// @brief Inspector で編集した Camera 設定を undo 可能な GameWorld 更新として扱う。
    [[nodiscard]] std::unique_ptr<Core::CQRS::ICommand> make_set_camera_component_command(
        GameCore::EntityId a_objectId,
        const ECS::CameraComponent& a_component);

    /// @brief Inspector で選択した MeshPool 上の mesh 参照を undo 可能な GameWorld 更新として扱う。
    [[nodiscard]] std::unique_ptr<Core::CQRS::ICommand> make_set_mesh_filter_component_command(
        GameCore::EntityId a_objectId,
        const ECS::MeshFilterComponent& a_component);

    /// @brief Inspector で編集した StaticMesh 描画設定を undo 可能な GameWorld 更新として扱う。
    [[nodiscard]] std::unique_ptr<Core::CQRS::ICommand> make_set_static_mesh_renderer_component_command(
        GameCore::EntityId a_objectId,
        const ECS::StaticMeshRendererComponent& a_component);
}
