#pragma once

// === Base includes ===
#include <CueAssert.h>

// === Engine includes ===
#include <Commands.h>
#include <GameCore/GameWorld.h>

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class Hierarchy final
    {
    public:
        struct ObjectEntry final
        {
            std::string name{};
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
        };

        Hierarchy(Core::CQRS::Bridge* bridge, GameCore::GameWorld* gameWorld,
            GameCore::EntityId* a_selectedEntityId)
            : editorBridge(bridge)
            , m_gameWorld(gameWorld)
            , m_selectedEntityId(a_selectedEntityId)
        {
        }
        ~Hierarchy() = default;

        void update()
        {
            ImGui::Begin("ヒエラルキー");

            if (m_gameWorld == nullptr)
            {
                ImGui::TextUnformatted("GameWorld が初期化されていません。");
                ImGui::End();
                return;
            }

            if (!refresh_objects())
            {
                ImGui::TextUnformatted("GameObject の列挙に失敗しました。");
                ImGui::End();
                return;
            }

            for (const ObjectEntry& object : m_objects)
            {
                draw_object_row(object);
            }

            ImGui::End();
        }

    private:
        [[nodiscard]] bool refresh_objects()
        {
            m_objects.clear();

            size_t objectCount = 0;
            Result countResult = m_gameWorld->object_count(objectCount);
            if (countResult)
            {
                m_objects.reserve(objectCount);
            }

            const Result enumerateResult =
                m_gameWorld->for_each_object(
                    [this](GameCore::EntityId a_entityId,
                        GameCore::SceneId,
                        GameCore::GameObject& a_object)
                    {
                        std::string objectName{};
                        Result nameResult = a_object.name(objectName);
                        if (!nameResult || objectName.empty())
                        {
                            objectName = "GameObject";
                        }

                        m_objects.push_back(
                            { std::move(objectName), a_entityId });
                    });

            if (!enumerateResult)
            {
                return false;
            }

            std::sort(m_objects.begin(), m_objects.end(),
                [](const ObjectEntry& a_left, const ObjectEntry& a_right)
                {
                    if (a_left.name == a_right.name)
                    {
                        return a_left.entityId < a_right.entityId;
                    }

                    return a_left.name < a_right.name;
                });

            const bool hasSelectedObject =
                std::any_of(m_objects.begin(), m_objects.end(),
                    [this](const ObjectEntry& a_object)
                    {
                        return a_object.entityId == selected_entity_id();
                    });
            if (!hasSelectedObject)
            {
                set_selected_entity_id(GameCore::k_invalidEntityId);
            }

            const bool hasRenamingObject =
                std::any_of(m_objects.begin(), m_objects.end(),
                    [this](const ObjectEntry& a_object)
                    {
                        return a_object.entityId == m_renamingEntityId;
                    });
            if (!hasRenamingObject)
            {
                cancel_rename();
            }

            return true;
        }

        void draw_object_row(const ObjectEntry& a_object)
        {
            ImGui::PushID(static_cast<int>(a_object.entityId));

            if (m_renamingEntityId == a_object.entityId)
            {
                if (m_focusRenameInput)
                {
                    ImGui::SetKeyboardFocusHere();
                    m_focusRenameInput = false;
                }

                const bool submitted = ImGui::InputText("##Rename",
                    m_renameBuffer.data(), m_renameBuffer.size(),
                    ImGuiInputTextFlags_AutoSelectAll |
                    ImGuiInputTextFlags_EnterReturnsTrue);
                const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
                const bool isActive = ImGui::IsItemActive();
                const bool isEscapePressed =
                    isActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false);

                if (submitted || deactivated)
                {
                    submit_rename_command(a_object.entityId);
                }
                else if (isEscapePressed)
                {
                    cancel_rename();
                }
            }
            else
            {
                const bool isSelected =
                    selected_entity_id() == a_object.entityId;
                if (ImGui::Selectable(a_object.name.c_str(), isSelected))
                {
                    set_selected_entity_id(a_object.entityId);
                }

                if (ImGui::IsItemHovered() &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    begin_rename(a_object);
                }
            }

            if (ImGui::BeginPopupContextItem("HierarchyContextMenu"))
            {
                set_selected_entity_id(a_object.entityId);

                if (ImGui::MenuItem("名前変更"))
                {
                    begin_rename(a_object);
                }

                if (ImGui::MenuItem("削除"))
                {
                    submit_delete_command(a_object.entityId);
                }

                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        void begin_rename(const ObjectEntry& a_object)
        {
            set_selected_entity_id(a_object.entityId);
            m_renamingEntityId = a_object.entityId;
            m_focusRenameInput = true;
            std::fill(m_renameBuffer.begin(), m_renameBuffer.end(), '\0');
            const size_t copyLength =
                (std::min)(a_object.name.size(), m_renameBuffer.size() - 1);
            if (copyLength > 0)
            {
                std::memcpy(m_renameBuffer.data(), a_object.name.data(), copyLength);
            }
        }

        void cancel_rename()
        {
            m_renamingEntityId = GameCore::k_invalidEntityId;
            m_focusRenameInput = false;
            std::fill(m_renameBuffer.begin(), m_renameBuffer.end(), '\0');
        }

        void submit_rename_command(GameCore::EntityId a_entityId)
        {
            std::string newName = m_renameBuffer.data();
            const auto objectIt = std::find_if(m_objects.begin(), m_objects.end(),
                [a_entityId](const ObjectEntry& a_object)
                {
                    return a_object.entityId == a_entityId;
                });

            if (objectIt != m_objects.end() && objectIt->name != newName)
            {
                Result result = editorBridge->submit_command(
                    std::make_unique<Cue::RenameObjectCommand>(a_entityId,
                        std::move(newName)));
                if (!result)
                {
                    CUE_ASSERTF(false,
                        "Failed to submit rename object command: %s (code: %s, severity: %s) at %s:%u in function %s",
                        result.message.data(), Cue::to_string(result.code),
                        Cue::to_string(result.severity), result.file,
                        result.line, result.function);
                }
            }

            cancel_rename();
        }

        void submit_delete_command(GameCore::EntityId a_entityId)
        {
            Result result = editorBridge->submit_command(
                std::make_unique<Cue::DeleteObjectCommand>(a_entityId));
            if (!result)
            {
                CUE_ASSERTF(false,
                    "Failed to submit delete object command: %s (code: %s, severity: %s) at %s:%u in function %s",
                    result.message.data(), Cue::to_string(result.code),
                    Cue::to_string(result.severity), result.file,
                    result.line, result.function);
            }

            if (selected_entity_id() == a_entityId)
            {
                set_selected_entity_id(GameCore::k_invalidEntityId);
            }
            if (m_renamingEntityId == a_entityId)
            {
                cancel_rename();
            }
        }

        [[nodiscard]] GameCore::EntityId selected_entity_id() const noexcept
        {
            return m_selectedEntityId != nullptr
                ? *m_selectedEntityId
                : GameCore::k_invalidEntityId;
        }

        void set_selected_entity_id(GameCore::EntityId a_entityId) noexcept
        {
            if (m_selectedEntityId != nullptr)
            {
                *m_selectedEntityId = a_entityId;
            }
        }

        Core::CQRS::Bridge* editorBridge = nullptr;
        GameCore::GameWorld* m_gameWorld = nullptr;
        std::vector<ObjectEntry> m_objects{};
        GameCore::EntityId* m_selectedEntityId = nullptr;
        GameCore::EntityId m_renamingEntityId = GameCore::k_invalidEntityId;
        std::array<char, 256> m_renameBuffer{};
        bool m_focusRenameInput = false;
    };
}
