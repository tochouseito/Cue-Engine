# ADR-0017: Authoring Scene, Runtime Instance, and Persistence Contract

- Status: Accepted
- Date: 2026-09-03
- Decision Owners: CueEngine Project

## Context

M11では、編集と保存の正本となるAuthoring Sceneを、M10で確立したRuntime Worldへ安全に実体化できる基盤を構築する。
Authoring ObjectとRuntime Entityを同じIdentityまたは同じ所有Objectとして扱うと、Entity Slot再利用、Play Session終了、
Editor Undo、Scene再読込のいずれかが別の寿命へ漏れ、保存Dataと実行状態の境界が崩れる。

ADR-0013はAuthoring SceneをSource Assetとし、PathだけをAsset Identityへ使用しない方針を決定した。ADR-0014は
Atomic File Replace、Publish前失敗時の元File維持、Publish後のDurability不明状態を決定した。ADR-0015は
永続`TypeId`／`FieldId`と未知Data保持方針を、
ADR-0016はRuntime `EntityHandle`がSession-localで永続化できないことを決定した。本ADRはこれらを接続し、
`SceneDocument`、将来の`EditorDocument`、不変Snapshot、`SceneInstance`、Runtime World、Scene Fileの責務を決定する。

M11ではPrefab、Scene Streaming、Play Mode変更反映、Scripting、Asset Database全体、Runtime Packageを決定しない。

## Legacy Reference

### Legacy Problem

旧CueEngineも、Scene内のObjectとComponentを編集、保存、再読込し、実行用Game Objectへ生成する必要があった。

### Legacy Approach

旧設計ではScene Asset、Scene Instance、Serializer、実行Objectの責務が近く、Path、Runtime Object、保存対象の関係が
呼び出し側の運用へ依存していた。Editor操作、保存Model、実行Instanceの寿命を一つのObject Graphとして扱う箇所もあった。

### Legacy Strengths

- Scene HierarchyとObject編集を直感的に扱えた
- Scene保存から実行Object生成までの経路が短かった
- Component単位のAuthoring DataをSceneへ保持できた

### Legacy Problems

- Authoring IdentityとRuntime Object Identityの境界が弱かった
- Scene Pathが参照とIdentityを兼ねやすく、移動とRenameに弱かった
- Runtime変更を保存Dataへ戻す条件が明確でなかった
- 未知Component、未知Field、Format Version、MigrationのLossless契約が不足していた
- 途中実体化失敗時のRollbackと所有権が明示されていなかった
- Editor Session状態がScene Sourceへ混入し得た

### Current Requirements

- Sceneの編集・保存正本をRuntime Worldから独立させる
- Scene、Object、Component InstanceをStable IDで識別する
- Runtime `EntityHandle`をSceneへ保存しない
- PathをScene Identityにしない
- 未知Componentと未知FieldをLosslessに保持する
- Versionごとの連続MigrationとAtomic Saveを提供する
- Runtime実体化を一方向かつ失敗時Rollback可能にする
- Editor固有状態をSceneDocumentの公開Modelへ含めない

### New Design

現在の要件から`Cue.Scene`を新しいFirst-party Moduleとして設計する。旧Source Code、Serializer、Object型、File Layoutは
コピー、移植、部分抽出しない。Legacyは解決していた問題と境界不足の確認だけに使用する。

### Validation

Stable ID、Hierarchy、未知Data Round-trip、Migration、Atomic Save失敗、破損入力、Runtime実体化Rollbackを
Unit TestとProcess Testで検証する。

## Reference Engine Comparison

| Engine | 参考にする点 | CueEngineでそのまま採用しない点 |
| --- | --- | --- |
| Unreal Engine | Authoring Assetと実行Worldを分離し、StableなAsset参照を使用する | UObject Identity、Package、Reflection ABIをCueEngineの共通基底へ移植しない |
| Unity | Scene AssetからPlay用Worldを生成し、編集状態とPlay状態を分離する | Instance IDやLibrary内部表現を永続Identityへ使用しない |
| Godot | Scene階層をAuthoring可能なDataとして保存し、Instanceを生成する | Node継承TreeをRuntime ECS Storageへ強制しない |

