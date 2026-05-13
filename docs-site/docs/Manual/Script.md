---
sidebar_position: 2
title: Script の使い方
---

# Script の使い方

Cue Engine の Script は `Marionette` フレームワークを使う C++ Script です。
プロジェクト側の `Assets/Scripts/*Script.h` と `Assets/Scripts/*Script.cpp` から `GameScript.dll` をビルドし、Editor が `ScriptComponent` として読み込みます。

## Script プロジェクト構成

新規プロジェクトには Script 用の CMake 構成が生成されます。

```text
CMakeLists.txt
CMakePresets.json
EngineModule/ScriptRegistry.h
Assets/Scripts/
Intermediate/Generated/
```

`CMakeLists.txt` は `Assets/Scripts/*Script.cpp` と `Assets/Scripts/*Script.h` を収集し、次の target を作ります。

- `GameScript`: Editor が読み込む DLL です。
- `Game`: 配布用 `CueApp` に静的リンクするライブラリです。
- `CueApp`: 配布用の Windows 実行ファイルです。

Editor は常に CMake で GameScript をビルドします。
読み込みに使う構成は `scriptBuildConfiguration` と同じです。
古い `scriptLoadConfiguration` と `scriptBuildBackend` は現在使いません。

`Tools/CMake/GenerateScriptRegistry.cmake` は `Assets/Scripts/*Script.h` を読み、`MARIONETTE_DECLARE_SCRIPT_TYPE` と `make_*_script_definition()` を持つ Script を `Intermediate/Generated/ScriptRegistry.gen.cpp` に登録します。
この生成ファイルは手で編集しません。

## Script を追加する

Editor から追加する場合は、`DebugView > 追加 > GameScript を追加` を選び、Script 名を入力します。
Script 名には英数字と `_` のみ使えます。先頭に数字は使えません。

たとえば `RotateCube` を追加すると、次の 2 ファイルが作成されます。

```text
Assets/Scripts/RotateCubeScript.h
Assets/Scripts/RotateCubeScript.cpp
```

生成される最小形は次の構造です。

```cpp
#pragma once

#include <ScriptFramework/Marionette.h>

MARIONETTE_DECLARE_SCRIPT_TYPE(RotateCube, "RotateCube");

class RotateCube final : public Marionette::Behaviour<RotateCube>
{
public:
    using StateBlob = Marionette::StateBlob<RotateCube>;
    using Marionette::Behaviour<RotateCube>::update;
    MARIONETTE_NO_FIELDS();
    MARIONETTE_NO_FUNCTIONS();

    void start();
    void update();
};

[[nodiscard]] Cue::Core::Native::ScriptClassDefinition
make_rotate_cube_script_definition() noexcept;
```

```cpp
#include "RotateCubeScript.h"

void RotateCube::start()
{
}

void RotateCube::update()
{
}

MARIONETTE_DEFINE_SCRIPT(rotate_cube, RotateCube);
```

`MARIONETTE_DECLARE_SCRIPT_TYPE`、`make_*_script_definition()` 宣言、`MARIONETTE_DEFINE_SCRIPT` の 3 点が揃っていない Script は自動登録されません。

## public field

Inspector に出す値は `MARIONETTE_FIELDS` で定義します。
値を読み込む処理は `bind_fields` に書きます。

```cpp
class RotateCube final : public Marionette::Behaviour<RotateCube>
{
public:
    using StateBlob = Marionette::StateBlob<RotateCube>;
    using Marionette::Behaviour<RotateCube>::update;
    MARIONETTE_FIELDS(
        CUE_FIELD_FLOAT_META(
            "Rotation",
            "rotationSpeed",
            0.78539816339f,
            Marionette::EditAnywhere | Marionette::Serialize)
    );

    void bind_fields(const Marionette::ScriptFieldReader& a_reader);
    void update();

private:
    float rotationSpeed = 0.78539816339f;
};
```

```cpp
void RotateCube::bind_fields(const Marionette::ScriptFieldReader& a_reader)
{
    (void)read_float(a_reader, "rotationSpeed", rotationSpeed);
}
```

現在使える主な field macro は次の通りです。

- `CUE_FIELD_FLOAT` / `CUE_FIELD_FLOAT_META`
- `CUE_FIELD_INT32` / `CUE_FIELD_INT32_META`
- `CUE_FIELD_BOOL` / `CUE_FIELD_BOOL_META`
- `CUE_FIELD_ENTITY` / `CUE_FIELD_ENTITY_META`
- `CUE_FIELD_SCRIPT_CLASS` / `CUE_FIELD_SCRIPT_CLASS_META`
- `MARIONETTE_OBJECT_PTR_FIELD`

