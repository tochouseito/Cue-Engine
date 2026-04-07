#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>

namespace Cue::ECS {
class ObjectInfoSystem final : public ECSManager::System<ObjectInfoComponent> {
public:
  ObjectInfoSystem()
      : ECSManager::System<ObjectInfoComponent>(
            [&](Entity e, ObjectInfoComponent &objectInfo) {
              update_component(e, objectInfo);
            },
            [&](Entity e, ObjectInfoComponent &objectInfo) {
              initialize_component(e, objectInfo);
            },
            [&](Entity e, ObjectInfoComponent &objectInfo) {
              finalize_component(e, objectInfo);
            }) {}

private:
  void update_component(Entity a_entity, ObjectInfoComponent &a_objectInfo) {
    a_entity;
    a_objectInfo;
  }

  void initialize_component(Entity a_entity,
                            ObjectInfoComponent &a_objectInfo) {
    a_entity;
    a_objectInfo;
  }

  void finalize_component(Entity a_entity, ObjectInfoComponent &a_objectInfo) {
    a_entity;
    a_objectInfo;
  }
};
} // namespace Cue::ECS
