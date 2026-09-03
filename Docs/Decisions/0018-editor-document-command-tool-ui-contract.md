# ADR-0018: Editor Document, Command Transaction, and Tool UI Contract

- Status: Accepted
- Date: 2026-09-04
- Decision Owners: CueEngine Project

## Context

M12では、Projectを選択し、Sceneを編集し、Undo／Redo、保存、再読込を行える最小Editor制作Loopを構築する。
ADR-0017は`SceneDocument`をAuthoring Sceneと保存Dataの正本とし、Selection、Dirty、編集履歴、File Locator、
Recovery Policyを将来の`EditorDocument`が所有すると決定した。本ADRは、そのApplication層とTool UIの境界を決定する。

Editor UIが`SceneDocument`、Serializer、Filesystem、`RuntimeWorld`を直接操作すると、同じ操作をHeadless Test、
Automation、将来の別Presentationから再利用できない。さらに、UI Widgetの寿命、Runtime Entityの寿命、永続Objectの寿命が
混在し、Undo、外部変更競合、保存失敗、Editor再起動で状態の正本が不明確になる。

M12ではViewport Rendering、Play Mode、Scripting、Prefab、Asset Import／Cook、Game Runtime Packagingを決定しない。

## Legacy Reference

### Legacy Problem

旧CueEngineも、Projectを選択してEditorを起動し、HierarchyとInspectorからSceneを編集し、保存する必要があった。

### Legacy Approach

旧設計ではEditor Manager、Project Hub、Scene、Command、GUIの責務が近く、UI CallbackからModelやRuntime Objectへ
到達しやすい構造だった。Project Open、Window、Scene、Runtime Objectの寿命もApplication全体の運用へ依存していた。

### Legacy Strengths

- Project選択からScene編集までの経路が短かった
- HierarchyとInspectorの操作をCommandへまとめる意図があった
- Editor固有機能を一つのApplicationとして提供できた

### Legacy Problems

- UI、Authoring Data、Runtime Objectの依存方向が明示されていなかった
- Commandが永続Identityではなく実行中Objectの参照を保持し得た
- Dirty、Saved、Undo位置の関係がRevisionとして定義されていなかった
- 複数変更の途中失敗時に完全Rollbackできる契約が不足していた
- SaveのPublish後不明状態、外部変更、Recoveryの正本が明確でなかった
- Project HubとEditor Processの失敗境界が呼び出し側へ漏れていた

### Current Requirements

- `SceneDocument`を唯一のAuthoring Scene正本として維持する
- Editor Session状態を`SceneDocument`とScene Fileへ混入させない
- すべての編集をStable IDを対象とするCommand境界へ集約する
- 一つのUser操作をAtomicなTransactionとしてUndo／Redoする
- Dirtyを現在Revisionと保存Revisionの比較で判定する
- Save、Reload、Close、Recovery、外部変更競合をHeadlessに検証する
- Project HubをRuntime Graphicsなしで起動可能にする
- ImGuiをPresentation Adapterに限定する
- RuntimeからEditorへの依存を作らない

### New Design

現在の要件から`Cue.EditorCore`を新しいFirst-party Moduleとして設計する。旧Source Code、型、Command実装、
Window Layout、Process起動実装はコピー、移植、部分抽出しない。Legacyは解決対象と境界不足の確認だけに使用する。

### Validation

依存Graph、Revision遷移、Selection整合、Command失敗Rollback、Undo／Redo、保存失敗、外部変更、Recovery、
Project Hub Process監視をUnit TestとProcess Testで検証する。ImGui AdapterはSemantic IntentとViewModelの接続だけを検証する。

## Reference Engine Comparison

| Engine | 参考にする点 | CueEngineでそのまま採用しない点 |
| --- | --- | --- |
| Unreal Engine | Transaction単位の編集、詳細Panel、AssetとWorldの明確な制作Workflow | UObject、Editor Transaction、Package形式、Reflection ABIを共通基底として移植しない |
| Unity | HubからProjectを選び、Hierarchy／InspectorでSceneを編集する理解しやすいLoop | Instance ID、Library内部状態、SerializedObjectをCueEngineの永続IdentityやCommandへ流用しない |
| Godot | Scene Tree中心の編集とTool実行時の軽量なFeedback | Node継承TreeをRuntime ECSまたはEditor Commandの共通Object Modelへ強制しない |
| SOL-AVES | Runtimeと制作Pipelineを分け、Iterationの待ち時間を独立した設計課題として扱う視点 | 公開資料から非公開ABI、内部Format、実装詳細を推測せず、Source CodeやData Layoutを取り込まない |

