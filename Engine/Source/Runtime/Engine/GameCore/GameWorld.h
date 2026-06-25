// GameWorld の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <CueMath.h>
#include <Result.h>

// === Engine includes ===
#include "Components.h"
#include "DebugDraw.h"
#include "GameObject.h"
#include "Navigation/Navigation.h"
#include "SceneAsset.h"
#include "SceneInstance.h"
#include "SceneSerializer.h"
#include "Systems/AudioSystem.h"
#include "Systems/CharacterControllerSystem.h"
#include "Systems/DemoEnemySystem.h"
#include "Systems/FirstPersonCameraControllerSystem.h"
#include "Systems/PhysicsBodySystem.h"
#include "Systems/PlayerControlSystem.h"
#include "Systems/TriggerVolumeSystem.h"
#include "Systems/UiLayoutSystem.h"
#include "Systems/UiWidgetSystem.h"
#include <AnimationSystem/AnimationSystem.h>
#include <Asset/AssetManager.h>
#include <DrawSystem/DrawFrameState.h>
#include <DrawSystem/DrawResources.h>
#include <DrawSystem/DrawScene.h>
#include <DrawSystem/StaticMeshPoolTypes.h>
#include <DrawSystem/Systems/CameraSystem.h>
#include <DrawSystem/Systems/RenderableObjectSystem.h>
#include <DrawSystem/Systems/SkinnedRenderableObjectSystem.h>
#include <DrawSystem/Systems/SpriteSystem.h>
#include <DrawSystem/Systems/TextSystem.h>
#include <EffectSystem/EffectPrimitiveFrameState.h>
#include <EffectSystem/EffectPrimitiveResources.h>
#include <EffectSystem/EffectPrimitiveScene.h>
#include <EffectSystem/Systems/EffectEmitterSystem.h>
#include <LightingSystem/LightFrameState.h>
#include <LightingSystem/LightResources.h>
#include <LightingSystem/LightScene.h>
#include <LightingSystem/Systems/LightSystem.h>
#include <ParticleSystem/ParticleFrameState.h>
#include <ParticleSystem/ParticleRangeAllocator.h>
#include <ParticleSystem/ParticleResources.h>
#include <ParticleSystem/ParticleScene.h>
#include <ParticleSystem/Systems/ParticleEmitterSystem.h>
#include <ShadowSystem/ShadowFrameState.h>
#include <ShadowSystem/ShadowResources.h>
#include <ShadowSystem/ShadowScene.h>
#include <ShadowSystem/Systems/ShadowSystem.h>

