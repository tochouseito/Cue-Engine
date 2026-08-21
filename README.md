# CueEngine

CueEngineは、C++を中心に一から再設計・再実装するモジュール型3Dゲームエンジンです。
新CueEngineの正本は`Rebuild`ブランチです。

旧CueEngineのコードは、問題設定、技術的知見、性能計測、失敗事例を調査するための参考資料として扱います。
新実装へ直接コピー、移植、改名は行いません。

`M00 Repository Foundation`では、再現可能なConfigure、Build、Test、CI、開発手順の基盤を整備しました。
M00の完了証跡は[Repository Foundation completion evidence](Docs/Milestones/M00-Repository-Foundation.md)に集約しています。
`M01 Runtime Foundation`では、PlatformとRenderingから独立したFoundation、Result、Assert、Log、Fatalと、その依存方向・公開Header・エラー経路を検証するTestを整備しました。

## Repository Layout

Engine が所有する Source の正本は `Engine/Source` です。
現在の最小構成は次のとおりです。

```text
Engine/
    Documents/
    Source/
        CueBuildProbe/
            Main.cpp
        Foundation/
            Private/
            Public/Cue/Foundation/
    Tests/
        Foundation/
```

Repository Root は CMake の Build 入口、License、Repository 設定などに使用します。
確定したModule境界と診断方針は、Project Policyに記載したADRを正本とします。

## Requirements

- Windows x64
- Visual Studio 2026（Desktop development with C++）
- CMake 4.2.0以上

## Clean Checkout

新規Checkoutでは、既定の`Rebuild`ブランチをCloneしてRepository Rootへ移動します。

```powershell
git clone https://github.com/tochouseito/CueEngine.git
Set-Location CueEngine
git branch --show-current
```

最後のコマンドが`Rebuild`を出力することを確認してから、以下のConfigure、Build、Testを順に実行します。

## Configure

```powershell
cmake --list-presets
cmake --preset windows-vs2026
```

生成物は`out/build/windows-vs2026`へ出力されます。

## Build

```powershell
cmake --build --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-development
cmake --build --preset windows-vs2026-release
```

Foundation専用Test TargetだけをBuildする場合は、次のようにTargetを指定します。

```powershell
cmake --build out/build/windows-vs2026 --config Debug --target Cue.Foundation.Tests
```

各構成の最小 C++ Target は次のコマンドで実行できます。

```powershell
out/build/windows-vs2026/bin/Debug/CueBuildProbe.exe
out/build/windows-vs2026/bin/Development/CueBuildProbe.exe
out/build/windows-vs2026/bin/Release/CueBuildProbe.exe
```

`Development` は最適化とデバッグ情報を有効にし、`NDEBUG` を定義しません。
First-party MSVC Target は `/utf-8` を使用し、Source と Execution Character Set を UTF-8 に固定します。

## Test

各構成を Build した後、対応する CTest Preset を実行します。

```powershell
ctest --preset windows-vs2026-debug
ctest --preset windows-vs2026-development
ctest --preset windows-vs2026-release
```

`CueBuildProbe.Smoke`に加えて、FoundationのResult、Assert、Log、Fatal、緊急終了、公開Header単体Compile、Module依存方向を検証します。
Foundation Testだけを実行する場合は、CTest Labelを指定します。

```powershell
ctest --preset windows-vs2026-debug -L Foundation
```

## Project Policy

- [Project rules](AGENTS.md)
- [Rebuild decision](Docs/Decisions/0001-rebuild-from-first-principles.md)
- [C++ build policy](Docs/Decisions/0003-cpp-build-policy.md)
- [Runtime Foundation module boundaries](Docs/Decisions/0004-runtime-foundation-module-boundaries.md)
- [Error, assert, and log policy](Docs/Decisions/0005-error-assert-log-policy.md)
- [M00 Repository Foundation completion evidence](Docs/Milestones/M00-Repository-Foundation.md)
- [M01 Runtime Foundation completion evidence](Docs/Milestones/M01-Runtime-Foundation.md)
