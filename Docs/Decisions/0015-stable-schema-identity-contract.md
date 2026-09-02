# ADR-0015: Stable Schema Identity Contract

- Status: Accepted
- Date: 2026-09-02
- Decision Owners: CueEngine Project

## Context

M10では、Scene、Editor、Runtime World、将来のScriptingが同じ型とFieldを参照できるSchema基盤を確立する。
Compiler型名、RTTI、登録順、Memory Address、現在のComponent LayoutをIdentityへ使用すると、Build、Module読込順、
Refactor、Hot Reloadによって永続参照の意味が変わる。逆に、永続IdentityをRuntimeのArray Indexとして直接使用すると、
検索とStorageへ不要なCostを持ち込み、高性能なECS内部表現までReflection Objectへ固定する。

本ADRはStable `TypeId`、`FieldId`、`SchemaVersion`、Runtime Dense Index、最小Metadata、未知Schemaの扱いを
決定する。SceneのWire Format、Serializer、Inspector、Script ABI、Component Storage Layoutは決定しない。

## Legacy Reference

### Legacy Problem

旧CueEngineも、Component Typeを識別してEntityへ追加し、System Queryから対応するComponent Storageを選ぶ必要があった。

### Legacy Approach

旧実装はProcess内で割り当てたComponent IDとC++ Template Typeを中心にECS Typeを識別した。Runtime操作には簡潔だったが、
永続Scene、Editor Property、Migration、Hot ReloadをまたぐStable Schema Identityは独立した契約になっていなかった。

### Legacy Strengths

- Runtime内のComponent検索へ小さい整数を使用できた
- C++ TypeからComponent操作へ到達しやすかった
- SceneやEditorの永続契約をECS Storageへ持ち込まずに済んだ

### Legacy Problems

- 登録順またはProcess状態に由来するIDを永続参照へ使用できない
- Field単位のStable IdentityとVersion Migration契約がなかった
- C++ Memory Layoutと保存Dataの境界が明示されていなかった
- Hot Reload前後のType Identityと現在Layout Generationを区別できなかった

旧Sourceは問題と制約の確認だけに使用し、新実装へコピー、移植、部分抽出しない。

## Decision

### Identity Domains

Schema Identityを次の独立した値へ分ける。

| Identity | Scope | Stable across process | Persistent | Purpose |
| --- | --- | --- | --- | --- |
| `TypeId` | CueEngine ecosystem全体 | Yes | Yes | Typeの意味を識別する |
| `FieldId` | 一つの`TypeId`内 | Yes | Yes | Type内のFieldの意味を識別する |
| `SchemaVersion` | 一つの`TypeId` | Yes | Yes | そのTypeの永続Schema世代を表す |
| `DenseTypeIndex` | Seal済みRegistry Instance | No | No | Runtime Array、Bit Set、Storage検索に使用する |
| `LayoutGeneration` | ModuleまたはRuntime Session | No | No | Hot Reload後の現在Memory Layoutを識別する |

これらを相互に代用しない。特に`DenseTypeIndex`、C++ Type名、RTTI、登録順、Pointer、Memory OffsetをFileへ保存しない。

### TypeId

`TypeId`は128-bit UUID Version 4の値型とする。新しい永続Typeを定義するAuthoring操作またはFirst-party Toolが一度生成し、
Source上の明示Metadataとして保持する。C++ Symbol名、Namespace、File Path、表示名をHashして自動生成しない。

永続Text表現はlowercaseの8-4-4-4-12形式とし、nil UUID、Version 4以外、RFC 4122 variant以外、非Canonical表現を拒否する。
Binary表現を導入する場合も同じ16 byte値をNetwork Byte Orderで表し、別のIdentityを再生成しない。

Typeの改名、Source移動、Module分割、C++実装差替え、Hot Reloadでは`TypeId`を維持する。意味的に互換でない新Typeは新しい
`TypeId`を発行する。削除した`TypeId`はTombstoneとして予約し、別の意味へ再利用しない。

Version Control対象のFirst-party Schema Registration Sourceを、Active `TypeDescriptor`と削除済み`TypeId` Tombstoneの正本とする。
これは独立したData FileまたはWire Formatではなく、各First-party ModuleがC++ Sourceの明示的な登録関数からLiteral ID、Descriptor、
TombstoneをRegistry Builderへ渡すCode上の台帳である。したがってManifest Format VersionやParserをM10へ導入しない。

Type削除時はActive登録を削除し、同じModuleのRegistration Sourceへ同じIDのTombstone登録を残してVersion Control履歴から値を
消さない。新しいTypeを発行するAuthoring Toolは、全ModuleのActive SetとTombstone SetをRegistry検証経路へ通す。
Registry構築時も両方を入力し、同じ`TypeId`がActiveとTombstoneに現れる場合、またはTombstoneが重複する場合は失敗する。
Standalone Schema Manifest Fileを導入する場合は、導入前に別Research IssueとADRでFile名、Format Version、Migration、Atomic Storage、
Module Merge規則を決定する。未決定のData FileをSchema Identityの正本として使用しない。