CueEngineは、ProjectからScene編集までの短い導線、Transaction単位の安全な編集、明確なHierarchy／Inspectorを取り入れる。
一方、各Engine固有のObject Model、Serializer、Package、Reflection ABIは採用せず、M09からM11で確立した
Project、Schema、Scene、Runtime World境界へ接続する。

## Decision

### Module Boundary

`Cue.EditorCore`はPlatform、Window、ImGui、Rendererに依存しないApplication Moduleとする。初期依存は
`Cue.Foundation`、`Cue.IO`、`Cue.Project`、`Cue.Scene`、`Cue.Schema`だけを許可する。

依存方向は次のとおりとする。

```text
Cue.ProjectHub.ImGui ----> Cue.EditorCore ----> Cue.Project
          |                       |-----------> Cue.Scene ----> Cue.GameCore
          |                       |-----------> Cue.IO
          |                       `-----------> Cue.Foundation
          `--------------> Cue.Platform

Cue.Editor.ImGui --------> Cue.EditorCore
          `--------------> Cue.Platform

Cue.RuntimeHost ---------> Cue.GameCore / Cue.Scene
```

`Cue.EditorCore`は`Cue.Platform`、`Cue.RHI`、D3D12、Renderer、ImGuiへ依存しない。`Cue.Scene`、
`Cue.Project`、`Cue.GameCore`、`Cue.RuntimeHost`は`Cue.EditorCore`へ依存しない。

M12のImGui HostはEditor用Descriptor、Game Renderer、Viewport Render Targetを要求しない。WindowとImGui Backendに必要な
Platform接続はPresentation Hostが所有し、`Cue.EditorCore`の公開APIへNative Handleを出さない。

### EditorDocument Ownership

`EditorDocument`は、一つの開いているSceneに対するEditor Session状態を一意に所有する。

所有する状態は次のとおりとする。

- 一つの`SceneDocument`
- Project Rootから解決されるScene Locator
- 現在State Revisionと最後に確定保存されたState Revision
- SelectionとHierarchy展開状態
- Undo／Redo History
- 外部変更Conflict状態
- RecoveryとClose Workflowの進行状態

`EditorDocument`は`RuntimeWorld`、`SceneInstance`、Runtime `EntityHandle`、Renderer Object、ImGui Widgetを所有しない。
`ProjectWorkspaceSession`は一つのProject Descriptorと複数の`EditorDocument`を所有するが、M12のUIは一つのScene Tabだけを
表示してよい。複数TabのPresentationは将来Scopeとする。

`EditorDocument`のMutationは作成Threadに限定する。Read-only Viewは同じThreadで取得し、非同期I/O結果は
Session Generationを照合してからOwner Threadで適用する。M12ではBackground Threadから直接Mutationしない。

### Stable Editor Identity

Selection、Command、Historyは`SceneAssetId`、`ObjectId`、`ComponentInstanceId`を使用する。
Runtime `EntityHandle`、Object Address、Container Index、ImGui ID、File PathだけをIdentityとして保存しない。

Editor Session内では`EditorDocumentId`とSession Generationを一時Identityとして使用できる。ただし、これらはScene Fileへ
永続化せず、Process再起動後の同一性へ使用しない。

### State Revision and Dirty

各EditorDocumentは単調増加し、再利用しない`DocumentStateId`を発行する。受理された永続Data変更だけが新しいStateを作る。

- `currentStateId`: 現在表示しているAuthoring状態
- `savedStateId`: 最後に確定保存されたAuthoring状態
- `nextStateId`: 次に発行する値

Dirtyは`currentStateId != savedStateId`で判定する。選択、Hierarchy展開、Panel配置などのSession-only変更はStateを進めない。
Undoで保存済みStateへ戻った場合はCleanになり、Redoで離れた場合はDirtyになる。Redo分岐を破棄してもState IDを再利用しない。

新規未保存Sceneは保存先が確定していない状態を別に持ち、State IDが一致していても保存要求が必要である。
単純な`bool isDirty`を独立した正本として保持しない。

### Command Boundary

Presentationは`EditorIntent`を生成し、`EditorController`がIntentを検証して具体的なCommandまたはWorkflowへ変換する。
UIは`SceneDocument`のMutation API、Scene Serializer、Filesystem、`RuntimeWorld`を直接呼ばない。

Commandは次の契約を満たす。

- 対象EditorDocument、Scene、Object、ComponentをStable IDで指定する
- Runtime Pointer、Runtime `EntityHandle`、ImGui Stateを保持しない
- 実行前に対象存在、Document一致、Schema、Hierarchy Cycle、Depth、値範囲を検証する
- 失敗時にDocument、Selection、Revision、Historyを変更しない
- 単体またはTransaction内のCommand成功だけでは`DocumentStateId`を発行しない
- 診断可能な`Result`を返す

Command TypeはEditor Coreの公開Requestであり、`SceneDocument`の公開Modelを万能Command基底型へ変更しない。
Renderer、Physics、Audio、ScriptのRuntime ObjectをCommand対象へ追加する場合は、各機能のAuthoring Schemaが決定された後に
別Issueで拡張する。

### Transaction and Strong Failure

一つのUser操作は一つの`EditorTransaction`として扱う。Transactionは一つ以上のCommand Requestと表示用Labelを持ち、
全変更を一つのHistory EntryとしてCommitする。

Transaction実行前に、Editor Coreは現在の完全なAuthoring状態を`SceneDocumentCheckpoint`として取得する。
CheckpointはScene ID、Object、Hierarchy、Transform、Component値、未知Component／Field、Extension Dataを保持し、
Runtime状態とEditor Session状態を含まない。

すべてのCommand適用と最終`SceneDocument::validate()`が成功した場合だけ、Revision、History、Selection整合をCommitする。
途中失敗またはValidation失敗時はCheckpointから完全に復元し、Transaction前の公開状態を維持する。
一つ以上の永続Data変更を含むTransaction Commitが成功した時点で、Transaction全体に対して一つだけ新しい
`DocumentStateId`を発行する。Commandごとの仮Revisionまたは公開Revisionを発行しない。

`SceneDocumentCheckpoint`はRuntime実体化用`SceneSnapshot`と別型とする。M12では正しさと未知Data保持を優先して完全Checkpointを
基準実装とし、Subtree／Component単位の差分最適化は同じ復元契約を満たす場合だけ追加できる。

### Undo and Redo

History EntryはTransactionのLabel、Before／After `SceneDocumentCheckpoint`、Before／After `DocumentStateId`を所有する。
UndoはBefore、RedoはAfterを復元する。復元後にScene検証を行い、Selectionから存在しないIDを除去する。

新規Transaction成功時はRedo Stackを破棄する。Historyは無制限に保持せず、Entry数と推定Byte数の両方で上限を持つ。
上限超過時は最古EntryからTransaction単位で破棄する。保存済みStateのHistory Entryが破棄されても`savedStateId`は維持し、
Dirty判定のためにState IDを再利用しない。

Persistent Undo History、Collaborative History、Play Mode変更取込はM12対象外とする。

### Selection Reconciliation

Selectionは順序を持つStable `ObjectId`集合とPrimary Selectionで表す。Command、Undo、Redo、Reload後に、存在しないObject IDと
Component IDを除去する。Primary Selectionが消えた場合は、残るSelectionの先頭をPrimaryとし、残らない場合は未選択とする。

Delete後に親や隣接Objectを自動選択するかはPresentation Policyとし、Coreの既定動作は削除対象をSelectionから除去するだけとする。

### Save, Reload, and External Change

保存は`EditorController`のWorkflowとして、`Cue.Scene`のAtomic Saveを使用する。UIはSerializerとFilesystemを直接呼ばない。

`ProjectWorkspaceSession`は一つの`SceneSaveCoordinator`をSession開始から終了まで一意所有する。Coordinatorは同じSession内の
Scene Save／Save As／Recovery Publishを直列化し、Sceneごとの進行中Saveと`SceneWriteLease`を所有する。
`EditorDocument`はCoordinatorを所有せず、Session終了より長く参照しない。

`SceneWriteLease`は本文Path、`.backup` Path、同じ親DirectoryのLock Sidecarを一つの排他範囲とする。Leaseは本文Entry確認前に取得し、
旧Byte列読込、Backup公開、本文公開、再読込比較、結果状態の記録が完了するまで保持する。Windows Adapterは同じ物理Directoryと
File名へ解決されるLock Sidecarを、Write／Delete共有を許可しないNative Handleとして開き、Process終了時にもOSが解放する。
別Rootから同じ実Fileへ到達するCueEngine Writerも同じSidecarで競合し、Lease取得失敗を待機または診断可能なBusyとして返す。
Sidecar削除失敗は本文Saveの成否を変更せずSecondary診断とする。

M12では既存Scene本文が複数Hard Link名を持つ場合を`UnsupportedEntry`としてSave／Save Asの置換対象から拒否する。
Windows AdapterはLease取得後、Backup作成前にNative File InformationのLink Countを検査する。別名ごとに異なるSidecarを取得して
同じFile Identityを同時置換することを許可しない。Hard Linkを安全に編集する必要が生じた場合は、Volume／File IdentityをKeyにする
Cross-process Leaseを別Research Issueで決定する。

協調しない外部ProcessはLease Protocolに参加しないため、CoordinatorはLease取得直後かつBackup作成直前にFile Fingerprintを再取得し、
操作が保持するDestination Expected Fingerprintと一致しなければ本文を公開せずExternal Conflictへ遷移する。OSが許す競合Writerによる
この再検査後の変更を完全には排除できないことは既知Riskとし、Publish後の再読込比較で検出する。

通常SaveのDestination Expected FingerprintはEditorDocumentのBase Fingerprintとする。Save Asでは選択した保存先のFingerprintを
操作開始時に取得し、新規Fileなら明示的な`Missing`を期待値とする。Save As開始時にEditorDocumentのLocatorまたはBase Fingerprintを
変更せず、保存先への`Committed`成功時だけ切り替える。

Save開始時の`currentStateId`、Destination Locator、Destination Expected Fingerprint、Candidate Byte列のContent Digest、
Candidate Checkpointを`PendingSaveRecord`としてEditorDocumentが所有する。結果状態が確定するまで破棄しない。
結果は次のように処理する。

| Save結果 | Editor状態 |
| --- | --- |
| `Committed` | `savedStateId`をSave開始時Stateへ更新し、Destination Locatorと新しいFingerprintを記録する。現在Stateが進んでいればDirtyを維持する |
| `NotPublished` | Document、History、Dirty、現在Locator、元Fileを維持し、失敗を通知する |
| `PublishedButDurabilityUnknown` | Recordを保持したSave Uncertainへ遷移し、明示的な再確認を要求する |
| `PublishedButVerificationFailed` | Recordを保持したSave Uncertainへ遷移し、再確認後にExternal Conflictか保存済み状態を確定する |

保存中に編集が進んだ場合も、保存開始時Stateだけを`savedStateId`として記録するため、現在Stateとの差によりDirtyを維持する。
Undoで開始時Stateへ戻ればCleanになる。M12の同期実装ではOwner ThreadをBlockしてよいが、将来非同期化してもこの契約を維持する。

Save Uncertainの再確認は、Coordinatorが`PendingSaveRecord`のDestinationへ新しいLeaseを取得して行う。
`PublishedButDurabilityUnknown`では、最初にDestination Fileと親Directoryに対するDurability Barrierを再試行する。
Barrierが失敗した場合はByte列を再読込できてもUncertainを解除せず、Recordを保持する。Barrierが成功した場合、または元結果が
`PublishedButVerificationFailed`でDurability成功済みの場合だけ、本文を上限付きで再読込し、完全Parse、Migration、Validationした
Byte列のDigestをCandidate Digestと比較する。

DigestとScene Identityが一致した場合は、Recordの開始時Stateを`savedStateId`へ設定し、Destination Locatorと現在Fingerprintを記録して
Uncertainを解除する。現在Stateが進んでいればDirtyは維持する。同じDestinationへ同じCandidateを再保存し、通常の`Committed`を得た場合も
同じ状態へ遷移できる。一致しない、再読込できない、別Scene Identityである場合はExternal Conflictへ遷移し、Recordと
Candidate Checkpointを保持したままReload、Save As、Retry Durability／Verification、Cancelの明示Intentを要求する。
明示的なDiscardまたはSession CloseまでRecordを破棄しない。

File Fingerprintは最終更新時刻だけに依存せず、File SizeとContent Digestを含む。外部変更を検出した場合は暗黙に上書きせず、
Reload、Save As、Cancelの明示Intentを要求する。

Reloadは現在Fileを一時Documentへ完全Load、Migration、Validationしてから入れ替える。失敗時は現在Document、History、Selection、
Dirtyを維持する。成功時はHistoryをClearし、新しいState IDを発行して`currentStateId`と`savedStateId`を一致させる。

### Recovery

Recovery FileはADR-0013で定義したProject Descriptorの`Saved` Root配下にある`Editor/Recovery`へ置き、Scene正本と異なるLocator、
Metadata、Lifecycleを持つ。User Workspace、Source Assets、Runtime Assets、Generated、CacheへRecovery本文を置かない。

RecoveryはVersion付きEnvelopeとし、MetadataはRecovery Format Version、Project ID、Scene ID、正本Locator、Base Fingerprint、
State ID、Scene Data Digestを含む。既知の古いVersionは`N -> N + 1`の連続MigrationをMemory上で完了し、現行Versionとして
再検証してから利用する。Migration Step欠落、Resource上限超過、未来Version、未知必須FieldはRecoveryを削除または正本へ適用せず、
`UnsupportedRecovery`として診断し、元Recovery Fileを維持する。Recoveryを開いただけでは暗黙に現行Versionへ書き戻さない。

Recovery書込み成功は`savedStateId`を更新しない。起動時に有効なRecoveryを検出した場合は、正本を暗黙置換せず、Recover、Discard、
Inspectの明示Intentを要求する。Recover後のDocumentはDirtyな新Stateとして開き、通常Saveが成功するまで正本としない。

### Close State Machine

Close要求はCoreの状態遷移として処理し、UI Dialogを状態の正本にしない。

```text
CloseRequested
    | clean and no uncertain state
    v
