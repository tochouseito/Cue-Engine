---
sidebar_position: 1
title: Editor の使い方
---

# Editor の使い方

Cue Engine の Editor は Windows 専用のホストアプリケーションです。
プロジェクト作成、Scene 編集、Asset 管理、GameScript ビルド、Play 実行、配布用ビルドを扱います。

## 起動前の準備

Editor を使う前に、Engine 本体を `Debug|x64` でビルドします。

```powershell
pwsh -NoProfile -File scripts/codex_build.ps1
```

Editor の出力先は次の場所です。

```text
generated/outputs/Editor/Debug/Editor.exe
```

## Project Hub

Editor 起動直後は `Project Hub` が表示されます。

- `新規プロジェクト作成`: プロジェクト名と作成先ディレクトリを指定して新規プロジェクトを作成します。
- `プロジェクトを開く`: 既存プロジェクトのフォルダを選択して開きます。

プロジェクト直下に `cueproject.json` があるフォルダが Cue Engine プロジェクトとして扱われます。
新規作成時は、少なくとも次の構成が生成されます。

```text
Assets/
Assets/Scenes/
Assets/Scripts/
EngineModule/
Saved/
Intermediate/
CMakeLists.txt
CMakePresets.json
cueproject.json
```

`cueproject.json` には `assetRoot`、`scriptRoot`、`startupScene`、`scriptBuildConfiguration`、配布ビルド設定が保存されます。
現在の Editor は `scriptLoadConfiguration` と `scriptBuildBackend` を使いません。
GameScript は常に `scriptBuildConfiguration` の構成から読み込み、ビルド backend は常に CMake です。

## 基本画面

プロジェクトを開くと、Editor は `cueproject.json` の `startupScene` を読み込みます。
`assetRoot` は Asset Browser と Inspector に設定され、`scriptRoot` から `GameScript.dll` の読み込みが試行されます。

主なビューは次の通りです。

- `GameView`: Play 実行中のゲーム画面を表示します。
- `DebugView`: Editor 用のデバッグ表示です。追加、ビュー、シーン操作のメニューを持ちます。
- `Asset Browser`: `Assets` 配下のフォルダとファイルを表示します。
- `ヒエラルキー`: 読み込み済み Scene と GameObject をツリー表示します。
- `インスペクター`: 選択中 GameObject または Material asset の詳細を編集します。
- `Frame Statistics`: Editor / View の計測情報を表示します。
- `Script Build Output`: GameScript と配布ビルドのログを表示します。
- `Navigation Debug`: NavMesh と Navigation のデバッグ情報を表示します。

## メニューバー

メニューバー左端には Undo / Redo のアイコンボタンがあります。
`編集` メニューは廃止され、編集系の追加操作は `DebugView > 追加` に移動しています。

### Undo / Redo

- `Undo`: 直前の編集コマンドを戻します。ショートカットは `Ctrl+Z` です。
- `Redo`: 戻した編集コマンドをやり直します。ショートカットは `Ctrl+Y` または `Ctrl+Shift+Z` です。

### ファイル

- `シーンを保存`: 現在の Scene を保存します。ショートカットは `Ctrl+S` です。
- `シーンを再読み込み`: 現在の Scene をバックグラウンドで再読み込みします。

### 表示

各 Window を表示してフォーカスします。
すでに表示されている Window を選んだ場合は、表示状態を維持したままフォーカスだけを移します。

- `GameView`
- `DebugView`
- `Asset Browser`
- `ヒエラルキー`
- `インスペクター`
- `Frame Statistics`
- `Script Build Output`
- `Navigation Debug`

### ナビゲーション

- `Scene NavMesh を Bake`: 現在の Scene から NavMesh を Bake します。Play 中は実行できません。
- `Debug Window`: `Navigation Debug` Window の表示を切り替えます。

### ビルド

- `ゲーム配布ビルド構成`: 配布用ビルドの構成を `Debug`、`RelWithDebInfo`、`Release` から選択します。
- `ゲーム配布 backend`: 配布用ビルドの backend を選択します。
- `ゲーム配布アプリ設定`: 配布される `exe` 名、タイトルバー、アプリアイコンを設定します。
- `ゲーム Release ビルド`: Engine 側の `CueApp` とプロジェクト側の `Game` / `CueApp` をビルドし、配布フォルダを作成します。
- `ゲーム Release ビルドフォルダを開く`: `gameReleaseOutputRoot` の出力先を Shell で開きます。
- `GameScript solution を開く`: プロジェクトの CMake preset から Visual Studio solution を開きます。
- `Editor にデバッガをアタッチ`: Visual Studio から現在の Editor プロセスへアタッチします。
- `Script Build Output`: Script Build Output Window の表示を切り替えます。

