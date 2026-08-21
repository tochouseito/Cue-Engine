# CueEngine

CueEngineは、C++を中心に一から再設計・再実装するモジュール型3Dゲームエンジンです。
新CueEngineの正本は`Rebuild`ブランチです。

旧CueEngineのコードは、問題設定、技術的知見、性能計測、失敗事例を調査するための参考資料として扱います。
新実装へ直接コピー、移植、改名は行いません。

現在は`M00 Repository Foundation`として、再現可能なConfigure、Build、Test、CI、開発手順の基盤を整備しています。
Runtime、Renderer、Editorなどの機能実装はまだ対象外です。

## Repository Layout

Engine が所有する Source の正本は `Engine/Source` です。
現在の最小構成は次のとおりです。

```text
Engine/
    Documents/
    Source/
        CueBuildProbe/
            Main.cpp
```

Repository Root は CMake の Build 入口、License、Repository 設定などに使用します。
将来の Module 構成は個別の Research Issue と ADR で決定します。

## Requirements

- Windows x64
- Visual Studio 2026（Desktop development with C++）
- CMake 4.2.0以上

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

現在は `CueBuildProbe.Smoke` が最小 Target の起動と正常終了を検証します。

## Project Policy

- [Project rules](AGENTS.md)
- [Rebuild decision](Docs/Decisions/0001-rebuild-from-first-principles.md)
- [C++ build policy](Docs/Decisions/0003-cpp-build-policy.md)
