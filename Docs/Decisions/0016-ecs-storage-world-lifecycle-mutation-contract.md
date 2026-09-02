# ADR-0016: ECS Storage, World Lifecycle, and Mutation Contract

- Status: Accepted
- Date: 2026-09-02
- Decision Owners: CueEngine Project

## Context

M10では、SceneやRendererへ接続する前に、世代付きEntity、Component Storage、Query、Structural Mutation、Headless Worldの
最小基盤を確立する。Storage方式だけを先に選ぶと、古いHandle、Component寿命、Query中の再配置、Thread所有、失敗途中の状態が
Call Siteごとに異なる。逆に、将来のParallel SchedulerやScene Formatまで同時に固定すると、現在の検証範囲を超える。

ADR-0015はStable Schema IdentityとRuntime Dense Indexを分離した。本ADRは`Cue.GameCore`が所有するRuntime EntityとComponentの
Storage、Lifetime、Mutation契約を決定する。Scene／Prefab／Asset参照、Parallel ECS、Renderer／Physics統合は決定しない。

## Legacy Reference and Baseline

### Legacy Problem

旧CueEngineも、大量のEntityへComponentを追加、取得、削除し、複数Componentを要求するSystemを走査し、走査中の変更を遅延する
必要があった。

### Legacy Approach

旧実装はProcess内Component ID、Entityごとの固定Component集合、Virtual Component／System境界、遅延Component削除を使用した。
Runtime内の基本操作は可能だったが、Stable Schema、世代付きHandle、任意Component数、Query Lifetime、World Owner Threadは
一つの契約として分離されていなかった。

### Legacy Strengths

- Entity、Component、Systemの基本操作を提供した
- Query走査中のComponent削除を遅延できた
- 小さいRuntime IDでComponentへ到達できた

### Legacy Problems

- Entity Slot再利用後に古いHandleを確実に拒否する世代契約が弱かった
- 固定幅のComponent集合がType数上限とMemory Costを結合した
- Virtual基底とRTTI由来IDがData-oriented Storageと永続Schemaを結合しやすかった
- Query中に許可されるMutationと参照の有効期間が公開契約になっていなかった
- Worldを操作できるThreadとShutdown時の破棄順が明確でなかった

Issue #182で旧ECSを隔離した`origin/release`から直接Compileし、同一WorkloadのBaselineを保存した。100,000 EntityのDevelopment測定では、
`component_get_sequential`中央値1.3107 ms、`query_two_components`中央値7.5899 ms、`component_add`中央値66.3325 msだった。
これは現在Machine上の比較基準であり、新設計の性能保証ではない。新ECSはSafety契約の追加Costを含め同じ条件で再測定する。

旧Sourceは問題、制約、Baselineの確認だけに使用し、新実装へコピー、移植、部分抽出しない。

## Reference Engine Comparison

| Engine | Relevant approach | Strength | Adoption cost for CueEngine |
| --- | --- | --- | --- |
| Unreal Engine Mass | 同じFragment構成のEntityをArchetypeとChunkへまとめ、Processor Queryと`MassCommandBuffer`で処理中の構成変更を遅延する | 同一構成のBatch走査とChunk Localityが高く、World単位のManagerと遅延変更境界を持つ | `UScriptStruct`、Mass Fragment基底、Archetype移動、Chunk管理までM10へ導入するとSchema分離と初期検証範囲を超える |
| Unity Entities | Component構成ごとのArchetype Chunkへ格納し、Add／Remove時は別Chunkへ移動する。Structural ChangeはMain ThreadまたはEntity Command Bufferで適用する | Query対象Archetypeを絞り、JobとCommand Bufferを組み合わせて大規模処理へ拡張できる | Job Safety、Sync Point、Chunk移動、Native Container規則を一体導入する必要があり、単一Thread基盤の初期Complexityが大きい |
| Godot | NodeをScene Treeへ所有し、Scene InstanceとNode Path／参照でGame Objectを構成する | Editor上の階層、Lifetime、Scene Instancingが直感的でAuthoringとの対応が分かりやすい | Object／Tree中心の構造をHot Component Storageへ流用するとVirtual ObjectとHierarchy Costを全Entityへ強制する |
| CueEngine M10 | Stable Schemaと分離した世代付きHandle、Per-component Sparse Set、Callback Query、Owner Thread Safe Pointを使用する | 小さいFirst-party実装でLifetimeとMutation Safetyを先に検証し、Legacyとの同一Workload比較ができる | Multi-component QueryのSparse照合が増え、ParallelismとArchetype Localityは後続Researchになる |