### FieldId

`FieldId`は一つの`TypeId`内で一意な、明示指定のnon-zero 32-bit unsigned integerとする。永続Text FormatがDecimalを
使用する場合は1から4294967295までのASCII decimal digitだけをCanonical表現とし、先頭Zero、符号、小数、指数表記を拒否する。

Field名、宣言順、Memory Offset、Compiler Layoutから`FieldId`を自動生成しない。Fieldの改名またはC++ Memberの移動ではIDを
維持する。Fieldを削除した場合、そのIDをTypeのReserved Setへ残し、別の意味へ再利用しない。意味または値Domainが互換でないFieldは
新しいIDを割り当て、Migrationで旧Fieldから変換する。

32-bit幅はType内Scopeで十分な空間を持ち、Scene内でFieldごとに128-bit値を繰り返すCostを避けられる。ID衝突をHash確率へ
委ねず、Authoring時とRegistry登録時の一意性検査で必ず拒否する。

### SchemaVersion and Migration

`SchemaVersion`はTypeごとの連続したnon-zero 32-bit unsigned integerとする。初期Versionは1とし、次のSchema変更では必ず
現在値へ1を加える。Versionを飛ばさない。次の変更ではVersionを増やす。

- Fieldの追加、削除、意味変更、値Domain変更
- Fieldの必須性またはDefault規則の変更
- Nested TypeまたはContainer意味の変更
- 同じ保存値を別のRuntime意味として解釈する変更

表示名、説明Comment、Editor Categoryなど、保存値の意味を変えない診断Metadataだけの変更では増やさなくてよい。
Runtime Memory Layoutだけの変更は`SchemaVersion`ではなく`LayoutGeneration`で扱う。

Migrationは`TypeId`ごとに`N -> N + 1`の明示変換として所有し、途中Versionを飛ばさない。読込み時に現在Versionへ暗黙解釈せず、
変換列が欠ける場合は`MigrationRequired`または`UnsupportedSchemaVersion`として失敗する。Migrationは元Dataを変更せずMemory上で
段階変換し、全段成功後にだけ上位のAtomic Storage契約で保存する。M10ではMigration RegistryとSerializerを実装しない。

### Runtime Dense Index

`DenseTypeIndex`はSchema RegistryをSealしたときに割り当てるProcess-localなnon-zero 32-bit値とする。0はInvalidに予約する。
同じDescriptor集合から再現可能な診断を得るため、Registryは`TypeId`の16 byte unsigned lexicographical orderで並べてから
1始まりのIndexを割り当てる。Module登録順は結果へ影響しない。

Seal後はDescriptor追加、削除、置換を拒否する。Hot ReloadまたはModule集合変更時は新しいRegistry Generationを構築してSealし、
古い`DenseTypeIndex`を新Registryへ持ち越さない。ECS StorageとQueryは同じSeal済みRegistryのLifetime内だけでIndexを使用する。

### Minimum Metadata

M10のSchema Registryが所有する最小Metadataを次に限定する。

`TypeDescriptor`:

- `TypeId`
- 診断用Canonical Name
- 現在の`SchemaVersion`
- `FieldDescriptor`の集合
- 再利用禁止となったReserved `FieldId`の集合

Registry全体:

- 削除済み`TypeId` Tombstoneの集合

`FieldDescriptor`:

- `FieldId`
- 診断用Name

Canonical Nameは有効なUTF-8で1 byte以上255 byte以下、Field Nameは1 byte以上128 byte以下とし、Unicode Control Characterを
拒否する。名前は診断とTool表示用でありIdentityではない。同じRegistry内でCanonical Nameの重複を拒否するが、改名で
`TypeId`または`FieldId`を変更しない。

M10のMetadataへ次を含めない。

- Component Object、共通Reflection基底Class、Virtual Function Table
- C++ `type_info`、Compiler固有型名、Template生成順
- Object Address、Constructor Function Pointer、Allocator
- `sizeof`、`alignof`、Field Offset、Padding、Endian依存Memory Image
- Serializer Function、Script ABI、Editor Widget、Platform Native型

値Kind、Container、Constraint、Attribute、Constructor Adapterは利用側の具体的要件を伴うFollow-upで追加する。Identity Registryへ
Runtime StorageやObject Lifetimeを所有させない。

### Registration and Collision Detection

Mutable RegistryはSeal前にDescriptorを登録し、次を検出した時点で構築を失敗させる。

- nil、Version／Variant不正、非Canonicalな`TypeId`
- 同じ`TypeId`の重複登録
- Active `TypeId`とTombstoneの重複、またはTombstone同士の重複
- Canonical Nameの重複
- `SchemaVersion`が0
- `FieldId`が0、Type内で重複、またはReserved Setと重複
- Field Nameの重複
- Invalid UTF-8、Control Character、長さ上限超過

重複Descriptorを同値なら成功とするIdempotent規則は採用しない。複数Moduleによる所有権競合を隠すためである。Errorには衝突した
Identity、登録元の診断名、規則を含めるが、Registryを部分的にSealしない。失敗後のMutable Registryは破棄して再構築する。

