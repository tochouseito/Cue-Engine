# M11 Authoring Scene and Persistence Completion Gate

## Gate Result

M11の先行Issue #149から#154がGitHub上でClosedであり、本Gate Issue #155がMilestone最後の1件であることを2026-09-03に確認した。
本変更のPRが`Closes #155`でMergeされた時点で、M11は7/7 Closedとなる。

| Acceptance Gate | Result | Evidence |
|---|---|---|
| M11配下の全Issue | Pass予定 | 先行6件Closed、本PRが最後の#155をCloseする |
| Debug／Development／Release Build | Pass | 全Targetを3構成でBuild成功 |
| CTestとScene Process Test | Pass | 3構成とも191 Test、失敗0。`Scene` Label 9 Testと4件のProcess Testを含む |
| 保存失敗時の元File維持 | Pass | Main File書込失敗時に`NotPublished`を返し、元FileとBackupがともに元内容のままであることを検証 |
| DocumentとRuntimeWorldの同期境界 | Pass | Snapshot生成後のDocument変更はRuntime実体化へ混入せず、Runtime変更はDocumentへ暗黙反映されない |
| 実行記録と未検証範囲 | Pass | 本文末尾へ明記 |

Releaseの全CTestでは、Debug Layer／InfoQueue／DREDを必要とする既存4 Testが構成条件によりSkippedとなった。
M11対象の`Scene` Label 9 Testは全構成で実行され、成功した。

## Coverage Map

| M11 Scope | Verification |
|---|---|
| Stable IDとHierarchy | `Cue.Scene.Document`がObject ID維持、追加／削除／Rename／Parent変更、Duplicate／Dangling／Cycle拒否、Hierarchy Depth上限を検証 |
| Component Data | `Cue.Scene.ComponentData`がSchema駆動の既知Data、未知Component／Field保持、型不一致、Component／Field上限を検証 |
| Scene Round-trip | `Cue.Scene.Serialization`が同一Documentの決定的出力とSerialize／Parse／Serialize一致を検証 |
| Migration | `Cue.Scene.Serialization`がScene FormatとComponent Schemaの連続Migration、Missing Step、未対応将来Versionを検証 |
| Unknown Data Round-trip | `Cue.Scene.Serialization`がScene Extensionと未知Fieldを再保存後も保持することを検証 |
| Atomic Save失敗 | `Cue.Scene.Serialization`が書込失敗時の元File／Backup維持、Durability Unknown、保存後検証失敗の状態区別を検証 |
| Malformed Input | `Cue.Scene.Serialization`が空入力、BOM、構文不正、重複Key、Trailing Data、不正UTF-8、過剰Nest／Object／Container／File Sizeを拒否し、既存Documentを維持することを検証 |
| Runtime Instantiation | `Cue.Scene.Instantiation`がObjectIdからEntityIdへの対応、Hierarchy、Transform、既知Component、決定的構築順、空Sceneを検証 |
| Rollbackと寿命 | `Cue.Scene.Instantiation`が途中失敗時の全Entity Rollback、World不一致拒否、明示終了、Destructor／Move契約を検証 |
| Process境界 | `Cue.Scene.Process.*` 4 Testがlive所有権を残した破棄／Move代入のfail-fastと、明示終了後／moved-fromのDestructorが無害であることを別Processで検証 |

SceneDocumentは編集・保存の正本、SceneSnapshotはRuntime実体化の入力、RuntimeWorldは実行状態として分離される。
`test_successful_instantiation`ではSnapshot作成後にDocumentのActive状態とParentを変更してもSnapshotから生成したRuntime状態が変わらず、Runtime側のActive状態変更もDocumentへ戻らないことを検証している。
RuntimeWorldはSceneDocumentへの生Pointerを保持せず、DocumentとRuntimeWorldの暗黙の双方向同期は存在しない。

## Validation Commands

- `cmake --preset windows-vs2026`
- `cmake --build --preset windows-vs2026-debug --parallel`
- `cmake --build --preset windows-vs2026-development --parallel`
- `cmake --build --preset windows-vs2026-release --parallel`
- `ctest --preset windows-vs2026-debug --output-on-failure`
- `ctest --preset windows-vs2026-development --output-on-failure`
- `ctest --preset windows-vs2026-release --output-on-failure`
- `git diff --check`

BuildとCTestはSource Commit `bf53d581a34bc0ce595b1e9c65659320d956e96c`に対して実行した。
本Issueの変更はこの検証結果を記録する文書だけであり、Source、Test、Build設定は変更していない。
Pull Requestの最新HeadはWindows CIで同じ3構成を再検証する。

## Not Run

- AddressSanitizer、ThreadSanitizer、UndefinedBehaviorSanitizer
- Fuzzerによる長時間の自動入力生成
- 数時間以上のScene Load／Save／Instantiation Soak Test
- 実Diskの電源断、Process強制終了、Disk Full、権限喪失を伴うFailure Injection
- Editor UI、Undo／Redo、Prefab、Scripting、Scene Streamingとの統合
- 複数ThreadからのSceneDocument編集またはRuntime実体化

## Remaining Risks

- Atomic SaveはFilesystem抽象を通じた決定的Failure Injectionで検証しており、実Hardware障害時の完全性は未検証
- Unknown Data保持には明示的なResource上限があり、上限を超える将来Dataは診断付きで拒否される
- SceneDocumentとRuntimeWorldは明示的なSnapshot実体化だけを提供し、Play Mode変更反映は未実装
- SceneComponentのRuntime生成には登録済み`RuntimeComponentBuilder`が必要であり、未対応Typeは実体化全体をRollbackする
- Editor操作、Prefab Override、Script Component、Runtime Packageは後続Milestone対象