GameScript の手動ビルドと手動再読み込み項目はビルドメニューから削除されています。
Script source の更新を検出し、Editor Window がフォーカスされている場合、Editor は GameScript を自動でビルドして再読み込みします。

### Play 操作

Play 操作はメニューバー中央のアイコンボタンで行います。

- `Play`: 現在の World で Play を開始します。
- `Pause (Stop)`: Play を停止します。
- `Stop (Exit)`: Play を終了して Editor 状態へ戻ります。

GameScript のビルドや再読み込み中は Play 操作が無効になります。
メニューバー中央には `GameScript ビルド構成` の combo も表示されます。
この combo で選んだ構成が、ビルドと読み込みの両方に使われます。

## DebugView メニュー

`DebugView` Window には専用のメニューバーがあります。

### 追加

- `3D > カメラを追加`
- `3D > オブジェクトを追加`
- `2D > オブジェクトを追加`
- `マテリアルを追加`
- `GameScript を追加`
- `メインカメラ`

追加先 Scene は、ヒエラルキーで選択中の Scene、または選択中 GameObject の所属 Scene です。
どちらも選択されていない場合は現在の primary Scene に追加されます。
GameObject 名は World 全体で一意です。別 Scene であっても同名は許可されません。

### ビュー

- `グリッドを表示`: DebugView の grid 表示を切り替えます。
- `描画モード > ソリッド`: マテリアルとライティングを使わず、固定色で表示します。
- `描画モード > マテリアル`: マテリアルだけを使い、ライティングなしで表示します。
- `描画モード > ライティング`: 固定色にライティングだけを適用して表示します。
- `描画モード > レンダー`: マテリアルとライティングの両方を使って表示します。

### シーン

`読み込み済みシーン` には、現在 Editor World に読み込まれている Scene 名だけが表示されます。
`読込` ボタンから `Assets/Scenes` 配下の `.cuescene` を追加読み込みできます。
すでに読み込まれている Scene は一覧上で選べない状態になります。

## Asset Browser

Asset Browser は `Assets` 配下をフォルダ単位で表示します。
ファイルはアイコン付きボタンとして表示されます。

- `.cuematerial`: Material アイコンを表示します。
- `.png` / `.cuetexture`: Image アイコン、または読み込み済み texture のプレビューを表示します。
- その他のファイル: Unknown アイコンを表示します。

Material asset を選択すると Inspector に Material の詳細が表示されます。
Texture asset は drag and drop payload として Material Inspector へ渡せます。

Editor Window へ外部ファイルをドロップすると、現在開いている Asset Browser のフォルダへコピーして import します。
Asset Browser が `Assets` 以下のフォルダを開いていない場合は `Assets/` にコピーします。
対応している外部ファイルは次の通りです。

- `.png`: `.cuetexture` に cook して登録します。
- `.wav`: `.cuesound` に cook します。
- `.obj`: `.cuemodel` に cook して登録します。

## Inspector

GameObject を選択すると Component ごとのタブで詳細を編集できます。
Material asset を選択すると Material Inspector が表示されます。

Material Inspector では現在次の項目を編集できます。

- `color`: Material color を編集して保存します。
- `Use Texture`: texture を使うかを切り替えます。
- `Texture をここへドロップ`: `.cuetexture` を drag and drop して Material に設定します。

RendererComponent の Material 欄には Asset Browser から `.cuematerial` を drag and drop できます。

## ヒエラルキー

ヒエラルキーは Scene 単位の tree で表示されます。
各 Scene の下に、その Scene に所属する GameObject が表示されます。

- Scene をクリックすると Scene が選択されます。
- GameObject をクリックすると GameObject と所属 Scene が選択されます。
- GameObject のダブルクリック、または右クリックメニューから名前変更できます。
- GameObject の右クリックメニューから削除できます。

Scene を複数読み込んでも、World は GameObject を一元管理します。
保存時は、それぞれの Scene の所属情報に従って Scene asset へ書き戻されます。
同じ Scene asset を複数同時に読み込むことは避けてください。

## 配布ビルド

`ゲーム Release ビルド` は、プロジェクトの `gameReleaseOutputRoot` に配布用フォルダを作成します。
既定値は次の通りです。

```json
{
  "gameReleaseOutputRoot": "Builds/Windows"
}
```

配布フォルダには `CueApp.exe`、`EngineResources`、`config`、`dxcompiler.dll`、`dxil.dll`、`Assets`、`cueproject.json` が集められます。
配布時の実行ファイル名、タイトルバー、アプリアイコンは `ゲーム配布アプリ設定` で変更できます。
アイコンはプロジェクト内の `.ico`、`.png`、`.jpg`、`.jpeg`、`.bmp` を指定できます。

Release asset としてコピーされるのは、cook 済みの `.cuetexture`、`.cuematerial`、`.cuescene`、`.cuemodel`、`.cuesound` です。
