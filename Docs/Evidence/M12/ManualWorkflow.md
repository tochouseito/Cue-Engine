# M12 Project Hub to Editor Manual Workflow

## Purpose

Project HubとEditorを別Processとして起動し、Project選択からScene作成、編集、保存、再起動、再Openまでの
最小制作Workflowを実Windowで確認する。Game Rendering、Viewport、Play Mode、Asset Importは対象外とする。

## Preconditions

- `Debug|x64`の全TargetをBuild済みであること
- `out/build/windows-vs2026/bin/Debug/CueProjectHubTool.exe`と同じDirectoryに
  `CueEditorTool.exe`が存在すること
- Test専用の空DirectoryをProject作成先として使用すること
- Project HubとEditorが参照するProject Descriptorの正本は`CueProject.json`であり、Source AssetのRootは
  Descriptorの`roots.sourceAssets`から解決されること

## Project Hub Workflow

1. `CueProjectHubTool.exe`を起動する。
2. 「新しいProject」を選択し、Test用Directory、Project名、表示名を入力して作成する。
3. 作成したProjectが一覧に一件だけ表示され、選択、Pin固定、Pin解除ができることを確認する。
4. Projectを選択して「Editorで開く」を押す。
5. Project Hubが終了せず、Editor Processの実行中は二重起動を受け付けないことを確認する。
6. Editorを通常終了し、Project Hubが再び同じProjectを選択できる状態へ戻ることを確認する。
7. Editorを再度起動して強制終了し、Project Hub自体は継続して操作できることを確認する。

## Editor Scene Workflow

1. Project-only画面で「新しいScene」を選択し、`Scenes/Main.cuescene`を作成する。
2. HierarchyでObjectを追加し、名前変更、Parent変更、Transform編集を行う。
3. UndoとRedoを実行し、Hierarchy、Inspector、Selection、Dirty表示が同じRevisionへ戻ることを確認する。
4. Sceneを保存し、`Source Assets/Scenes/Main.cuescene`が作成されることを確認する。
5. 「名前を付けて保存」または`Ctrl+Shift+S`で別Locatorへ保存し、元Fileを変更せず新しいScene Fileが作成されることを確認する。
6. 未保存変更がない状態でSceneを再読込し、Scene ID、Object ID、Hierarchy、Transformが維持されることを確認する。
7. Editorを終了し、Project Hubから同じProjectを再度開く。
8. 「Sceneを開く」から`Scenes/Main.cuescene`を開き、Scene IDとObject IDが前回保存時から維持されることを確認する。

## Unsaved and Error Workflow

1. Sceneへ変更を加えてEditorのWindowを閉じ、「キャンセル」で編集状態へ戻ることを確認する。
2. 再度Windowを閉じ、「保存」で保存完了後にEditorが終了することを確認する。
3. Sceneへ別の変更を加えてWindowを閉じ、「破棄」で保存せずにEditorが終了することを確認する。
4. `../Outside.cuescene`、絶対Path、不正文字をScene Locatorへ入力し、Project Root外を開けないことを確認する。
5. 存在しないSceneを開き、Editor Processが落ちず、同じProject-only画面から再試行できることを確認する。
6. Scene編集中に存在しない、または破損したSceneを開き、現在のScene、Selection、History、Dirty状態が維持されることを確認する。
7. 保存結果が未確定になった場合、成功表示や自動終了をせず、「再検証 / 再試行」と「不確定記録を破棄」を選べることを確認する。
8. 新規Sceneの初回保存先が既に存在する場合、通常Saveでは置換せず、別の上書き確認を表示することを確認する。
9. DirtyなSceneのRecoveryがSaved Rootへ自動保存され、Editor再起動後のRecovery一覧から編集内容を開けることを確認する。

## Expected Persistence

- 再起動後のProject ID、Scene ID、Object IDが一致する。
- 保存済みSceneの読込失敗時、現在のSessionへ部分的なDocumentを公開しない。
- 保存失敗または保存未確定時、成功表示と終了を行わず、元Fileを保持して判断画面へ戻る。
- Project HubはEditorの正常終了、起動失敗、異常終了のいずれでも自身の操作可能状態を回復する。

## Automated Process Boundary

`Cue.Editor.Workflow.ProcessRoundTrip`はTest用Projectと保存済みSceneを作成し、実際の`CueEditorTool.exe`をVersion付き
Command Lineと`--initial-scene`で四Process起動する。最初の子Processは未編集の新規Sceneを作成してRecovery Autosaveを実行し、
親Processが未保存LocatorとIdentityを再Openして検証する。次の子Processは既存Sceneを編集してRecovery Autosaveを実行し、親Processが
Recovery内容を再Openして検証する。三つ目の子Processは別のObjectを追加し、Dirty CloseのSaveを通してSceneを閉じる。最後の子Processは
保存済みSceneを再Openする。各ProcessはTest専用の有限Frame設定で正常終了し、Project ID、Scene ID、既存Object ID、子Processで
追加したObject、保存Dataが維持されることを検証する。

## Deferred Default Scene

Project Descriptor schema version 1では`defaultScene`を`null`に保つ。Default Startup SceneとDescriptor Migrationは
M16 Issue #229の対象であり、M12ではProject再Open後に明示的にSceneを開く。
