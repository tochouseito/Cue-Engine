---
sidebar_position: 3
title: Scene / Asset / 物理 / Navigation
---

# Scene / Asset / 物理 / Navigation

このページは Editor と Script から見える現在の Runtime 仕様をまとめます。

## Scene 管理

World はすべての GameObject を一元管理します。
Scene は GameObject の所属元として扱われ、ヒエラルキーでは Scene ごとの tree として表示されます。

- `startupScene`: Project を開いたときに最初に読み込む Scene です。
- 追加読み込み: `DebugView > シーン > 読込` から `Assets/Scenes` 配下の `.cuescene` を World に追加します。
- 保存: 各 GameObject の所属 Scene に従って、該当する Scene asset へ書き戻します。
- 名前: GameObject 名は World 全体で一意です。別 Scene でも同名は許可されません。

Script からは `Marionette::SceneManager` を使って Scene の読み込みと削除を予約できます。
読み込みと削除は遅延実行です。

## Scene asset

Scene asset は `.cuescene` です。
現在の serializer は次の Component を保存できます。

- `TransformComponent`
- `CameraComponent`
- `MeshFilterComponent`
- `NavAgentComponent`
- `NavMeshBakeSourceComponent`
- `StaticMeshRendererComponent`
- `SpriteRendererComponent`
- `AudioSourceComponent`
- `RigidBodyComponent`
- `ColliderComponent`
- `CharacterControllerComponent`
- `ScriptComponent`

`ScriptComponent` の Entity 参照は、Scene 保存時に localObjectId として保存されます。
Scene 読み込み時に Runtime EntityId へ解決されます。

## Asset Browser と import

Editor Window へ外部ファイルをドロップすると、Asset Browser の現在フォルダへコピーして import します。
現在フォルダが `Assets` 以下ではない場合は `Assets/` へコピーします。

対応形式は次の通りです。

| 外部ファイル | 生成される asset | 用途 |
| --- | --- | --- |
| `.png` | `.cuetexture` | Texture / Material |
| `.wav` | `.cuesound` | AudioSource |
| `.obj` | `.cuemodel` | MeshFilter / MeshCollider |

Asset Browser では `.png` と `.cuetexture` のプレビュー表示を試みます。
プレビューできない場合は Image アイコンを表示します。

## Material

Material asset は `.cuematerial` です。
現在の Material は次の値を持ちます。

- `color`: 描画色です。
- `texture`: 登録済み texture 名です。
- `useTexture`: texture を使うかどうかです。

Inspector で Material asset を選択すると、`color` と `Use Texture` を編集できます。
Asset Browser から `.cuetexture` を Material Inspector へドロップすると、Material の texture に設定されます。
Asset Browser から `.cuematerial` を RendererComponent の drop target へドロップすると、その Renderer に Material を設定できます。

## 物理 Component

物理は `RigidBodyComponent`、`ColliderComponent`、`CharacterControllerComponent` の組み合わせで使います。

### RigidBodyComponent

`RigidBodyComponent` は Physics backend の body を表します。

- `Static`: 動かない床や壁に使います。
- `Kinematic`: Transform を Engine 側から動かす body です。
- `Dynamic`: 物理シミュレーションで動く body です。

Script からは次の操作を使えます。

- `set_rigid_body_linear_velocity()`
- `get_rigid_body_linear_velocity()`
- `add_rigid_body_force()`
- `add_rigid_body_impulse()`

### ColliderComponent

`ColliderComponent` は shape を表します。
現在使える shape は `Box`、`Sphere`、`Capsule`、`Mesh` です。

`Mesh` は `.cuemodel` の geometry から static triangle mesh collider を作ります。
地形や床のような動かない形状に使います。

### CharacterControllerComponent

`CharacterControllerComponent` は kinematic body として水平移動、重力、接地判定、地面への snap を処理します。
Script からは `set_character_move_velocity()` で水平速度を渡し、`request_character_jump()` でジャンプを要求します。

CharacterController の移動は Transform を直接編集するより安定します。
Player の WASD 移動のように、床に沿って移動させたい Entity に使います。

## Navigation

Navigation は Scene に設定された NavMesh を使います。
Editor では `ナビゲーション > Scene NavMesh を Bake` で NavMesh asset を生成します。

### NavAgentComponent

`NavAgentComponent` は目的地または追跡対象への path を計算します。
Script からは次の操作を使えます。

- `set_nav_agent_destination()`: 固定座標へ移動します。
- `set_nav_agent_target()`: 対象 Entity の現在位置を追跡します。

### movementMode

`movementMode` は次の 2 種類です。

- `DirectTransform`: NavAgent が Transform を直接更新します。物理を使わない簡易移動向けです。
- `DesiredVelocityOnly`: NavAgent は `desiredVelocity` を出すだけです。CharacterController と組み合わせる物理移動向けです。

`DesiredVelocityOnly` では NavMesh の高さへ Transform を直接スナップしません。
NavAgentMotorSystem が `desiredVelocity` を CharacterController の `moveVelocity` へ渡します。

## Debug 表示

DebugView では grid 表示を切り替えられます。
Navigation Debug Window では NavMesh と Navigation の状態を確認できます。

カメラ、ライトなど通常描画されない Editor object も screen picker の対象になります。
選択 outline は選択 object の形を depth に依存せず前面へ表示する方針です。