CueEngineはAuthoring Sourceと実行Instanceの分離、Hierarchyの編集可能性、Stable参照のUsabilityを取り入れる。
RuntimeはM10のECS契約を維持し、Authoring ObjectをVirtual Object Treeとして保持しない。

## Decision

### Module Boundary

`Cue.Scene`はPlatform非依存のAuthoring Scene Model、Snapshot、Serializer、Migration、Runtime実体化契約を所有する。
初期依存は`Cue.Foundation`、`Cue.Math`、`Cue.Schema`、`Cue.GameCore`、`Cue.IO`だけを許可する。

依存方向は次のとおりとする。

```text
Cue.Editor -> Cue.Scene -> Cue.GameCore
                    |\-> Cue.Schema
                    |\-> Cue.Math
                    |\-> Cue.IO
                    \--> Cue.Foundation
```

`Cue.Scene`は`Cue.Editor`、`Cue.Platform`、`Cue.RHI`、Rendererへ依存しない。`Cue.GameCore`は`Cue.Scene`へ
依存せず、Runtime World APIを汎用のまま維持する。

### Stable Identity

M11では相互変換できない三つのStrong Typeを導入する。

| Type | Scope | Persistence | Reuse |
| --- | --- | --- | --- |
| `SceneAssetId` | Project内のAuthoring Scene | Scene FileとAsset参照へ保存 | 同一Project内で再利用しない |
| `ObjectId` | 一つのSceneDocument内のObject | Scene Objectへ保存 | 削除後も同じSceneで自動再利用しない |
| `ComponentInstanceId` | 一つのSceneDocument内のComponent Instance | Component Entryへ保存 | 削除後も同じSceneで自動再利用しない |

三つのIDは128-bit UUID Version 4とし、JSONではlowercaseの`8-4-4-4-12`形式を使用する。nil、Version不正、
Variant不正を拒否する。生成は呼び出し側が一意所有するIdentity Sourceを注入して行い、Global可変Generatorを使用しない。

UUIDであることは一般Asset Databaseの全`AssetId`表現を決定しない。`SceneAssetId`はM11 Scene Source内のIdentityであり、
将来のAsset Databaseは明示的なMappingまたは互換ADRを通じて統合する。

Runtime `WorldIdentity`、`EntityHandle`、Component Dense Index、Pointer、Container Index、File OffsetをこれらのIDへ変換して
保存しない。Runtime Instanceは`ObjectId`から`EntityHandle`へのSession-local Mappingを保持できるが、そのMappingをSceneへ保存しない。

### SceneDocument

`SceneDocument`は編集・保存対象Dataの唯一の正本であり、次を所有する。

- `SceneAssetId`
- Stable `ObjectId`を持つObject集合
- Object名、Active状態、任意のParent `ObjectId`
- ADR-0011に従うCore Transform Data
- Stable `ComponentInstanceId`、`TypeId`、Typeごとの`SchemaVersion`を持つComponent Data
- `FieldId`で識別する既知Field Data
- 未知Component、未知Field、ExtensionのOpaque Data

Object格納順、Memory Address、Runtime Entity生成順を意味へ含めない。Serializerは決定的な順序を選べるが、
SceneDocument APIの正しさをContainer順へ依存させない。

`SceneDocument`は次を所有しない。

- File Path、Project絶対Path、Asset Database Locator
- Runtime `EntityHandle`、World、SceneInstance、Component Pointer
- Selection、Dirty、Undo／Redo、Open Tab、Viewport、Gizmo、Window状態
- Autosave Path、Recovery Path、Last Saved Timestamp
- Renderer、Physics、Audio、Scriptの実行状態

Document mutationは生成Threadで直列実行し、M11ではThread-safeにしない。公開操作は入力と全Invariantを検証してから
状態を変更し、失敗時はDocumentを変更しない。外部から直接Containerを書き換えるMutable参照を返さない。

### EditorDocument Separation

M12の`EditorDocument`は`SceneDocument`を編集対象として一意所有または明示的に参照するApplication層であり、
次のSession状態を所有する。