### Unknown Type and Field Policy

未知Dataを一律に破棄またはDefault Componentへ変換しない。利用境界ごとに扱いを分ける。

| Boundary | Unknown Type | Unknown Field |
| --- | --- | --- |
| Authoring read／round-trip | ContainerがRaw表現を保持できる場合はOpaque Dataとして保持し、編集とRuntime生成を禁止する | 所有TypeとともにOpaque Dataとして保持する |
| Migration | 対応TypeまたはFieldの変換が登録されるまで失敗する | 対応Migrationがない場合は失敗する |
| Runtime Package | 必須DataならLoadを失敗し、EntityまたはComponentを部分生成しない | Current Schemaで意味を解釈できなければLoadを失敗する |
| Schema Registry | 未登録Identityの検索は`NotFound`を返す | 未登録Fieldの検索は`NotFound`を返す |

Opaque保持は将来のSerializer／Authoring Containerが実装する能力であり、M10 RegistryがRaw Scene Dataを所有することを意味しない。
理解できないDataを読込み成功として破棄してから再保存することを禁止する。

### ABI, Persistence, and Hot Reload Separation

`TypeId`と`FieldId`は意味Identityであり、ABI互換性を保証しない。Module ABIは別VersionとBoundaryを持ち、C++例外、STL所有権、
生Pointer所有権をPlugin境界へ公開しない。Scene FormatはStable IDとSchema Versionを参照するが、Component Memory Imageを保存しない。

Hot Reloadは同じ`TypeId`に対して新しい`LayoutGeneration`とRuntime Adapterを登録できるが、旧LayoutのObjectを新Layoutとして
reinterpretしない。State変換はVersion付きSchema Dataを経由するか、専用Migration契約を使用する。ABI、Scene Format、Hot Reload
Stateを同一Layoutまたは同一Version番号へ統合しない。

## Rejected Alternatives

### C++ Type NameまたはRTTIをTypeIdにする

Compiler、Build Option、Namespace Refactor、Module境界で安定せず、永続参照に使用できないため採用しない。

### Type名またはField名のHashをStable IDにする

改名でIdentityが変わり、Collisionを確実に防げない。Collision解決で順序依存も導入するため採用しない。

### UUIDをそのままRuntime Array Indexにする

Sparseで大きいIdentityをHot Pathへ持ち込み、Bit SetとDense StorageのCostを増やすため採用しない。

### Component Memory Imageを保存する

Padding、Alignment、Endian、Pointer、ABI、Compiler Layoutへ依存し、安全なMigrationと未知Field保持ができないため採用しない。

### 全ComponentをReflection Objectから継承させる

Virtual Dispatch、Layout制約、所有権の結合を全Runtime Componentへ強制し、Data-oriented Storageを妨げるため採用しない。

### 未知Fieldを無視して再保存する

新しいToolで作成したDataを古いToolが不可逆に消失させるため採用しない。

## Consequences

### Positive

- Source Refactor、登録順、Process再起動をまたいでTypeとFieldの意味を維持できる
- Runtimeは小さいDense IndexをArray、Bit Set、Component Storageへ使用できる
- ECS Componentへ共通基底Classまたは永続Memory Layoutを強制しない
- Scene、Editor、Scripting、Hot Reloadが同じStable Identityを共有しながらLayout契約を分離できる
- 重複Identityと再利用をToolおよびRegistryで診断できる

### Negative

- TypeとFieldのAuthorはIDを明示管理し、削除IDもReserved Setへ残す必要がある
- Registry Seal後のType追加には新しいRegistry Generationと利用側再構築が必要になる
- 未知Dataの安全なRound-tripはSerializer側にOpaque Storageを要求する
- Type改名後の診断名AliasとMigration Toolingは別途必要になる

## Validation

- 同じDescriptor集合を異なる登録順でSealし、同じ`DenseTypeIndex` Mappingになることを検証する
- 重複`TypeId`、Canonical Name、`FieldId`、Field Name、Reserved ID再利用を拒否する
- nil、Version／Variant不正UUID、0のVersion／Field ID、UTF-8と長さ境界を拒否する
- Registry GenerationをまたいだDense Indexの再利用をAPI形状とTestで防ぐ
- Schema RegistryのPublic Header単体Compileと依存方向を検証する
- Component TypeがReflection基底Classを持たず登録できることを検証する
- Scene Serializer導入時に未知Type／FieldのRound-tripとRuntime拒否をProcess Testする
- Debug／Development／Release Build、CTest、`git diff --check`を実行する

## Follow-up Work

- Issue #143: ECS Storage、Entity Lifetime、Structural Mutation契約
- Issue #144: Stable Schema RegistryとRuntime Dense Index
- Issue #145: Entity／Component Storage
- Issue #146: Query／Structural Command Buffer
- Issue #147: Headless Runtime World
- Scene Research: Version付きScene Format、Opaque Unknown Data、Migration Registry
- Hot Reload Research: Layout Generation、State Migration、Module ABI
