---
sidebar_position: 0
title: 環境要件
---

# 環境要件

Cue Engine の Renderer / Runtime 開発は Windows を前提にしています。
ここではローカル開発、docs-site、Release package 作成に必要なツールをまとめます。

## Engine / Renderer 開発

必須:

- Windows
- Visual Studio 2026 または Visual Studio Build Tools 2026
- MSVC / MSBuild
- CMake 4.2.0 以上
- PowerShell 7 または Windows PowerShell
- Git
- vcpkg
- DirectX 12 対応 GPU / Driver

現在の GitHub Actions は `windows-2025-vs2026` と `Visual Studio 18 2026` を使います。
ローカルの `scripts/codex_build.ps1` は `MSBuild.exe` を探して `Cue Engine.slnx` を `Debug|x64` でビルドします。

```powershell
pwsh -NoProfile -File scripts/codex_build.ps1
```

`scripts/codex_build.ps1` は次の順で MSBuild を探します。

- Visual Studio 18 Insiders
- Visual Studio 17 Enterprise / Professional / Community
- `vswhere.exe` で見つかる Visual Studio

## CMake と vcpkg

GameScript、配布ビルド、package 作成は CMake preset を使います。
プロジェクト生成側の `CMakeLists.txt` は `cmake_minimum_required(VERSION 4.2.0)` を出力します。

vcpkg を使う構成では `VCPKG_ROOT` が必要です。

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
```

`VCPKG_ROOT` は vcpkg の root directory を指します。
`$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake` が存在する必要があります。

GitHub Actions では workflow 内で vcpkg を clone して bootstrap するため、runner 側で事前設定する必要はありません。

## GameScript 開発

GameScript は CMake でビルドします。
Renderer 上の GameScript build backend は常に CMake です。

必須:

- CMake
- Visual Studio / MSVC
- `VCPKG_ROOT`
- PowerShell

Visual Studio 連携機能を使う場合は、追加で次が必要です。

- .NET SDK / `dotnet.exe`

`ビルド > GameScript solution を開く` と `ビルド > Renderer にデバッガをアタッチ` は、Visual Studio 連携用 tool を `dotnet run` で起動します。

## docs-site 開発

docs-site は Docusaurus です。

必須:

- Node.js 20 以上
- npm

初回は `docs-site` directory で依存関係を入れます。

```powershell
cd docs-site
npm ci
```

ローカル確認:

```powershell
npm run start
```

静的 build:

```powershell
npm run build
```

## Release package 作成

Renderer / Engine の配布 package は次の script で作成します。

```powershell
pwsh -NoProfile -File scripts/create_renderer_release_package.ps1 -Version 0.1.0
```

必須:

- Visual Studio / MSBuild
- CMake
- PowerShell
- vcpkg / `VCPKG_ROOT`
- Git

生成物は既定で `generated/release` に出力されます。

- `CueEngineRenderer_<version>_win-x64.zip`
- `CueEngineRenderer_<version>_win-x64.json`
- `CueUpdater_<version>_win-x64.exe`

GitHub Release へ publish する workflow は `.github/workflows/release-renderer.yml` です。
この workflow は `v*` tag push、または Actions の手動実行で動きます。
Release asset の作成と upload には GitHub Actions 上の `gh` CLI と `contents: write` 権限を使います。

## 配布された Renderer を使うだけの場合

配布 package から Renderer を使うだけなら、開発用 SDK 全体は不要です。
ただし、プロジェクト内で GameScript をビルドする場合は Visual Studio / CMake / vcpkg が必要です。

CueUpdater で install / update する場合は、GitHub Releases に次の asset が添付されている必要があります。

- package zip
- package manifest json
- CueUpdater exe

## 確認コマンド

環境確認には次のコマンドを使えます。

```powershell
git --version
cmake --version
pwsh --version
node --version
npm --version
dotnet --version
```

MSBuild は Visual Studio の install 状態で path が変わります。
`scripts/codex_build.ps1` を実行し、`MSBuild.exe が見つかりません` と出た場合は Visual Studio または Build Tools の C++ workload を確認してください。