| EditorDocument | SceneDocument |
| --- | --- |
| File Locator、Open Tab | SceneAssetId |
| Dirty、Saved Revision | Object／Component Authoring Data |
| Selection、Expanded Tree | Stable Object／Component Identity |
| Command History、Undo／Redo | Command適用後の永続状態 |
| Autosave／Recovery Policy | 保存対象の正本Data |

EditorなしのToolとTestも`SceneDocument`を生成、検証、保存できる。`SceneDocument`公開APIへEditor型を出さない。

### Immutable Snapshot

Runtime実体化はMutable `SceneDocument`を直接参照せず、検証済みの不変`SceneSnapshot`を入力とする。
Snapshot生成時にStable ID、Hierarchy、Component Data、Resource Limitを再検証し、全DataをSnapshot自身が所有する。

Snapshotは生成後に変更されず、所有者が明示的に寿命を管理する。Runtime実体化中にEditorがDocumentを変更しても、
進行中の実体化結果はSnapshot作成時点のDataだけで決まる。生PointerまたはDocument Container IteratorをSnapshotへ保存しない。

### Runtime Instantiation

実体化は次の一方向Pipelineとする。

```text
SceneDocument -> SceneSnapshot -> Instantiation Plan -> RuntimeWorld + SceneInstance
```

`SceneInstantiator`はRuntime WorldのOwner ThreadかつSafe Pointでだけ実行する。まずSnapshot全体から生成Planを構築し、
TypeとField、Hierarchy、Resource上限、Runtime Component生成能力を検証する。Plan検証中はRuntime Worldを変更しない。

適用前に、生成と失敗時破棄に必要なStructural Mutation容量を検証する。適用時はObjectごとにRuntime Entityを生成し、Core Transform、
Hierarchy、既知Componentを構築する。途中で一件でも失敗した場合、その実体化Operationで生成した生存Entityを逆順で全て破棄する。
Slot Generation、Free List、`StructuralEpoch`を含むWorld内部状態が呼び出し前と同一になることは保証しない。他の既存Entityを変更せず、
失敗したScene Instanceに由来する生存Entityを残さないことを観測可能なRollback契約とする。

成功時に返す`SceneInstance`は次だけを保持する。

- 元`SceneAssetId`
- Instance-local Identity
- 実体化先Runtime `WorldId`
- `ObjectId`からRuntime `EntityHandle`へのMapping
- そのInstanceが生成したEntityの順序付き集合

`SceneInstance`は`SceneDocument`または`SceneSnapshot`への生Pointerを保持しない。Runtime WorldもDocument、Snapshot、
EditorDocumentへの参照を保持しない。Instance終了は呼び出し側が同じRuntime Worldを明示的に渡し、Owner ThreadのSafe Pointで
実行する。処理開始前に保存した`WorldId`と引数WorldのIdentityを照合し、不一致なら所有集合を変更せず拒否する。一致した場合だけ
所属Entityを逆順で処理する。各Handleを`World::is_alive`で確認し、Gameplayにより既に破棄されたEntityは解放済みとして飛ばす。
生存Entityの破棄が失敗しても後続を処理し、全失敗を順序付き結果として返す。失敗後も生存するEntityは`SceneInstance`の所有集合へ残し、
破棄済みEntityに対応する`ObjectId`／`EntityHandle` Mappingは同じ終了Stepで除去する。所有集合とMappingの更新を分離せず、
部分失敗後もMappingが現在の生存所有Entityだけを解決する状態を保つ。呼び出し側が再試行またはRuntime World終了を選べるようにする。
Runtime Worldより先にSceneInstanceを正常終了する所有順を
Composition Rootが保証する。

Runtime Type Builderが存在しない未知Componentまたは、解釈できない既知Component Fieldを含むSnapshotは実体化を失敗させる。
Authoring保存では未知Dataを保持できるが、意味不明なDataを黙ってRuntimeへ無視して部分実行しない。

### No Implicit Reverse Synchronization

Runtime変更を`SceneDocument`へ暗黙反映しない。Runtime Component、Entity生成／破棄、Transform変更、Script状態を
Documentへ自動書戻ししない。`ObjectId`／`EntityHandle` Mappingの存在は逆同期の許可を意味しない。