Closed

CloseRequested
    | dirty / unsaved / conflict / save uncertain
    v
AwaitingDecision --Cancel--> Open
    |--Discard-------------> Closed
    `--Save--> Saving --Committed--> Closed
                    `--Failure-----> AwaitingDecision
```

DiscardはMemory上のEditorDocumentを閉じるだけで、正本FileやRecovery Fileを削除しない。Recovery削除は別の明示Intentとする。

### Project Hub and Editor Process Boundary

Project HubとEditorは別Processとする。Project HubはProject一覧、作成、登録、互換性表示、Editor起動要求を所有し、
Scene編集状態を所有しない。Editor Processは一つの`ProjectWorkspaceSession`を所有する。

Process境界ではC++ Object、STL Container、生Pointer、Native Handleを渡さない。`EditorLaunchRequest`は次を値として渡す。

- 正規化済みProject Descriptor Locator
- 期待する`ProjectId`
- Engine Compatibility識別子
- 任意の初期Scene Locator
- Launch Protocol Version

Editorは起動後に同じ`Cue.Project` ParserでDescriptorを再読込し、Project IDとCompatibilityを再検証する。失敗時は部分Sessionを
公開せず、診断可能なExit Resultを返す。Project HubはProcess終了を監視し、正常終了、起動失敗、異常終了のいずれでも再操作可能に戻る。

M12では複数Editor Processによる同一Project編集、Live IPC、Project Lock Protocolを決定しない。

### Presentation Adapter

ImGui AdapterはEditor Coreから取得した不変ViewModelを描画し、User入力をSemantic Intentとして返す。

- Hierarchy Rowは`ObjectId`を保持する
- Inspector Fieldは`ComponentInstanceId`とStable `FieldId`を保持する
- WidgetのText Buffer、展開、FocusはPresentation Stateとする
- Error、Progress、Conflict、Close確認はCoreの状態を表示する
- File Dialog結果は未検証PathとしてControllerへ渡し、CoreがProject Root境界とDescriptorを再検証する

Adapterは`SceneDocument`、`RecentProjectRegistry`、Filesystem、Serializer、`RuntimeWorld`を直接変更しない。
ViewModelはProject／Scene Modelを再実装せず、表示に必要な値と操作可否だけを持つ。

### Error and Lifetime Contract

公開操作は`Result`を返し、失敗理由と対象Identityを診断へ含める。UI表示用日本語TextをCore Errorの正本にせず、
安定Error CategoryとContextからPresentationがMessageを生成する。

`ProjectWorkspaceSession`、`EditorDocument`、History、Recovery Workflowの所有権は一意とし、Global Singletonへ置かない。
公開View、Selection Span、ViewModel Snapshotの有効期間は次のMutationまでとし、長期保持にはStable IDを使用する。

### Headless Test Boundary

次をWindow、ImGui、D3D12 Device、Renderer、RuntimeHostなしで検証可能にする。

- Project Workspace SessionのOpen／失敗Rollback
- RevisionとDirty遷移
- Selection整合
- 全Command ValidationとStrong Failure
- Transaction Commit／Rollback
- Undo／RedoとHistory上限
- Save／Reload／External Conflict／Recovery／Close状態遷移
- Project Hub ServiceとEditor Launch Request生成
- Process終了Resultの解釈

Buildの依存検証で、`Cue.EditorCore`から`Cue.RHI`、D3D12、Renderer、ImGuiへの依存と、Runtime Moduleから
`Cue.EditorCore`への逆依存を拒否する。

## Consequences

### Positive

- UIなしで編集と保存の主要状態遷移を再現できる
- Stable IDとRevisionによりUndo、Selection、Dirtyが同じ規則で整合する
- Transaction途中失敗がSceneへ部分適用されない
- Project HubとEditorの異常終了をProcess境界で隔離できる
- Tool UIのためにRuntime Graphics公開APIを拡張せずに済む
- 将来の別UI、Automation、Command Paletteが同じCoreを利用できる

### Trade-offs

- 完全CheckpointはMemory使用量とCopy Costが大きい
- Core、Workflow、Presentationを分離するため、単純なUI Callbackより型と状態遷移が増える
- Process境界ではProject情報を再検証するため、起動時I/Oが重複する
- Save UncertainとExternal Conflictを区別するため、UIに追加の判断経路が必要になる

### Mitigations

- HistoryをEntry数とByte数で制限し、将来の差分Checkpoint最適化を契約内で許可する
- ViewModelとIntentを小さく保ち、Scene／Project Modelの重複実装を禁止する
- すべての状態遷移をHeadless Testし、Presentation Testを描画とIntent変換へ限定する
- Process Launch ProtocolをVersion付き値Contractにし、ABI共有を避ける

## Rejected Alternatives

### ImGui CallbackからSceneDocumentを直接変更する

実装は短いが、Automation、Headless Test、Undo、失敗Rollback、別Presentationで同じ操作を再利用できないため採用しない。

### RuntimeWorldをEditorの編集正本にする

Runtime EntityはSession-localで、Slot再利用とRuntime Component Layoutが永続編集に適さないため採用しない。

### Dirtyを独立したboolで管理する

Undoで保存地点へ戻る場合、保存中の編集、Redo分岐で正しく復元できないため採用しない。

### CommandへRuntime Entity Pointerを保持する

Undo実行時までPointer寿命を保証できず、Scene再読込とRuntime再生成で無効になるため採用しない。

### Runtime SceneSnapshotをUndo Snapshotとして共用する

Runtime実体化に不要な未知Data、Editor復元情報、保存完全性を保持する契約ではないため採用しない。

### Project HubとEditorを一つのProcess／Global Stateにする

Editor起動失敗や異常終了がHubを巻き込み、Project選択へ戻れないため採用しない。

### Tool UIのためにCue.RHIまたはGame Rendererを拡張する

M12のHierarchy／Inspector／Project Hubは描画Runtime機能を必要とせず、EditorとRuntimeの依存を混在させるため採用しない。

## Implementation Sequence

1. `Cue.EditorCore`にEditorDocument、Revision、Selection、Close状態を実装する
2. Stable IDを対象とするScene編集Commandと完全Checkpointを実装する
3. Transaction型Undo／RedoとHistory上限を実装する
4. Atomic Save、Reload、External Conflict、RecoveryをEditor Workflowへ接続する
5. Project Hub ServiceとViewModelを実装する
6. ImGui Project Hub ShellをPresentation Adapterとして接続する
7. Hierarchy／InspectorをViewModel／Intent／Commandへ接続する
8. Project OpenからEditor再起動／再OpenまでのProcess Workflowを統合する
9. Headless、Process、手動UI WorkflowをM12 Completion Gateで検証する

## Follow-up

- M12 #157から#165で本契約を実装、統合、検証する
- Viewport Rendering、Play Mode、Scriptingは後続MilestoneのResearchを先行する
- 差分CheckpointやCommand Coalescingが必要な場合は、計測結果と復元同値Testを伴う別Issueで判断する
- 複数Editor同時編集、Project Lock、Crash-safe Persistent Undoは別Research Issueで扱う
