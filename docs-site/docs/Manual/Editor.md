---
sidebar_position: 1
title: Editor の使い方
---

# Editor の使い方

Cue Engine の Editor は Windows 専用のホストアプリケーションです。
プロジェクトを開き、スタートアップシーンを読み込み、Scene 編集、Script ビルド、Play 実行、配布用ビルドを行います。

## 起動前の準備

Editor を使う前に、Engine 本体を `Debug|x64` でビルドします。

```powershell
pwsh -NoProfile -File scripts/codex_build.ps1
```

Editor の出力先は現在の CMake 設定では次の場所です。

```text
generated/outputs/Editor/Debug/Editor.exe
```

## Project Hub

Editor 起動直後は `Project Hub` が表示されます。

- `新規プロジェクト作成`: プロジェクト名と作成先ディレクトリを指定して新規プロジェクトを作成します。
- `プロジェクトを開く`: 既存プロジェクトのフォルダを選択して開きます。

既存プロジェクトとして認識されるには、プロジェクト直下に `cueproject.json` が必要です。
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

`cueproject.json` には `assetRoot`、`scriptRoot`、`startupScene`、Script のビルド設定、配布ビルド設定が保存されます。

## 基本画面

プロジェクトを開くと、Editor は `cueproject.json` の `startupScene` を読み込みます。
また、`assetRoot` を `Asset Browser` と `Inspector` に設定し、`scriptRoot` から `GameScript.dll` の読み込みを試みます。

主なビューは次の通りです。

- `Asset Browser`: `Assets` 配下のフォルダとファイルを表示します。`Refresh` で再読み込みします。
- `ヒエラルキー`: 現在の Scene にある GameObject を一覧表示します。
- `Inspector`: 選択中 GameObject の Component を編集します。`ScriptComponent` もここで設定します。
- `GameView`: ゲーム画面を表示します。
- `DebugView`: デバッグカメラ用の表示です。
- `Statistics`: フレームや Editor 更新処理の計測情報を表示します。
- `Script Build Output`: GameScript の configure、build、reload、artifact、log を確認します。

## メニュー操作

### ファイル

- `シーンを保存`: 現在の Scene を保存します。ショートカットは `Ctrl+S` です。
- `シーンを再読み込み`: 現在の Scene をディスクから再読み込みします。

### 編集

- `3D > カメラを追加`: Camera GameObject を追加します。
- `3D > オブジェクトを追加`: 3D StaticMesh GameObject を追加します。
- `2D > オブジェクトを追加`: 2D Sprite GameObject を追加します。
- `メインカメラ`: Scene 内の Camera からメインカメラを選択します。
- `Undo`: 直前の編集コマンドを戻します。ショートカットは `Ctrl+Z` です。
- `Redo`: 戻した編集コマンドをやり直します。ショートカットは `Ctrl+Y` または `Ctrl+Shift+Z` です。

### ナビゲーション

- `Scene NavMesh を Bake`: 現在の Scene の NavMesh を Bake します。Play 中は実行できません。
- `Debug Window`: Navigation のデバッグウィンドウを表示します。

### 実行

- `Play`: 現在の Scene で Play を開始します。
- `Stop`: Play を停止します。
- `Exit`: Play を終了して Editor 状態へ戻ります。

Script のビルドや再読み込み中は Play 操作が無効になります。

### ビルド

- `GameScript ビルド構成`: `Debug`、`RelWithDebInfo`、`Release` から GameScript のビルド構成を選択します。
- `GameScript 読み込み構成`: Editor が読み込む GameScript の構成を選択します。
- `GameScript backend`: `CMake` または `VisualStudio` を選択します。
- `ゲーム配布ビルド構成`: 配布用ビルドの構成を選択します。
- `ゲーム配布 backend`: 配布用ビルドの backend を選択します。
- `GameScript を再読み込み`: 現在の `scriptRoot` から GameScript を再読み込みします。
- `GameScript をビルド`: GameScript をビルドし、成功後に再読み込みします。
- `ゲーム Release ビルド`: Engine 側の `CueApp` とプロジェクト側の `Game` / `CueApp` をビルドし、配布フォルダを作成します。
- `ゲーム Release ビルドフォルダを開く`: `gameReleaseOutputRoot` の出力先を Shell で開きます。
- `GameScript を追加`: `Assets/Scripts` に Script の `.h` と `.cpp` テンプレートを作成します。
- `GameScript solution を開く`: `scriptRoot` の CMake preset から Visual Studio solution を開きます。
- `Editor にデバッガをアタッチ`: Visual Studio から現在の Editor プロセスへアタッチします。
- `Script Build Output`: Script Build Output ウィンドウの表示を切り替えます。

## GameScript Build Output

`Script Build Output` では、最後に実行した GameScript 操作の結果を確認できます。

- 成否と summary
- Exit Code
- configure を実行したかどうか
- `Open Solution`
- `Attach Editor`
- `Clear Output`
- Configure Log
- Build Log
- stage ごとの command と output
- build message
- artifact path

ビルドに失敗した場合は、このウィンドウの message と log path を先に確認します。

## 配布ビルド

`ゲーム Release ビルド` は、プロジェクトの `gameReleaseOutputRoot` に配布用フォルダを作成します。
既定値は次の通りです。

```json
{
  "gameReleaseOutputRoot": "Builds/Windows"
}
```

現在の処理では、`CueApp.exe`、`EngineResources`、`config`、`dxcompiler.dll`、`dxil.dll`、`Assets`、`cueproject.json` を配布フォルダへ集めます。
Sound asset は配布コピー前に cook されます。