将来Play Mode変更をAuthoringへ適用する場合は、Runtime Snapshot間のDiffを検証し、Editor Commandとして明示的に適用する
別Research IssueとADRを必要とする。M11ではそのAPIを先回りして用意しない。

### Scene File Envelope

Scene SourceはUTF-8 JSONとして保存し、BOMを受理しない。初期File Extensionは`.cuescene`とする。ExtensionとPathは
探索と表示のLocatorでありIdentityではない。File内の`sceneAssetId`が永続Identityである。

Format Version 1のTop-level Envelopeは次のMemberを必須とする。

```json
{
    "formatVersion": 1,
    "sceneAssetId": "00000000-0000-4000-8000-000000000000",
    "objects": [],
    "extensions": {}
}
```

例のIDは形だけを示し、有効なScene生成に使用しない。Object、Transform、Component、Field ValueのVersion 1 Wire Schemaは
Issue #150と#151で本ADRのIdentityと未知Data契約に従って実装し、Issue #152でSerializerとして固定する。
同じ`formatVersion`の意味を実装後に変更しない。

各Component Entryは`TypeId`に加えて保存時のnon-zero `SchemaVersion`を必須Memberとして保持する。登録済みTypeのAuthoring Loadは
ADR-0015に従って過去Versionから現在のType Schemaへ連続Migrationできることを検証し、Step欠落を拒否する。未知`TypeId`または
現在より未来の`SchemaVersion`を持つComponentはMigrationせず、`SchemaVersion`を含むEntry全体をOpaque DataとしてLosslessに保持する。
そのComponentの編集とRuntime実体化は拒否するが、Authoring Loadと他の編集可能Dataの利用は許可する。
Top-level `formatVersion`をComponentの`SchemaVersion`として代用しない。

固定Schema Objectの未知Memberと重複Memberを拒否する。`extensions`と未知Component／Field用Opaque Payloadだけは
意味解釈せず所有し、再保存で失わない。Opaque JSONも構文、重複Member、Nesting、String、Container、File Size上限を適用する。

SerializerはObjectを`ObjectId`、Componentを`ComponentInstanceId`、Fieldを`FieldId`のcanonical文字列表現で昇順に出力する。
JSON ObjectのMember順は固定する。有限数だけを受理し、NaN、Infinity、locale依存表現、非決定的な浮動小数点整形を保存しない。

### Version and Migration

`formatVersion`はScene File形式の単調増加する正の32-bit unsigned integerであり、Project Descriptor Version、
Schema Version、CueEngine Versionとは別に扱う。未来の未対応Versionを推測して読まない。

Migrationは`N -> N + 1`の連続変換だけを登録し、途中Versionを飛ばさない。Parseした元DataをMemory上で段階変換し、
各段階と現行Documentを検証する。Migration失敗時は元Fileと既存SceneDocumentを変更しない。Downgrade保存は行わない。

Sceneを開いただけでSource Fileを暗黙更新しない。Migration済みDocumentは呼び出し側へ`MigrationRequired`状態と元Versionを返し、
明示Save時だけ現行Versionとして保存する。元FileのBackup／Recovery PolicyはIssue #152でADR-0014に従って実装する。

### Save and Load Ownership

Scene File LocatorはProject／Editor／Tool側が`Cue.IO`のRoot境界付き相対Pathとして渡す。`SceneDocument`自身はLocatorを保持しない。
LoadはFileを上限内で読み、完全にParse、Migration、検証した新Documentを構築してから成功値として返す。既存Documentへ
部分適用しない。

SaveはDocumentからMemory上にCandidate Byte列を生成し、同じParserでParse-backして同値性を確認してから
ADR-0014のAtomic File Replaceで公開する。結果は`Committed`、`NotPublished`、`PublishedButDurabilityUnknown`を区別してそのまま返す。
Publish前失敗の`NotPublished`では元Fileを維持してTemporary FileをCleanupする。Publish後の
`PublishedButDurabilityUnknown`では新Fileが既に可視化されているため元Fileへ戻さず、呼び出し側へ再読込と診断を要求する。
Cleanup失敗はPrimary Errorを置換せずSecondary診断へ追加する。