Sources:

- [Unreal Engine MassEntity Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-mass-entity-in-unreal-engine)
- [Unity Entities Archetypes](https://docs.unity.cn/Packages/com.unity.entities%401.0/manual/concepts-archetypes.html)
- [Unity Entities Structural Changes](https://docs.unity.cn/Packages/com.unity.entities%401.0/manual/concepts-structural-changes.html)
- [Godot Nodes and Scene Instances](https://docs.godotengine.org/en/stable/tutorials/scripting/nodes_and_scene_instances.html)

必須の比較観点を次のように評価する。

| Viewpoint | Unreal Mass | Unity Entities | Godot Nodes／Scenes | CueEngine M10 decision |
| --- | --- | --- | --- | --- |
| Usability | Fragment／Processor／Queryの専用概念を使う | Entity、Component、System、Job、ECBを使う | Node TreeとScene InstanceをEditorで直接扱う | 単一Worldと型付きComponent APIへ絞る |
| Runtime Performance | Archetype Chunk Batchを優先する | Archetype ChunkとJobを優先する | ObjectとTree traversalを中心にする | Sparse Setの連続単一Type走査を優先し、結果を測定する |
| Iteration Speed | Mass ConfigとEditor Toolへ統合する | Baking、World、Profilerへ統合する | Scene Tree編集と即時実行が近い | Headless Testを先に作り、Editor統合はScene Milestoneへ分ける |
| Extensibility | Fragment、Tag、Processor、Traitを追加する | Component、System、Baking、Jobを追加する | Node／Resource／Scriptを継承・合成する | 基底ClassなしのComponentとStable TypeIdを追加する |
| Portability | Unreal RuntimeとReflectionへ依存する | Unity Runtime、Burst、Native Containerへ依存する | Godot Object ModelとScene Runtimeへ依存する | Standard C++とFirst-party Moduleだけを使用する |
| Data Safety | Processing ScopeとCommand Bufferを持つ | Safety Handle、Sync Point、ECBを持つ | Tree ownershipとObject lifetime規則を持つ | Generation、World Incarnation、Query Guard、Safe Pointを明示する |
| Compatibility | `UScriptStruct`とMass APIが境界になる | Entities Package VersionとGenerated Codeが境界になる | Node ClassとScene Formatが境界になる | Schema Identity、Runtime Layout、Scene Formatを分離する |
| Diagnostics | Mass DebuggerとProcessor／Archetype情報を使う | Systems／Archetypes WindowとProfilerを使う | Remote Scene TreeとDebuggerを使う | 分類済みError、Command Report、Headless Testを最初の診断にする |
| Testability | Unreal Test／World Harnessが必要になる | Unity World／Entities Test環境が必要になる | SceneTree Test環境が必要になる | Platform／Graphics不要のWorld Testを提供する |
| Complexity | Archetype、Chunk、Traits、Observerを含む | Archetype、Job、Sync、Bakingを含む | Object、Hierarchy、Signal、Sceneを含む | ParallelismとAuthoringを除外し、StorageとLifetimeへ限定する |

比較の結果、Unreal／UnityのSafe PointとCommand Buffer、Godotの明確なWorld／Tree所有という問題分離は参考にする。一方、M10では
Archetype Chunk、Job System、Reflection Object、Scene Treeを導入せず、Legacy比較とSafety Gateを満たす最小Storageを選ぶ。

## Storage Comparison

| Candidate | Strength | Cost | M10 Decision |
| --- | --- | --- | --- |
| Per-component packed sparse set | Add／Removeが局所的、単一Component走査が連続、実装と寿命検証を分離しやすい | 複数Component Queryは最小集合から他Storageを照合する | Adopt |
| Archetype chunk | 同じComponent集合の複数Field走査が連続し、Query計画を最適化しやすい | Add／RemoveでArchetype間移動、Chunk寿命、Alignment、Exception Safetyが同時に必要 | M10では採用しない |
| EntityごとのComponent Map | 実装が直感的でEntity単位操作が容易 | Allocation、Pointer追跡、Query Locality、Memory Overheadが大きい | Reject |
| 固定幅Bit Set＋Storage Array | Query Filterが小さく高速 | Type上限と全Entity Memoryを固定し、動的Module追加とSchema分離に弱い | Reject |

M10はPer-component packed sparse setを採用する。ArchetypeはBenchmarkと実利用Profileで必要性が確認された場合に別Research Issueで
比較する。Public Entity／Query APIはStorage方式を露出せず、将来の内部置換を可能にする。

## Decision

### Module Boundary

`Cue.GameCore`は`Cue.Foundation`と`Cue.Schema`だけへ公開依存する。Platform、RHI、RuntimeHost、Editor、Scene、Assetへ依存しない。
Componentは共通基底Class、Virtual Function、RTTI登録、Editor型を要求しない。

`Cue.Schema`はIdentityとImmutable Descriptorを所有し、`Cue.GameCore`は同じSeal済みRegistryに対応するComponent Runtime Adapterと
Storageを所有する。Stable `TypeId`をStorage Array Indexへ直接使用せず、Registryの`DenseTypeIndex`をWorld Lifetime内で使用する。

### World Identity and Entity Handle

公開`EntityHandle`は次の値から構成する。

| Part | Width | Rule |
| --- | ---: | --- |
| `WorldId` | 64-bit unsigned | Process共有`WorldIdentitySource`が供給するnon-zeroのIncarnation値 |
| `EntityIndex` | 32-bit unsigned | Slot Table Index、`UINT32_MAX`はInvalidに予約 |
| `Generation` | 32-bit unsigned | 1から開始、0はInvalidに予約 |

`WorldId`はScene Object Identityまたは永続Entity IDではなく、別WorldへHandleを渡した誤りを検出するRuntime Incarnation値である。
Composition Rootが一意所有する`WorldIdentitySource`のMonotonic Atomic Counterから発行し、Process終了まで値を再利用しない。
0をInvalidに予約し、`UINT64_MAX`を発行した後はWrapせず、以後のWorld作成を`CapacityExceeded`として拒否する。

`WorldIdentitySource`はWorld Pointer、Registry、Session、Callbackを保持せず、ID発行だけを行う明示所有のProcess Stateとする。
Reset、値指定、Service Lookup、任意Object登録APIを提供せず、無制限なGlobal World Singletonにしない。RuntimeHost、Editor Play Session、
Testを含む全World作成経路は同じSourceを共有し、Session再作成後も古い値を再発行しない。WorldのPublic Constructorから任意IDを
注入させない。明示Sourceにより、`Cue.GameCore`を複数DLLへStatic Linkした場合もLibrary内部Counterの複製を避ける。
`EntityHandle`をScene、Project、Asset、Save Dataへ保存しない。

WorldはSlot Tableを所有する。CreateはFree List末尾からIndexを再利用し、なければ新しいSlotを追加する。Destroy成功時は全Componentを
破棄し、SlotをDeadにしてGenerationを1増やしてからFree Listへ追加する。同じIndexを再利用してもGenerationが異なるため、古いHandleを
拒否する。Generationが`UINT32_MAX`のSlotをDestroyした場合は0へWrapせず永久Retireし、再利用しない。Index空間を使い切った場合は
`CapacityExceeded`を返し、既存Worldを変更しない。

全Entity APIはWorldId一致、Index範囲、Alive Flag、Generation一致を検証する。不一致は`InvalidEntity`であり、別Entityへ暗黙転送しない。
Destroy済みEntityへのDestroy、Add、Remove、Get、Command適用も同じ規則で拒否する。

### Component Storage and Lifetime

各Component Typeは一つのTyped Packed Sparse Setを持つ。

- Dense Entity Index Array
- Dense Component Array
- Entity IndexからDense位置＋1を引くSparse Array
- `DenseTypeIndex`に対応するType-erased Storage Owner

0のSparse値はComponent不存在を表す。AddはDense末尾へEntity IndexとComponentを追加し、Sparse位置を更新する。Removeは対象を破棄し、
末尾要素をSwap-moveしてDense穴を埋め、移動EntityのSparse位置を更新する。Iteration順はStorage変更で変わり得るため、Gameplayの
決定順として公開しない。順序保証が必要な処理は明示Sort Keyを別Dataとして持つ。

Component Typeは`nothrow_destructible`かつ`nothrow_move_constructible`を必須とする。Addで使用するConstructorも`noexcept`を必須とし、
Component由来の例外をWorld境界へ出さない。Copy可能性、Virtual基底、Standard Layout、Trivially Copyableは要求しない。
AlignmentはC++ Object規則に従ってTyped Storageが保証し、Raw Byte Bufferの手動Alignmentへ委ねない。

Worldは全Component Objectを一意所有する。Add成功後からRemove、Entity Destroy、World Shutdownのいずれかまで所有し、各Objectを
正確に一度破棄する。Duplicate AddとMissing Removeは分類済みErrorを返し、既存Componentを変更しない。ReplaceはM10公開APIに含めず、
利用側がMutable参照から値を更新するか、明示Remove後にAddする。

Allocationを伴う操作は`std::bad_alloc`を通常Errorへ変換せず、ADR-0005に従い注入済み`EmergencyHandler`へ直接移る。予期しない例外も
Module境界を越えずFatal経路へ変換する。RecoverableなValidation失敗はMutation前に検査し、失敗時にWorldを変更しない。

### Component Reference Lifetime

`try_get`が返すComponent Pointer／Referenceは、次のいずれかまでだけ有効とする。

- 同じComponent Storageに対する次のStructural Mutation
- 対象EntityのDestroy
- World Shutdown

別StorageのMutationでも将来のStorage実装を固定しないため、長期保持を推奨しない。Pointerを保存、別Threadへ渡す、次Frameへ持ち越す、
Scene参照として使用することを禁止する。安定参照が必要な利用側は`EntityHandle`と`TypeId`を保持し、使用時に再解決する。

### Query Lifetime and Access

M10 QueryはCallback-scopedとし、Query View Objectを呼出し外へ返さない。Worldは要求されたComponent Storageのうち最小Dense集合を走査し、
他StorageのSparse照合で一致Entityを選ぶ。Query開始時にActive Query Guardを取得し、Callback完了またはStack unwinding時に解放する。

Query Callbackへ渡されたComponent参照はそのCallback呼出し中だけ有効である。非StructuralなComponent値更新はMutable Queryで許可する。
同じWorldでのNested Queryは参照AliasとMutation規則を曖昧にするProgrammer ErrorとしてAssertする。Query要求に重複Type、未登録Type、Storage未生成Typeがある場合は
走査前に診断する。Storage未生成Typeは正常な0件結果、重複または未登録Typeは`InvalidQuery`とする。

Callbackが予期しない例外を投げた場合、Guardを解放してからADR-0005のFatal経路へ移り、通常実行へ復帰しない。Callbackから
Component参照、World内部Pointer、Dense Indexを永続所有させない。

### Structural Mutation Safe Point

Entity Create／Destroy、Component Add／Remove、Storage作成はStructural Mutationである。Active Query中に即時Structural APIを呼ぶことは
Safe Point前提への違反であり、Programmer ErrorとしてAssertする。Query Callbackから必要な変更はCommand Bufferへ記録する。

Query中に構造変更が必要な場合は`StructuralCommandBuffer`へCommandを記録する。BufferはWorld Owner Threadだけが使用し、登録順を
保持する。Safe PointはActive Queryがなく、WorldがShutdown中でない時点である。Issue #146は次の適用規則を実装する。

M10の公開APIは、単一または2 Componentの`query_read`／`query_write`をCallback-scoped Queryとして提供する。
`StructuralCommandBuffer`は既存`EntityHandle`またはBuffer-local `PendingEntityId`を対象にCreate／Destroy／Add／Removeを記録し、
`World::flush_commands`が全結果を`StructuralCommandReport`へFIFO順で返す。3 Component以上のQuery Builder、動的Query、並列Queryは
実測と利用要件が揃うまで後続Researchとする。

- CommandはFIFOで一件ずつ適用する
- 一つのCommandは成功して全変更を公開するか、失敗してそのCommandの変更を公開しない
- 失敗しても後続Commandを評価し、全Commandの結果を順序付きReportで返す
- 後続Commandは先行する成功Commandの結果を観測する
- Stale Handle、Duplicate Add、Missing Removeは該当Commandだけを拒否する
- Flush終了時に成功／失敗を問わず全Commandを消費し、暗黙Retryしない
- Apply中に新しいCommandを同じBufferへ追加しない

Deferred CreateはBuffer-localなnon-zero `PendingEntityId`を返す。同じBuffer内の後続CommandだけがこのIDを参照でき、Flush時に
実Entityへ解決する。Create失敗時、そのPending Entityを参照する後続Commandは`DependencyFailed`になる。Pending IDをBuffer外、
次Flush、Scene Dataへ保存しない。成功したCreateの`EntityHandle`は順序付きReportから取得する。

WorldはStructural Mutation成功ごとに64-bit `StructuralEpoch`を1増やす。0はInvalidに予約し、最大値へ到達したWorldはそれ以上の
Structural Mutationを`CapacityExceeded`として拒否する。Query GuardとComponent参照の診断はEpochを利用できるが、Releaseの正しさを
Assertだけへ依存させない。

### World Owner Thread and Public API Contract

Worldは作成時ThreadをOwner Threadとして記録し、次のAPIをOwner Thread限定とする。

| API family | Owner | Lifetime／failure contract |
| --- | --- | --- |
| Create／Destroy Entity | World | Safe Pointのみ、失敗時は既存World不変 |
| Add／Remove Component | World | Safe Pointのみ、Componentを成功後から一意所有 |
| Get／Has Component | World | Owner Threadのみ、返した参照はStorage Mutationまで有効 |
| Query | World | Owner Threadのみ、Callback中だけ参照有効、Nested Query拒否 |
| Record／Flush Commands | Command Buffer／World | Owner Threadのみ、FIFO Reportを返す |
| Schema lookup | Sealed Schema Registry | Registry Lifetime中は複数Threadから同時読取り可能 |
| Shutdown | World | Owner Threadのみ、Query中は拒否、全Componentを一度破棄 |

M10 WorldはThread-safeではない。他Threadからの呼出しはADR-0005の所有権／Thread規則違反に該当するProgrammer ErrorとしてAssertする。
`WrongThread`をRecoverable `Result`として通常分岐へ返さない。Thread-safe Queue、Parallel Query、
Job Scheduler、Work Stealingを先回りして導入しない。将来のSchedulerはWorld Owner Thread上のSafe PointへCommandを集約する。

### World Lifecycle

World状態は`Active`、`ShuttingDown`、`Destroyed`を持つ。作成成功後は`Active`となる。明示`shutdown`はActive QueryがないOwner Threadで
だけ実行し、全Storageを逆作成順で破棄し、Slot TableとCommand Bufferを解放して`Destroyed`へ移る。Shutdown中またはDestroyed後の
Public操作は不正StateのProgrammer ErrorとしてAssertする。Destructorは未Shutdownなら同じ破棄順を実行するが、診断を返せないため通常Flowは明示Shutdownを
使用する。別ThreadでのDestructorを正しい終了経路として利用しない。

Runtime Session、複数World切替、Fixed／Variable Update、Frame ClockはIssue #147でWorld所有者として実装する。Global World Singletonを
導入せず、RuntimeHost、Editor Play Session、Testが所有者を明示する。Process共有`WorldIdentitySource`は所有者ではなく、
再利用不能なIncarnation値だけを発行する限定状態とする。

## Rejected Alternatives

### 旧版と同じ固定Bit SetとRTTI IDを前提にする

Type数上限、Process状態、Compiler実装を新しいSchemaとStorageへ持ち込むため採用しない。

### M10でArchetype Chunkを実装する

複数Component QueryのLocalityは有利だが、Entity移動、Chunk Packing、Fragmentation、Alignment、参照無効化を同時に固定する。
まずSafety契約と比較Benchmarkを確立するため採用しない。

### Query中の即時Mutationを自動的に許可する

Dense Array再配置とIterator無効化がCallback途中に発生し、結果が呼出し順へ依存するため採用しない。

### 全CommandをRollbackするTransactionにする

任意Componentの逆操作、Destructor、Allocation、External Side Effectを一般化する必要がありM10の範囲を超える。Command単位Atomicと
順序付きReportで失敗を可視化する。

### EntityHandleを永続Scene Identityとして使う

World、Slot再利用、Runtime Sessionに依存し、Authoring ObjectのIdentityを維持できないため採用しない。

## Consequences

### Positive

- 古いHandleと別World Handleを全Entity APIで拒否できる
- Componentを共通Virtual基底やMemory Imageへ固定せずPacked Storageへ置ける
- Query中の構造変更と参照無効化をSafe Pointへ集約できる
- Owner ThreadとShutdown順が明示され、Headless Testで寿命を検証できる
- Legacy Baselineと同じWorkloadでSafety Costを含む差を測定できる

### Negative

- 複数Component QueryはArchetypeよりSparse照合が増える可能性がある
- Swap-removeによりIteration順が安定しない
- World操作は単一Threadに限定され、Parallel Schedulerは後続作業になる
- Component Typeへnothrow move／destruct／construction制約が加わる
- Command Batch全体はTransactionではなく、呼び出し側が順序付き失敗Reportを処理する必要がある

## Validation

- Entity Create／Destroy／ReuseでIndex再利用とGeneration更新、古いHandle拒否を検証する
- 別WorldId、Invalid Index、Generation不一致、Generation上限Slot Retireを検証する
- Component Add／Get／Remove、Duplicate／Missing、Swap-remove、Destructor一回を検証する
- 0／1／10,000／100,000 EntityでSparse Set整合性を検証する
- Queryの最小Storage走査、0件、重複Type、Nested Query、参照Lifetimeを検証する
- Query中の即時Mutation拒否とDeferred CommandのFIFO／失敗Report／Pending Entity依存を検証する
- Wrong Thread、Nested Query、Query中即時Mutation、Query中ShutdownのAssert終了と、明示Shutdown、Destructor FallbackをProcess Testする
- Runtime WorldをGraphics、Window、Editorなしで起動・更新・終了する
- Issue #182と同じDevelopment条件で新ECS Benchmarkを取り、中央値／p95を機能差とともに記録する
- Debug／Development／Release Build、CTest、Public Header単体Compile、依存方向、`git diff --check`を実行する

## Follow-up Work

- Issue #144: Stable Schema RegistryとRuntime Dense Index
- Issue #145: Entity／Component Sparse Set Storage
- Issue #146: QueryとStructural Command Buffer
- Issue #147: Headless Runtime WorldとRuntime Session所有
- Issue #148: M10 Completion Gate、Legacy比較、全構成検証
- Archetype Research: 実測Query／Mutation比率に基づくStorage再評価
- Parallel ECS Research: Scheduler、Read／Write Access、Command Merge、Determinism