`EditAnywhere` を付けると Inspector で編集できます。
`Serialize` を付けると Scene 保存対象になります。
`ReadOnly` を付けると Inspector 上で編集不可になります。

## Script 関数

Inspector や他 Script から呼び出す関数は `MARIONETTE_FUNCTIONS` で登録します。

```cpp
class RotateCube final : public Marionette::Behaviour<RotateCube>
{
public:
    using StateBlob = Marionette::StateBlob<RotateCube>;
    using Marionette::Behaviour<RotateCube>::update;
    MARIONETTE_NO_FIELDS();
    MARIONETTE_FUNCTIONS(
        MARIONETTE_FUNCTION(RotateCube, reset_rotation),
        MARIONETTE_FUNCTION(RotateCube, toggle_direction)
    );

    void reset_rotation();
    void toggle_direction();
};
```

`ScriptRef` を取得している場合は、名前で関数を呼び出せます。

```cpp
const Marionette::ScriptRef<RotateCube> rotateCube = target.script_ref();
if (rotateCube.is_valid())
{
    (void)rotateCube.invoke("toggle_direction");
}
```

## Behaviour から使える主な機能

`Marionette::Behaviour<T>` は owner Entity に紐付く Component 的な Script です。
Script 内では次の操作を使えます。

- `start()`: Script instance 生成後の初回更新で呼ばれます。
- `update()` または `update(float)`: 毎フレーム呼ばれます。
- `delta_time()`: 現在フレームの delta time を返します。
- `self()` / `owner()` / `get_owner()`: owner Entity handle を返します。
- `is_entity_valid()`: owner Entity が有効か確認します。
- `has_transform()`、`get_transform()`、`set_transform()`: Transform を読み書きします。`Transform::rotation` は弧度法の Euler 回転です。
- `get_transform_degrees()`、`set_transform_degrees()`: `rotation` を度数法で読み書きする Transform API です。
- `push_key(Marionette::Key::...)`: 入力状態を確認します。
- `request_audio_source_play()`: owner の AudioSource 再生を要求します。
- `log_info()`、`log_warning()`、`log_error()`: Engine log へ出力します。
- `get_script<T>()` / `find_script<T>()`: 同一または指定 Entity の Script instance を検索します。
- `set_rigid_body_linear_velocity()` / `get_rigid_body_linear_velocity()`: RigidBody の線形速度を読み書きします。
- `add_rigid_body_force()` / `add_rigid_body_impulse()`: RigidBody に力またはインパルスを加えます。
- `set_character_move_velocity()` / `request_character_jump()`: CharacterController の水平移動とジャンプを要求します。
- `set_nav_agent_destination()` / `set_nav_agent_target()`: NavAgent の目的地または追跡対象を設定します。

Transform を動かす最小例です。

```cpp
void RotateCube::update()
{
    if (!is_entity_valid() || !has_transform())
    {
        return;
    }

    Transform transform{};
    if (get_transform(transform) != CueResult_Ok)
    {
        return;
    }

    transform.rotation.y += delta_time() * rotationSpeed;
    (void)set_transform(transform);
}
```

`rotationSpeed` は rad/s として扱います。
度数法で扱いたい場合は `get_transform_degrees()` と `set_transform_degrees()` を使います。

## CharacterController を使った移動

Player のような入力移動は、`CharacterControllerComponent` と `RigidBodyComponent` と `ColliderComponent` を持つ Entity で扱います。
Script からは水平速度を渡します。
Y 方向の重力、接地判定、地面への snap は Engine 側の CharacterControllerSystem が処理します。

```cpp
void Player::update()
{
    CueFloat3 velocity{};
    const float speed = 6.0f;

    if (push_key(Marionette::Key::W))
    {
        velocity.z += speed;
    }
    if (push_key(Marionette::Key::S))
    {
        velocity.z -= speed;
    }
    if (push_key(Marionette::Key::A))
    {
        velocity.x -= speed;
    }
    if (push_key(Marionette::Key::D))
    {
        velocity.x += speed;
    }

    (void)set_character_move_velocity(velocity);
}
```

`CharacterControllerComponent` は kinematic body として動きます。
動的な物理挙動にしたい場合は `RigidBodyComponent` を Dynamic にして、`set_rigid_body_linear_velocity()`、`add_rigid_body_force()`、`add_rigid_body_impulse()` を使います。

## NavAgent を使った追跡

NavAgent は NavMesh 上の経路探索を担当します。
`movementMode` は次の 2 種類です。

- `DirectTransform`: NavAgent が Transform を直接動かします。
- `DesiredVelocityOnly`: NavAgent は `desiredVelocity` だけを計算します。`CharacterControllerComponent` がある場合、NavAgentMotorSystem がその速度を CharacterController に渡します。

