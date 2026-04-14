#pragma once

// === Engine includes ===
#include <Commands.h>
#include <GameCore/GameWorld.h>

// === C++ includes ===
#include <algorithm>
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

        Hierarchy(Core::CQRS::Bridge* bridge, GameCore::GameWorld* gameWorld)
            : editorBridge(bridge), m_gameWorld(gameWorld)
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

            std::vector<ObjectEntry> objects{};
            size_t objectCount = 0;
            Result countResult = m_gameWorld->object_count(objectCount);
            if (countResult)
            {
                objects.reserve(objectCount);
            }

            const Result enumerateResult =
                m_gameWorld->for_each_object(
                    [&objects](GameCore::EntityId a_entityId,
                        GameCore::SceneId,
                        GameCore::GameObject& a_object)
                    {
                        std::string objectName{};
                        Result nameResult = a_object.name(objectName);
                        if (!nameResult || objectName.empty())
                        {
                            objectName = "GameObject";
                        }

                        objects.push_back(
                            { std::move(objectName), a_entityId });
                    });

            if (!enumerateResult)
            {
                ImGui::TextUnformatted("GameObject の列挙に失敗しました。");
                ImGui::End();
                return;
            }

            std::sort(objects.begin(), objects.end(),
                [](const ObjectEntry& a_left, const ObjectEntry& a_right)
                {
                    if (a_left.name == a_right.name)
                    {
                        return a_left.entityId < a_right.entityId;
                    }

                    return a_left.name < a_right.name;
                });

            for (const ObjectEntry& object : objects)
            {
                ImGui::BulletText("%s", object.name.c_str());
            }

            ImGui::End();
        }
    private:
        Core::CQRS::Bridge* editorBridge = nullptr;
        GameCore::GameWorld* m_gameWorld = nullptr;
    };
}