## Rejected Alternatives

### Runtime EntityHandleをObjectIdとして保存する

World、Slot、Generation、Session寿命に依存し、再読込後のAuthoring Identityを維持できないため採用しない。

### Scene PathをScene Identityとして使用する

Rename、移動、Project複製で参照が壊れ、ADR-0013のAsset Identity方針に反するため採用しない。

### SceneDocumentをEditorDocumentとして実装する

Selection、Undo、Window、LocatorがHeadless ToolとRuntime境界へ漏れ、RuntimeからEditorへの依存を作るため採用しない。

### RuntimeWorldがSceneDocumentを直接参照する

Mutable Authoring Dataの寿命とOwner ThreadがRuntime更新へ漏れ、Editor変更と実行結果が非決定的になるため採用しない。

### Runtime変更を自動的にSceneへ反映する

どの変更を保存するか、Undo履歴、Script一時状態、Entity削除の意味が曖昧になり、意図しないSource変更を起こすため採用しない。

### 未知ComponentをRuntimeで黙って無視する

Authoring上存在するGame LogicまたはDataの欠落を成功として実行するため採用しない。保存時のLossless保持とRuntime実行可能性を分ける。

## Consequences

### Positive

- Scene編集、保存、Runtime実行の正本と寿命が明確になる
- Scene Path変更とRuntime Entity再利用からStable Identityを分離できる
- EditorなしのToolとTestが同じSceneDocumentを使用できる
- 未知Dataを保持しつつ、Runtimeでは不完全実行を拒否できる
- SnapshotによりEditor変更とRuntime実体化を決定的に分離できる
- 途中実体化失敗を既存Worldへ残さずRollbackできる

### Negative

- Document、Snapshot、Instance間でData Copyまたは変換Costが発生する
- 三種類のStable IDとRuntime Mappingを管理する必要がある
- 未知Dataを保持するAuthoring ModelとRuntime Builder Registryが別に必要になる
- 暗黙逆同期がないため、Play変更適用には将来の明示Workflowが必要になる
- VersionごとのMigrationとParse-back検証を継続保守する必要がある

## Validation

- SceneAssetId、ObjectId、ComponentInstanceIdの形式、nil、重複、再読込後維持を検証する
- Dangling Parent、Self Parent、Hierarchy Cycle、Depth上限を拒否する
- ObjectとComponentの追加、削除、Rename、Reparent失敗時にDocumentが不変であることを検証する
- 未知Component、未知Field、ExtensionをLoad／SaveでLosslessに保持する
- 同じDocumentからByte単位で安定した出力を生成する
- Migrationの連続適用、欠落Step、未来Version、失敗時元File維持を検証する
- Parse-back失敗と`NotPublished`で元Fileと既存Documentを維持し、`PublishedButDurabilityUnknown`では新Fileの再読込を要求する
- Snapshot作成後のDocument変更がSnapshotへ影響しないことを検証する
- Runtime実体化成功時のObjectId／EntityHandle Mappingと、途中失敗時にOperation由来の生存Entityを残さないことを検証する
- SceneInstance終了で先に破棄されたEntityを飛ばし、他の生存Entityを処理し、破棄失敗後の所有集合を維持する
- Runtime変更がSceneDocumentへ暗黙反映されないことを検証する
- Public Header単体Compile、依存方向、Debug／Development／Release BuildとCTestを実行する

## Follow-up Work

- Issue #150: Stable ID付きSceneDocumentとHierarchy
- Issue #151: Schema駆動Component Dataと未知Data保持
- Issue #152: Version付きScene Serializer、Migration、Atomic Save
- Issue #153: SceneDocument SnapshotからRuntimeWorldへの実体化
- Issue #154: 破損Scene入力とResource上限
- Issue #155: M11 Completion Gate
- Asset Database Research: `SceneAssetId`とProject全体`AssetId`の統合
- Play Mode Apply Research: Runtime DiffからEditor Commandへの明示変換