物理床の上を移動する Enemy には `DesiredVelocityOnly` を使います。
この場合、NavMesh の高さへ Transform を直接スナップせず、水平速度だけが CharacterController に渡されます。

```cpp
void Enemy::start()
{
    if (m_target.entity_handle().value != k_cueInvalidHandleValue)
    {
        (void)set_nav_agent_target(m_target.entity_handle());
    }
}
```

`set_nav_agent_target()` を使うと、NavAgent は対象 Entity の現在 Transform を追い続けます。
固定地点へ移動したい場合は `set_nav_agent_destination()` を使います。

## SceneManager

どの Script からでも `Marionette::SceneManager` を使えます。
Scene の読み込みと削除は遅延実行され、現在の Script update 中に World を直接変更しません。

```cpp
void Door::open_next_area()
{
    const Marionette::SceneId sceneId =
        Marionette::SceneManager.load_scene("BattleField");
    if (sceneId == Marionette::k_invalidSceneId)
    {
        log_error("Scene load request failed.");
    }
}
```

```cpp
void Area::close()
{
    (void)Marionette::SceneManager.unload_scene(m_sceneId);
}
```

`load_scene()` は Scene 名を受け取り、読み込み予約に成功した SceneId を返します。
`unload_scene()` は SceneId を指定して削除を予約します。

## Script 参照

別の Script を Inspector から参照させる場合は `MARIONETTE_OBJECT_PTR_FIELD` と `ScriptObjectPtr<T>` を使います。

```cpp
MARIONETTE_DECLARE_SCRIPT_TYPE(LookupRotateCube, "LookupRotateCube");

class LookupRotateCube final : public Marionette::Behaviour<LookupRotateCube>
{
public:
    using StateBlob = Marionette::StateBlob<LookupRotateCube>;
    using Marionette::Behaviour<LookupRotateCube>::update;
    MARIONETTE_FIELDS(
        MARIONETTE_OBJECT_PTR_FIELD(RotateCube, "target")
    );

    void bind_fields(const Marionette::ScriptFieldReader& a_reader);
    void update();

private:
    Marionette::ScriptObjectPtr<RotateCube> target{};
};
```

```cpp
void LookupRotateCube::bind_fields(
    const Marionette::ScriptFieldReader& a_reader)
{
    (void)read_object_ptr(a_reader, "target", target);
}
```

Editor 側では `targetEntity` と `targetScriptClass` の組として扱われます。
参照先 Entity に ScriptComponent がない場合や class が一致しない場合は、Inspector に診断が表示されます。
Scene 保存時は localObjectId として保存され、読み込み時に Runtime Entity へ解決されます。

## Editor で ScriptComponent を使う

1. `DebugView > 追加 > GameScript を追加` で Script を作成します。
2. Script の `.h` / `.cpp` を編集します。
3. Editor Window にフォーカスを戻します。
4. Editor が変更検出後に GameScript を自動ビルドして再読み込みします。
5. `ヒエラルキー` で GameObject を選択します。
6. `Inspector` で `ScriptComponent` を追加します。
7. `className` の combo から登録済み Script class を選択します。
8. `public fields` を編集します。
9. `ファイル > シーンを保存` で Scene に保存します。

`className` に登録済み class が表示されない場合は、GameScript のビルド失敗、reload 失敗、または ABI mismatch の可能性があります。
`Script Build Output` の summary、message、log を確認してください。

Script の field 定義を変更した後も、自動ビルドと再読み込みが走ります。
Inspector に `field 定義との差分があります。` と表示された場合は、必要に応じて `定義に合わせる` または `Reset Fields` を使います。

## Visual Studio 連携

`ビルド > GameScript solution を開く` で `scriptRoot` の CMake preset から Visual Studio solution を開けます。
`ビルド > Editor にデバッガをアタッチ` で、Visual Studio から現在の Editor プロセスへアタッチできます。

## 注意点

- `GameScript.dll` は `scriptBuildConfiguration` に指定された構成から読み込まれます。
- ABI version が Engine と一致しない GameScript はロードされません。該当する場合は再ビルドしてください。
- `Assets/Scripts/*Script.h` だけでなく、対応する `.cpp` に `MARIONETTE_DEFINE_SCRIPT` が必要です。
- Script の生成登録は `*Script.h` だけを対象にします。ファイル名がこの規則から外れると自動登録されません。
- `Intermediate/Generated/ScriptRegistry.gen.cpp` は生成物です。手で編集せず、CMake configure / build で更新します。
- Scene 保存対象にしたい field には `Serialize` を付けます。