// === PAL includes ===
#include <Input/InputManager.h>

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Cue::GameCore
{
/// @brief Scene、Entity、各 Runtime System を統合して実行時 World を管理する。
///
/// GameObject は EntityId と世代番号の軽量ハンドルとして扱い、実体の生成、削除、
/// Scene への所属、Component 復元、描画・物理・Navigation 各 System との同期をここで行う。
class GameWorld final
{
  public:
    /// @brief 1 World で扱う StaticMesh Render Object の最大数。
    static constexpr uint32_t k_maxRenderObjectCount = 1000;
    /// @brief 1 World で扱う Sprite の最大数。
    static constexpr uint32_t k_maxSpriteCount = 1000;
    /// @brief 1 World で扱う ParticleEmitter の最大数。
    static constexpr uint32_t k_maxParticleEmitterCount = GpuData::k_maxParticleEmitterCount;
    /// @brief 1 World で扱う Particle の最大数。
    static constexpr uint32_t k_maxParticleCount = GpuData::k_maxParticleCount;
    /// @brief Skinning 用 Palette 行列の最大数。
    static constexpr uint32_t k_maxSkinPaletteCount = k_maxRenderObjectCount * 128u;
    /// @brief 1 World で扱う Material の最大数。
    static constexpr uint32_t k_maxMaterialCount = 1024;

    /// @brief Scene ロードや追加入力の結果を返すための値。
    struct LoadSceneResult final
    {
        /// @brief 対象 Scene の実行時 ID。
        SceneId sceneId = k_invalidSceneId;
        /// @brief 実体化された GameObject ハンドル一覧。
        std::vector<GameObject> objects{};
    };

    /// @brief EntityId の生存状態と Scene 由来情報を管理する内部レコード。
    struct EntityRecord final
    {
        /// @brief 古い GameObject ハンドルを無効化するための世代番号。
        Generation generation = 0;
        /// @brief Entity が現在生存しているか。
        bool isAlive = false;
        // 遅延削除キューへ同じ Entity を二重登録しないためのフラグ
        bool isPendingDestroy = false;
        /// @brief 生成元 Scene。Scene に属さない場合は無効 ID。
        SceneId sourceSceneId = k_invalidSceneId;
        /// @brief 生成元 SceneAsset 内の LocalObjectId。
        LocalObjectId sourceLocalObjectId = k_invalidLocalObjectId;
    };

    /// @brief ファイルからの Scene 遅延ロード要求。
    struct PendingSceneLoad final
    {
        /// @brief ロード完了後に割り当てる Scene ID。
        SceneId sceneId = k_invalidSceneId;
        /// @brief 読み込む `.cuescene` のパス。
        Core::IO::Path path{};
    };

    /// @brief Gameplay 向け Raycast 入力。
    struct GameplayRaycastDesc final
    {
        /// @brief Ray の開始位置。
        Math::float3 origin = Math::float3::zero();
        /// @brief Ray の方向。
        Math::float3 direction = Math::float3(0.0f, 0.0f, 1.0f);
        /// @brief 判定から除外する Entity。
        EntityId ignoredEntity = k_invalidEntityId;
        /// @brief Ray の最大距離。
        float distance = 1000.0f;
    };

    /// @brief Gameplay 向け Raycast 結果。
    struct GameplayRaycastHit final
    {
        /// @brief Hit した Entity。
        EntityId entity = k_invalidEntityId;
        /// @brief Hit 位置。
        Math::float3 position = Math::float3::zero();
        /// @brief Hit 面の法線。
        Math::float3 normal = Math::float3::zero();
        /// @brief Ray 開始位置から Hit 位置までの距離。
        float distance = 0.0f;
    };

    /// @brief 空の World を生成する。
    GameWorld();

    /// @brief 内部 ECSManager への非所有ポインタを取得する。
    /// @param a_outEcs ECSManager の出力先。
    [[nodiscard]] Result ecs(ECS::ECSManager *&a_outEcs) noexcept;

    /// @brief World が利用する Runtime System と GPU/Asset/Platform 依存を初期化する。
    [[nodiscard]] Result initialize(
        RHI::IBufferManager *a_bufferManager, RHI::IViewManager *a_viewManager,
        DrawSystem::IStaticMeshPool *a_staticMeshPool, AssetManager *a_assetManager,
        Core::IO::IFileSystem *a_fileSystem, Audio::IBackend *a_audioBackend,
        Audio::AudioDeviceHandle a_audioDevice, Physics::IPhysicsSystem *a_physicsSystem,
        PAL::InputManager *a_inputManager, uint32_t a_bufferCount, uint32_t a_renderWidth,
        uint32_t a_renderHeight, uint32_t a_defaultStaticMeshId,
        MaterialHandle a_defaultMaterialHandle);

    /// @brief initialize() 後に各 System の最終構築処理を行う。
    [[nodiscard]] Result finalize_systems() noexcept;

    /// @brief Asset の相対パス解決に使うルートパスを設定する。
    void set_asset_root_path(const Core::IO::Path &a_assetRootPath);

    /// @brief Asset の相対パス解決に使うルートパスを返す。
    [[nodiscard]] const Core::IO::Path &asset_root_path() const noexcept;

    /// @brief World が使用している FileSystem を返す。
    [[nodiscard]] Core::IO::IFileSystem *file_system() const noexcept;

    /// @brief Gameplay Simulation だけを進める。
    [[nodiscard]] Result simulate(float a_deltaTime);

    /// @brief Editor 表示用の更新と描画データ構築を行う。
    [[nodiscard]] Result editor_update(uint32_t a_bufferIndex, uint32_t a_renderWidth,
                                       uint32_t a_renderHeight, float a_deltaTime = 0.0f);

    /// @brief Runtime 用の Simulation と描画データ構築を行う。
    [[nodiscard]] Result update(float a_deltaTime, uint32_t a_bufferIndex, uint32_t a_renderWidth,
                                uint32_t a_renderHeight);

    /// @brief 別 World の Scene と Object 状態をこの World に複製する。
    [[nodiscard]] Result clone_from(const GameWorld &a_source);

    /// @brief 既定位置に StaticMeshObject を生成する。
    [[nodiscard]] Result add_object();

    /// @brief 既定位置に StaticMeshObject を生成し、生成した GameObject を返す。
    [[nodiscard]] Result add_object(GameObject &a_outObject);

    /// @brief 指定位置に StaticMeshObject を生成する。
    [[nodiscard]] Result add_object(const Math::float3 &a_position);

    /// @brief 指定位置に StaticMeshObject を生成し、生成した GameObject を返す。
    [[nodiscard]] Result add_object(const Math::float3 &a_position, GameObject &a_outObject);

    /// @brief 既定位置に空の GameObject を生成する。
    [[nodiscard]] Result add_game_object();

    /// @brief 既定位置に空の GameObject を生成し、生成した GameObject を返す。
    [[nodiscard]] Result add_game_object(GameObject &a_outObject);

    [[nodiscard]] Result add_game_object(const Math::float3 &a_position);

    [[nodiscard]] Result add_game_object(const Math::float3 &a_position, GameObject &a_outObject);
    [[nodiscard]] Result add_camera_object();

    [[nodiscard]] Result add_camera_object(GameObject &a_outObject);

    [[nodiscard]] Result add_camera_object(const Math::float3 &a_position);

    [[nodiscard]] Result add_camera_object(const Math::float3 &a_position, GameObject &a_outObject);
    [[nodiscard]] Result add_directional_light_object();

    [[nodiscard]] Result add_directional_light_object(GameObject &a_outObject);
    [[nodiscard]] Result add_directional_light_object(const Math::float3 &a_position);

    [[nodiscard]] Result add_directional_light_object(const Math::float3 &a_position,
                                                      GameObject &a_outObject);
    [[nodiscard]] Result add_point_light_object();

    [[nodiscard]] Result add_point_light_object(GameObject &a_outObject);

    [[nodiscard]] Result add_point_light_object(const Math::float3 &a_position);

    [[nodiscard]] Result add_point_light_object(const Math::float3 &a_position,
                                                GameObject &a_outObject);
    [[nodiscard]] Result add_spot_light_object();

    [[nodiscard]] Result add_spot_light_object(GameObject &a_outObject);

    [[nodiscard]] Result add_spot_light_object(const Math::float3 &a_position);

    [[nodiscard]] Result add_spot_light_object(const Math::float3 &a_position,
                                               GameObject &a_outObject);
    [[nodiscard]] Result add_sprite_object();

    [[nodiscard]] Result add_sprite_object(GameObject &a_outObject);

    [[nodiscard]] Result add_sprite_object(const Math::float3 &a_position);

    [[nodiscard]] Result add_sprite_object(const Math::float3 &a_position, GameObject &a_outObject);

    [[nodiscard]] Result add_object_to_scene(SceneId a_sceneId);

    [[nodiscard]] Result add_object_to_scene(SceneId a_sceneId, GameObject &a_outObject);

    [[nodiscard]] Result add_object_to_scene(SceneId a_sceneId, const Math::float3 &a_position);

    [[nodiscard]] Result add_object_to_scene(SceneId a_sceneId, const Math::float3 &a_position,
                                             GameObject &a_outObject);
    [[nodiscard]] Result add_game_object_to_scene(SceneId a_sceneId);

    [[nodiscard]] Result add_game_object_to_scene(SceneId a_sceneId, GameObject &a_outObject);

    [[nodiscard]] Result add_game_object_to_scene(SceneId a_sceneId,
                                                  const Math::float3 &a_position);

    [[nodiscard]] Result add_game_object_to_scene(SceneId a_sceneId, const Math::float3 &a_position,
                                                  GameObject &a_outObject);
    [[nodiscard]] Result add_camera_object_to_scene(SceneId a_sceneId);

    [[nodiscard]] Result add_camera_object_to_scene(SceneId a_sceneId, GameObject &a_outObject);

    [[nodiscard]] Result add_camera_object_to_scene(SceneId a_sceneId,
                                                    const Math::float3 &a_position);

    [[nodiscard]] Result add_camera_object_to_scene(SceneId a_sceneId,
                                                    const Math::float3 &a_position,
                                                    GameObject &a_outObject);
    [[nodiscard]] Result add_directional_light_object_to_scene(SceneId a_sceneId);

    [[nodiscard]] Result add_directional_light_object_to_scene(SceneId a_sceneId,
                                                               GameObject &a_outObject);

    [[nodiscard]] Result add_directional_light_object_to_scene(SceneId a_sceneId,
                                                               const Math::float3 &a_position);

    [[nodiscard]] Result add_directional_light_object_to_scene(SceneId a_sceneId,
                                                               const Math::float3 &a_position,
                                                               GameObject &a_outObject);
    [[nodiscard]] Result add_point_light_object_to_scene(SceneId a_sceneId);

    [[nodiscard]] Result add_point_light_object_to_scene(SceneId a_sceneId,
                                                         GameObject &a_outObject);

    [[nodiscard]] Result add_point_light_object_to_scene(SceneId a_sceneId,
                                                         const Math::float3 &a_position);

    [[nodiscard]] Result add_point_light_object_to_scene(SceneId a_sceneId,
                                                         const Math::float3 &a_position,
                                                         GameObject &a_outObject);
    [[nodiscard]] Result add_spot_light_object_to_scene(SceneId a_sceneId);

    [[nodiscard]] Result add_spot_light_object_to_scene(SceneId a_sceneId, GameObject &a_outObject);

    [[nodiscard]] Result add_spot_light_object_to_scene(SceneId a_sceneId,
                                                        const Math::float3 &a_position);

    [[nodiscard]] Result add_spot_light_object_to_scene(SceneId a_sceneId,
                                                        const Math::float3 &a_position,
                                                        GameObject &a_outObject);
    [[nodiscard]] Result add_sprite_object_to_scene(SceneId a_sceneId);

    [[nodiscard]] Result add_sprite_object_to_scene(SceneId a_sceneId, GameObject &a_outObject);

    [[nodiscard]] Result add_sprite_object_to_scene(SceneId a_sceneId,
                                                    const Math::float3 &a_position);

    [[nodiscard]] Result add_sprite_object_to_scene(SceneId a_sceneId,
                                                    const Math::float3 &a_position,
                                                    GameObject &a_outObject);

    /// @brief 旧 StaticMesh object id に紐付く Object を削除予約する。
    [[nodiscard]] Result remove_object(uint32_t a_objectId) noexcept;

    /// @brief 旧 StaticMesh object id から EntityId を取得する。
    [[nodiscard]] Result get_render_object_entity(uint32_t a_objectId,
                                                  EntityId &a_outEntityId) const noexcept;

    /// @brief 描画に使う Main Camera を指定する。
    [[nodiscard]] Result set_main_camera(EntityId a_cameraEntityId);

    /// @brief 指定 Entity の親 Entity を取得する。
    [[nodiscard]] Result get_parent(EntityId a_entityId, EntityId &a_outParent) const noexcept;

    /// @brief 指定 Entity の親 Entity を設定する。
    ///
    /// a_keepsWorldTransform が true の場合は、親変更後も World Transform が維持されるよう
    /// Local Transform を再計算する。
    [[nodiscard]] Result set_parent(EntityId a_childEntityId, EntityId a_parentEntityId,
                                    bool a_keepsWorldTransform) noexcept;

    /// @brief 指定 Entity を親子関係から切り離す。
    ///
    /// a_keepsWorldTransform が true の場合は、切り離し後も World Transform を維持する。
    [[nodiscard]] Result detach_parent(EntityId a_childEntityId,
                                       bool a_keepsWorldTransform) noexcept;

    /// @brief 描画 System が構築した FrameState を取得する。
    DrawSystem::DrawFrameState &draw_frame_state() noexcept;

    /// @brief 描画 System が構築した FrameState を取得する。
    const DrawSystem::DrawFrameState &draw_frame_state() const noexcept;

    /// @brief NavigationWorld を取得する。
    NavigationWorld &navigation_world() noexcept;

    /// @brief NavigationWorld を取得する。
    const NavigationWorld &navigation_world() const noexcept;

    /// @brief NavMeshAssetData を読み込み、Active NavMesh として設定する。
    [[nodiscard]] Result load_navigation_mesh(const NavMeshAssetData &a_asset,
                                              NavMeshHandle &a_outHandle) noexcept;

    /// @brief 指定パスから NavMesh を読み込み、Active NavMesh として設定する。
    [[nodiscard]] Result load_navigation_mesh_from_path(const Core::IO::Path &a_path,
                                                        NavMeshHandle &a_outHandle) noexcept;

    /// @brief 読み込み済み NavMesh を NavigationSystem の Active NavMesh に設定する。
    [[nodiscard]] Result set_active_navigation_mesh(NavMeshHandle a_handle) noexcept;

    /// @brief 読み込み済み NavMesh と元 AssetData を Active NavMesh として保持する。
    [[nodiscard]] Result set_active_navigation_mesh(NavMeshHandle a_handle,
                                                    const NavMeshAssetData &a_asset) noexcept;

    /// @brief 現在 Active な NavMesh Handle を返す。
    [[nodiscard]] NavMeshHandle active_navigation_mesh() const noexcept;

    /// @brief NavAgent に直接目的地を設定する。
    [[nodiscard]] Result set_nav_agent_destination(EntityId a_entityId,
                                                   const Math::float3 &a_destination) noexcept;

    /// @brief NavAgent に追跡対象 Entity を設定する。
    [[nodiscard]] Result set_nav_agent_target(EntityId a_entityId,
                                              EntityId a_targetEntityId) noexcept;

    /// @brief PhysicsSystem へ Raycast を発行し、Hit した Body を Entity に変換して返す。
    [[nodiscard]] Result raycast(const GameplayRaycastDesc &a_desc,
                                 GameplayRaycastHit &a_outHit) const noexcept;

    /// @brief TriggerVolume が現在重なっている Entity 一覧を取得する。
    [[nodiscard]] Result trigger_overlaps(EntityId a_entityId,
                                          std::vector<EntityId> &a_outEntities) const noexcept;

    /// @brief DebugDraw 用バッファを取得する。
    DebugDrawBuffer &debug_draw() noexcept;

    /// @brief DebugDraw 用バッファを取得する。
    const DebugDrawBuffer &debug_draw() const noexcept;

    /// @brief Active NavMesh と Agent 状態から Debug 表示用 Geometry を構築する。
    [[nodiscard]] Result build_navigation_debug_geometry(
        NavMeshDebugGeometry &a_outGeometry) noexcept;

    /// @brief DrawSystem が保持する GPU Resource 群を取得する。
    [[nodiscard]] const DrawSystem::DrawResources *draw_resources() const noexcept;

    /// @brief LightingSystem が保持する GPU Resource 群を取得する。
    [[nodiscard]] const LightingSystem::LightResources *light_resources() const noexcept;

    /// @brief ShadowSystem が保持する GPU Resource 群を取得する。
    [[nodiscard]] const ShadowSystem::ShadowResources *shadow_resources() const noexcept;

    /// @brief ParticleSystem が保持する GPU Resource 群を取得する。
    [[nodiscard]] const ParticleSystem::ParticleResources *particle_resources() const noexcept;

    /// @brief EffectSystem が保持する GPU Resource 群を取得する。
    [[nodiscard]] const EffectSystem::EffectPrimitiveResources *effect_primitive_resources()
        const noexcept;

    LightingSystem::LightFrameState &light_frame_state() noexcept;

    ParticleSystem::ParticleFrameState &particle_frame_state() noexcept;

    const ParticleSystem::ParticleFrameState &particle_frame_state() const noexcept;

    EffectSystem::EffectPrimitiveFrameState &effect_primitive_frame_state() noexcept;

    const EffectSystem::EffectPrimitiveFrameState &effect_primitive_frame_state() const noexcept;

    const LightingSystem::LightFrameState &light_frame_state() const noexcept;

    ShadowSystem::ShadowFrameState &shadow_frame_state() noexcept;

    const ShadowSystem::ShadowFrameState &shadow_frame_state() const noexcept;

    /// @brief CPU 側 StaticMesh batching の有効状態を設定する。
    void set_cpu_batching_enabled(bool a_enabled) noexcept;

    /// @brief CPU 側 StaticMesh batching が有効かを返す。
    [[nodiscard]] bool is_cpu_batching_enabled() const noexcept;

    /// @brief 名前・タグ・永続状態を指定して空の GameObject を生成する。
    [[nodiscard]] Result create_object(std::string_view a_name, std::string_view a_tag,
                                       bool a_isPersistent, GameObject &a_outObject);

    /// @brief 名前を指定して空の GameObject を生成する。
    [[nodiscard]] Result create_object(std::string_view a_name, GameObject &a_outObject);

    /// @brief SceneAsset を新しい SceneId で読み込み、Object 群を実体化する。
    [[nodiscard]] Result load_scene(const SceneAsset &a_asset, LoadSceneResult &a_outResult);

    /// @brief SceneAsset を指定 SceneId で読み込み、Object 群を実体化する。
    [[nodiscard]] Result load_scene(SceneId a_sceneId, const SceneAsset &a_asset,
                                    LoadSceneResult &a_outResult);

    /// @brief Scene 名から `.cuescene` パスを解決し、次回の遅延ロードを予約する。
    [[nodiscard]] Result request_load_scene(std::string_view a_sceneName, SceneId &a_outSceneId);

    /// @brief 既存 Scene に ObjectDefinition 群を追加して実体化する。
    [[nodiscard]] Result append_to_scene(SceneId a_sceneId,
                                         std::span<const ObjectDefinition> a_objects,
                                         LoadSceneResult &a_outResult);

    /// @brief 既存 Scene に ObjectDefinition 群を追加して実体化する。
    [[nodiscard]] Result append_to_scene(SceneId a_sceneId,
                                         const std::vector<ObjectDefinition> &a_objects,
                                         LoadSceneResult &a_outResult);

    /// @brief 既存 Scene に ObjectDefinition 1 件を追加して実体化する。
    [[nodiscard]] Result append_object_to_scene(SceneId a_sceneId, const ObjectDefinition &a_object,
                                                GameObject &a_outObject);

    /// @brief 指定 Entity の削除を遅延キューへ登録する。
    [[nodiscard]] Result destroy_object(EntityId a_entityId) noexcept;

    /// @brief 指定 Scene のアンロードを遅延キューへ登録する。
    [[nodiscard]] Result unload_scene(SceneId a_sceneId) noexcept;

    /// @brief 遅延ロード前なら予約を取り消し、ロード済みならアンロードを予約する。
    [[nodiscard]] Result request_unload_scene(SceneId a_sceneId) noexcept;

    /// @brief 遅延削除・遅延 Scene アンロードを即時実行する。
    [[nodiscard]] Result execute_deferred_deletions() noexcept;

    /// @brief EntityId から GameObject ハンドルを取得する。
    [[nodiscard]] Result find_object(EntityId a_entityId, GameObject &a_outObject) noexcept;

    /// @brief Entity が現在生存しているかを取得する。
    [[nodiscard]] Result contains_object(EntityId a_entityId, bool &a_outContains) const noexcept;

    /// @brief Scene が現在ロードされているかを取得する。
    [[nodiscard]] Result contains_scene(SceneId a_sceneId, bool &a_outContains) const noexcept;

    /// @brief Entity のタグを取得する。
    [[nodiscard]] Result get_object_tag(EntityId a_entityId, std::string &a_outTag) const;

    /// @brief Entity の表示名を取得する。
    [[nodiscard]] Result get_object_name(EntityId a_entityId, std::string &a_outName) const;

    /// @brief Entity の表示名を設定する。
    ///
    /// 同名 Object が既に存在する場合は、GameWorld 内で一意になる名前へ解決する。
    [[nodiscard]] Result set_object_name(EntityId a_entityId, std::string_view a_name);

    /// @brief Entity のタグを設定する。
    [[nodiscard]] Result set_object_tag(EntityId a_entityId, std::string_view a_tag);

    /// @brief Entity の有効状態を取得する。
    [[nodiscard]] Result is_object_active(EntityId a_entityId, bool &a_outIsActive) const noexcept;

    /// @brief 削除前の Object 状態を復元可能な Snapshot として取得する。
    [[nodiscard]] Result capture_deleted_object(EntityId a_entityId,
                                                DeletedObjectSnapshot &a_outSnapshot) const;

    /// @brief capture_deleted_object() で取得した Snapshot から Object を復元する。
    [[nodiscard]] Result restore_deleted_object(const DeletedObjectSnapshot &a_snapshot,
                                                EntityId &a_outObjectId);

    /// @brief Entity の有効状態を設定する。
    [[nodiscard]] Result set_object_active(EntityId a_entityId, bool a_isActive);

    /// @brief Entity が Scene アンロード後も残る永続 Object かを取得する。
    [[nodiscard]] Result is_object_persistent(EntityId a_entityId,
                                              bool &a_outIsPersistent) const noexcept;

    /// @brief Entity が Scene アンロード後も残る永続 Object かを設定する。
    [[nodiscard]] Result set_object_persistent(EntityId a_entityId, bool a_isPersistent);

    /// @brief EntityId と世代番号が現在も同じ生存 Entity を指しているかを取得する。
    [[nodiscard]] Result is_alive(EntityId a_entityId, Generation a_generation,
                                  bool &a_outIsAlive) const noexcept;

    /// @brief Entity の生成元 SceneId を取得する。
    [[nodiscard]] Result source_scene_id(EntityId a_entityId, SceneId &a_outSceneId) const noexcept;

    /// @brief 現在生存している Object 数を取得する。
    [[nodiscard]] Result object_count(size_t &a_outCount) const noexcept;

    /// @brief RigidBody の線形速度を設定し、PhysicsSystem 側にも反映する。
    [[nodiscard]] Result set_rigid_body_linear_velocity(EntityId a_entityId,
                                                        Math::float3 a_velocity) noexcept;

    /// @brief RigidBody の線形速度を取得する。
    [[nodiscard]] Result get_rigid_body_linear_velocity(EntityId a_entityId,
                                                        Math::float3 &a_outVelocity) const noexcept;

    /// @brief RigidBody に継続的な Force を加える。
    [[nodiscard]] Result add_rigid_body_force(EntityId a_entityId, Math::float3 a_force) noexcept;

    /// @brief RigidBody に瞬間的な Impulse を加える。
    [[nodiscard]] Result add_rigid_body_impulse(EntityId a_entityId,
                                                Math::float3 a_impulse) noexcept;

    /// @brief CharacterController の移動速度を設定する。
    [[nodiscard]] Result set_character_move_velocity(EntityId a_entityId,
                                                     Math::float3 a_velocity) noexcept;

    /// @brief CharacterController に Jump 要求を設定する。
    [[nodiscard]] Result request_character_jump(EntityId a_entityId) noexcept;

    /// @brief 現在ロードされている Scene 数を取得する。
    [[nodiscard]] Result scene_count(size_t &a_outCount) const noexcept;

    /// @brief World 内の Scene、Object、遅延要求、所有 SceneAsset を破棄する。
    [[nodiscard]] Result clear() noexcept;

    /// @brief 指定 Entity から Component を取得する。
    ///
    /// 外部向け API として Result を返し、Component が無い場合は a_outComponent を nullptr にする。
    template <typename T>
    [[nodiscard]] Result get_component(EntityId a_entityId, T *&a_outComponent) noexcept
    {
        a_outComponent = get_component<T>(a_entityId);
        return a_outComponent != nullptr ? Result::ok()
                                         : Result::fail(Code::NotFound, Severity::Warning,
                                                        "GameWorld component was not found.");
    }

    /// @brief 指定 Entity から読み取り専用 Component を取得する。
    template <typename T>
    [[nodiscard]] Result get_component(EntityId a_entityId, const T *&a_outComponent) const noexcept
    {
        a_outComponent = get_component<T>(a_entityId);
        return a_outComponent != nullptr ? Result::ok()
                                         : Result::fail(Code::NotFound, Severity::Warning,
                                                        "GameWorld component was not found.");
    }

    /// @brief 指定 Entity に Component を追加し、追加した Component を取得する。
    template <typename T, typename... Args>
    [[nodiscard]] Result add_component(EntityId a_entityId, T *&a_outComponent, Args &&...a_args)
    {
        a_outComponent = nullptr;
        return capture_result(
            [this, &a_outComponent, a_entityId, &a_args...]()
            { a_outComponent = &add_component<T>(a_entityId, std::forward<Args>(a_args)...); });
    }

    /// @brief 指定 Entity が Component を持っているかを取得する。
    template <typename T>
    [[nodiscard]] Result has_component(EntityId a_entityId, bool &a_outHasComponent) const noexcept
    {
        a_outHasComponent = has_component<T>(a_entityId);
        return contains_object(a_entityId) ? Result::ok()
                                           : Result::fail(Code::NotFound, Severity::Warning,
                                                          "GameWorld object was not found.");
    }

    /// @brief 指定 Entity から Component を削除する。
    template <typename T> [[nodiscard]] Result remove_component(EntityId a_entityId) noexcept
    {
        if (!contains_object(a_entityId))
        {
            return Result::fail(Code::NotFound, Severity::Warning,
                                "GameWorld object was not found.");
        }

        m_ecs.remove_component<T>(a_entityId);
        return Result::ok();
    }

    /// @brief 指定 Entity を GameObject として取得して関数を実行する。
    template <class F> [[nodiscard]] Result visit_object(EntityId a_entityId, F &&a_func)
    {
        GameObject object = find_object(a_entityId);
        if (!object.is_valid())
        {
            return Result::fail(Code::NotFound, Severity::Warning,
                                "GameWorld object was not found.");
        }

        a_func(object.entity_id(), source_scene_id(a_entityId), object);
        return Result::ok();
    }

    /// @brief 指定 Scene に属する生存 Object を順に訪問する。
    template <class F> [[nodiscard]] Result for_each_object_in_scene(SceneId a_sceneId, F &&a_func)
    {
        auto sceneIt = m_scenes.find(a_sceneId);
        if (sceneIt == m_scenes.end())
        {
            return Result::fail(Code::NotFound, Severity::Warning,
                                "GameWorld scene was not found.");
        }

        const std::vector<EntityId> entities = sceneIt->second.entities;
        for (const EntityId entity : entities)
        {
            GameObject object = find_object(entity);
            if (!object.is_valid())
            {
                continue;
            }

            a_func(object.entity_id(), a_sceneId, object);
        }

        return Result::ok();
    }

    /// @brief World 内の生存 Object を順に訪問する。
    template <class F> [[nodiscard]] Result for_each_object(F &&a_func)
    {
        for (EntityId entity = 0; entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
        {
            GameObject object = find_object(entity);
            if (!object.is_valid())
            {
                continue;
            }

            a_func(object.entity_id(), source_scene_id(entity), object);
        }

        return Result::ok();
    }

    /// @brief 指定タグを持つ Object をすべて取得する。
    [[nodiscard]] Result find_objects_by_tag(std::string_view a_tag,
                                             std::vector<GameObject> &a_outObjects);

    /// @brief 指定名と一致する Object をすべて取得する。
    [[nodiscard]] Result find_objects_by_name(std::string_view a_name,
                                              std::vector<GameObject> &a_outObjects);

    /// @brief 指定名と一致する最初の Object を取得する。
    [[nodiscard]] Result find_object_by_name(std::string_view a_name, GameObject &a_outObject);

    /// @brief 指定名と一致する最初の Object を削除予約する。
    [[nodiscard]] Result destroy_object_by_name(std::string_view a_name) noexcept;

    /// @brief 指定名と一致する Object をすべて削除予約する。
    [[nodiscard]] Result destroy_objects_by_name(std::string_view a_name,
                                                 size_t &a_outCount) noexcept;

    /// @brief `Name`, `Name(1)` のような連番名に属する Object を取得する。
    [[nodiscard]] Result find_objects_by_name_series(std::string_view a_baseName,
                                                     std::vector<GameObject> &a_outObjects);

    [[nodiscard]] Result destroy_objects_by_name_series(std::string_view a_baseName,
                                                        size_t &a_outCount) noexcept;

    [[nodiscard]] Result destroy_objects_by_tag(std::string_view a_tag,
                                                size_t &a_outCount) noexcept;

  private:
    [[nodiscard]] Math::float3 make_spawn_position() const noexcept;

    [[nodiscard]] Math::float3 make_camera_spawn_position() const noexcept;

    [[nodiscard]] Math::float3 make_sprite_spawn_position() const noexcept;

    [[nodiscard]] Math::float3 make_light_spawn_position() const noexcept;

    [[nodiscard]] static Math::float3 multiply_components(const Math::float3 &a_left,
                                                          const Math::float3 &a_right) noexcept;

    [[nodiscard]] static Math::float3 divide_components_safe(const Math::float3 &a_left,
                                                             const Math::float3 &a_right) noexcept;

    [[nodiscard]] static Math::float3 rotate_vector(const Math::Quaternion &a_rotation,
                                                    const Math::float3 &a_value) noexcept;

    [[nodiscard]] static ECS::WorldTransformComponent compose_world_transform(
        const ECS::WorldTransformComponent &a_parent,
        const ECS::TransformComponent &a_local) noexcept;

    [[nodiscard]] static ECS::TransformComponent make_local_transform(
        const ECS::WorldTransformComponent &a_parent,
        const ECS::WorldTransformComponent &a_world) noexcept;

    [[nodiscard]] bool is_descendant_of(EntityId a_entityId,
                                        EntityId a_potentialAncestorId) const noexcept;

    [[nodiscard]] bool resolve_world_transform(EntityId a_entityId, std::vector<uint8_t> &a_state,
                                               ECS::WorldTransformComponent &a_outWorld) noexcept;

    void sync_world_transforms() noexcept;

    void sync_draw_frame_state(uint32_t a_bufferIndex, uint32_t a_renderWidth,
                               uint32_t a_renderHeight) noexcept;

    [[nodiscard]] Result upload_draw_scene(uint32_t a_bufferIndex);
    [[nodiscard]] Result upload_particle_scene(uint32_t a_bufferIndex);
    [[nodiscard]] Result upload_effect_primitive_scene(uint32_t a_bufferIndex);
    [[nodiscard]] Result upload_light_scene(uint32_t a_bufferIndex);
    [[nodiscard]] Result upload_shadow_scene(uint32_t a_bufferIndex);

    void animate_static_mesh_objects(float a_deltaTime);

    [[nodiscard]] std::vector<EntityId> collect_active_static_mesh_entities() const;

    [[nodiscard]] std::vector<EntityId> collect_camera_entities() const;

    [[nodiscard]] size_t count_active_static_mesh_objects() const;

    [[nodiscard]] bool try_get_static_mesh_entity(uint32_t a_objectId,
                                                  EntityId &a_outEntityId) const noexcept;

    template <typename F> [[nodiscard]] static Result capture_result(F &&a_func)
    {
        try
        {
            a_func();
            return Result::ok();
        }
        catch (const std::bad_alloc &)
        {
            return Result::fail(Code::OutOfMemory, Severity::Error, "GameWorld out of memory.");
        }
        catch (const std::overflow_error &a_error)
        {
            return map_exception_message(a_error.what());
        }
        catch (const std::runtime_error &a_error)
        {
            return map_exception_message(a_error.what());
        }
        catch (const std::exception &)
        {
            return Result::fail(Code::UnknownError, Severity::Error,
                                "GameWorld unknown exception.");
        }
    }

    [[nodiscard]] static Result map_exception_message(std::string_view a_message) noexcept;

    [[nodiscard]] GameObject create_object(std::string_view a_name,
                                           std::string_view a_tag = "Default",
                                           bool a_isPersistent = false);

    [[nodiscard]] GameObject instantiate_object(const ObjectDefinition &a_object);

    [[nodiscard]] LoadSceneResult load_scene(const SceneAsset &a_asset);

    [[nodiscard]] LoadSceneResult load_scene(SceneId a_sceneId, const SceneAsset &a_asset);

    [[nodiscard]] LoadSceneResult append_to_scene(SceneId a_sceneId,
                                                  std::span<const ObjectDefinition> a_objects);

    [[nodiscard]] GameObject append_object_to_scene(SceneId a_sceneId,
                                                    const ObjectDefinition &a_object);

    void destroy_object_internal(EntityId a_entityId) noexcept;

    [[nodiscard]] bool unload_scene_internal(SceneId a_sceneId) noexcept;

    void execute_deferred_deletions_internal() noexcept;

    [[nodiscard]] Result execute_deferred_scene_loads();

    [[nodiscard]] Result resolve_scene_path(std::string_view a_sceneName,
                                            Core::IO::Path &a_outPath) const noexcept;

    [[nodiscard]] GameObject find_object(EntityId a_entityId) noexcept;

    [[nodiscard]] bool contains_object(EntityId a_entityId) const noexcept;

    [[nodiscard]] bool contains_scene(SceneId a_sceneId) const noexcept;

    void localize_script_entity_references(ECS::ScriptComponent &a_script,
                                           EntityId a_sourceEntityId) const noexcept;

    [[nodiscard]] EntityId localize_entity_reference(EntityId a_entityValue,
                                                     EntityId a_sourceEntityId) const noexcept;

    void resolve_script_entity_references(
        EntityId a_entityId, const SceneInstance &a_scene,
        const std::unordered_map<LocalObjectId, EntityId> &a_newLocalObjectToEntity) noexcept;

    void resolve_component_entity_references(
        EntityId a_entityId, const SceneInstance &a_scene,
        const std::unordered_map<LocalObjectId, EntityId> &a_newLocalObjectToEntity) noexcept;

    [[nodiscard]] std::string get_object_tag(EntityId a_entityId) const;

    [[nodiscard]] std::string get_object_name(EntityId a_entityId) const;

    [[nodiscard]] GameObjectProto build_object_prototype(EntityId a_entityId,
                                                         const BaseComponent &a_base) const;

    [[nodiscard]] bool is_object_persistent(EntityId a_entityId) const noexcept;

    [[nodiscard]] bool is_alive(EntityId a_entityId, Generation a_generation) const noexcept;

    [[nodiscard]] SceneId source_scene_id(EntityId a_entityId) const noexcept;

    template <typename T> [[nodiscard]] T *get_component(EntityId a_entityId) noexcept
    {
        if (!contains_object(a_entityId))
        {
            return nullptr;
        }

        return m_ecs.get_component<T>(a_entityId);
    }

    template <typename T> [[nodiscard]] const T *get_component(EntityId a_entityId) const noexcept
    {
        return const_cast<GameWorld *>(this)->get_component<T>(a_entityId);
    }

    template <typename T, typename... Args> T &add_component(EntityId a_entityId, Args &&...a_args)
    {
        if (!contains_object(a_entityId))
        {
            throw std::runtime_error("GameWorld object is not alive.");
        }

        T *component = m_ecs.add_component<T>(a_entityId);
        if (component == nullptr)
        {
            throw std::runtime_error("GameWorld failed to add component.");
        }

        *component = T{std::forward<Args>(a_args)...};
        return *component;
    }

    template <typename T> [[nodiscard]] bool has_component(EntityId a_entityId) const noexcept
    {
        return get_component<T>(a_entityId) != nullptr;
    }

    [[nodiscard]] std::vector<GameObject> find_objects_by_tag(std::string_view a_tag);

    [[nodiscard]] std::vector<GameObject> find_objects_by_name(std::string_view a_name);

    [[nodiscard]] GameObject find_object_by_name(std::string_view a_name);

    [[nodiscard]] std::vector<GameObject> find_objects_by_name_series(std::string_view a_baseName);

    [[nodiscard]] SceneId generate_scene_id();

    [[nodiscard]] EntityId create_entity_record(SceneId a_sourceSceneId,
                                                LocalObjectId a_localObjectId);

    void initialize_base_component(EntityId a_entityId, std::string_view a_name,
                                   std::string_view a_tag, SceneId a_owningSceneId,
                                   EntityId a_parent, bool a_isActive, bool a_isPersistent);

    [[nodiscard]] LoadSceneResult instantiate_into_scene(
        SceneId a_sceneId, std::span<const ObjectDefinition> a_objects, const SceneAsset *a_asset);

    void destroy_object_immediately(EntityId a_entityId) noexcept;

    [[nodiscard]] bool unload_scene_immediately(SceneId a_sceneId) noexcept;

    [[nodiscard]] bool unlink_object_from_scene(EntityId a_entityId) noexcept;

    [[nodiscard]] GameObject make_handle(EntityId a_entityId) noexcept;

    [[nodiscard]] EntityRecord *try_get_entity_record(EntityId a_entityId) noexcept;

    [[nodiscard]] const EntityRecord *try_get_entity_record(EntityId a_entityId) const noexcept;

    void add_object_to_tag_index(EntityId a_entityId, const std::string &a_tag);

    void add_object_to_name_index(EntityId a_entityId, const std::string &a_name);

    [[nodiscard]] std::string normalize_object_name(std::string_view a_name) const;

    [[nodiscard]] bool is_name_taken(std::string_view a_name,
                                     EntityId a_ignoredEntityId = k_invalidEntityId) const;

    [[nodiscard]] std::string make_unique_object_name(
        std::string_view a_requestedName, EntityId a_ignoredEntityId = k_invalidEntityId) const;

    [[nodiscard]] bool try_get_name_series_index(const std::string &a_name,
                                                 std::string_view a_baseName,
                                                 std::uint32_t &a_outSeriesIndex) const;

    void remove_object_from_tag_index(EntityId a_entityId, const std::string &a_tag);

    void remove_object_from_name_index(EntityId a_entityId, const std::string &a_name);

    [[nodiscard]] bool find_entity_by_body(Physics::RigidBodyHandle a_body,
                                           EntityId &a_outEntity) const noexcept;

    /// @brief Entity と Component の実体を保持する ECS。
    ECS::ECSManager m_ecs{};
    /// @brief Editor 更新で実行する System Pipeline。
    ECS::ECSManager::SystemPipeline m_editorPipeline{};
    /// @brief Runtime Simulation で実行する System Pipeline。
    ECS::ECSManager::SystemPipeline m_simulationPipeline{};
    /// @brief NavMesh と経路探索状態を保持する Navigation World。
    NavigationWorld m_navigationWorld{};
    /// @brief ECS 側 NavigationSystem への非所有ポインタ。
    ECS::NavigationSystem *m_navigationSystem = nullptr;
    /// @brief 現在 Simulation に使う NavMesh。
    NavMeshHandle m_activeNavMesh{};
    /// @brief Active NavMesh の元 AssetData。
    NavMeshAssetData m_activeNavMeshAsset{};
    /// @brief DrawSystem の GPU Resource 群。
    std::unique_ptr<DrawSystem::DrawResources> m_drawResources = nullptr;
    /// @brief LightingSystem の GPU Resource 群。
    std::unique_ptr<LightingSystem::LightResources> m_lightResources = nullptr;
    /// @brief ShadowSystem の GPU Resource 群。
    std::unique_ptr<ShadowSystem::ShadowResources> m_shadowResources = nullptr;
    /// @brief ParticleSystem の GPU Resource 群。
    std::unique_ptr<ParticleSystem::ParticleResources> m_particleResources = nullptr;
    /// @brief EffectSystem の GPU Resource 群。
    std::unique_ptr<EffectSystem::EffectPrimitiveResources> m_effectPrimitiveResources = nullptr;
    /// @brief Asset 解決に使う AssetManager の非所有ポインタ。
    AssetManager *m_assetManager = nullptr;
    /// @brief Scene/NavMesh などのファイル入出力に使う FileSystem。
    Core::IO::IFileSystem *m_fileSystem = nullptr;
    /// @brief AudioSource 再生に使う Audio Backend。
    Audio::IBackend *m_audioBackend = nullptr;
    /// @brief Physics Body と Raycast に使う PhysicsSystem。
    Physics::IPhysicsSystem *m_physicsSystem = nullptr;
    /// @brief 入力参照に使う InputManager。
    PAL::InputManager *m_inputManager = nullptr;
    /// @brief Audio Backend 上の出力 Device。
    Audio::AudioDeviceHandle m_audioDevice{};
    /// @brief Debug 描画要求の一時バッファ。
    DebugDrawBuffer m_debugDraw{};
    /// @brief Asset 相対パス解決の基準パス。
    Core::IO::Path m_assetRootPath{};
    /// @brief StaticMesh の CPU batching を有効にするか。
    bool m_isCpuBatchingEnabled = false;
    /// @brief Active NavMesh の AssetData を保持しているか。
    bool m_hasActiveNavMeshAsset = false;
    /// @brief Text 描画で使う FontAtlas 管理。
    DrawSystem::FontAtlasManager m_fontAtlasManager{};
    /// @brief DrawSystem に渡す Scene 単位の描画入力。
    DrawSystem::DrawScene m_drawScene{};
    /// @brief DrawSystem が出力する Frame 単位の描画状態。
    DrawSystem::DrawFrameState m_drawFrameState{};
    /// @brief ParticleSystem に渡す Scene 単位の入力。
    ParticleSystem::ParticleScene m_particleScene{};
    /// @brief ParticleSystem が出力する Frame 単位の状態。
    ParticleSystem::ParticleFrameState m_particleFrameState{};
    /// @brief Particle 実行時領域の範囲割り当て管理。
    ParticleSystem::ParticleRangeAllocator m_particleRangeAllocator{};
    /// @brief EffectSystem に渡す Scene 単位の入力。
    EffectSystem::EffectPrimitiveScene m_effectPrimitiveScene{};
    /// @brief EffectSystem が出力する Frame 単位の状態。
    EffectSystem::EffectPrimitiveFrameState m_effectPrimitiveFrameState{};
    /// @brief Particle Trail 更新用のフレームカウンタ。
    uint32_t m_particleTrailFrameIndex = 0;
    /// @brief LightingSystem に渡す Scene 単位の入力。
    LightingSystem::LightScene m_lightScene{};
    /// @brief LightingSystem が出力する Frame 単位の状態。
    LightingSystem::LightFrameState m_lightFrameState{};
    /// @brief ShadowSystem に渡す Scene 単位の入力。
    ShadowSystem::ShadowScene m_shadowScene{};
    /// @brief ShadowSystem が出力する Frame 単位の状態。
    ShadowSystem::ShadowFrameState m_shadowFrameState{};
    /// @brief 既定生成 Object に割り当てる Material。
    MaterialHandle m_defaultMaterialHandle{};
    /// @brief ロード済み Scene の実行時状態。
    std::unordered_map<SceneId, SceneInstance> m_scenes{};
    /// @brief ファイルから遅延ロードした SceneAsset の所有領域。
    std::unordered_map<SceneId, std::unique_ptr<SceneAsset>> m_ownedSceneAssets{};
    /// @brief Object 名から Entity を逆引きする索引。
    std::unordered_map<std::string, std::unordered_set<EntityId>> m_nameIndex{};
    /// @brief Object タグから Entity を逆引きする索引。
    std::unordered_map<std::string, std::unordered_set<EntityId>> m_tagIndex{};
    /// @brief EntityId ごとの生存状態と世代情報。
    std::vector<EntityRecord> m_entityRecords{};
    // 公開 API の遅延削除要求を一時的に保持するキュー
    std::vector<EntityId> m_pendingDestroyedEntities{};
    /// @brief 次回 flush 時にファイルから読み込む Scene 一覧。
    std::vector<PendingSceneLoad> m_pendingLoadedScenes{};
    /// @brief 次回 flush 時にアンロードする Scene 一覧。
    std::vector<SceneId> m_pendingUnloadedScenes{};
    /// @brief 次に発行する SceneId。
    SceneId m_nextSceneId = 1;
    /// @brief 現在生存している Entity 数。
    size_t m_liveObjectCount = 0;
    /// @brief collect_camera_entities() 内での Main Camera index。
    uint32_t m_mainCameraIndex = 0;
    /// @brief 既定 StaticMeshObject に使う Mesh ID。
    uint32_t m_defaultStaticMeshId = ECS::k_invalidMeshId;
};

template <typename T> inline Result GameObject::get_component(T *&a_outComponent) noexcept
{
    if (!is_valid())
    {
        a_outComponent = nullptr;
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->get_component<T>(m_entityId, a_outComponent);
}

template <typename T, typename... Args>
inline Result GameObject::add_component(T *&a_outComponent, Args &&...a_args)
{
    if (!is_valid())
    {
        a_outComponent = nullptr;
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->add_component<T>(m_entityId, a_outComponent, std::forward<Args>(a_args)...);
}

template <typename T>
inline Result GameObject::has_component(bool &a_outHasComponent) const noexcept
{
    if (!is_valid())
    {
        a_outHasComponent = false;
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->has_component<T>(m_entityId, a_outHasComponent);
}

template <typename T> inline Result GameObject::remove_component() noexcept
{
    if (!is_valid())
    {
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->remove_component<T>(m_entityId);
}

} // namespace Cue::GameCore
