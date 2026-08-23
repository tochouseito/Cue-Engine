# CueEngine

CueEngineは、C++を中心に一から再設計・再実装するモジュール型3Dゲームエンジンです。
新CueEngineの正本は`Rebuild`ブランチです。

旧CueEngineのコードは、問題設定、技術的知見、性能計測、失敗事例を調査するための参考資料として扱います。
新実装へ直接コピー、移植、改名は行いません。

`M00 Repository Foundation`では、再現可能なConfigure、Build、Test、CI、開発手順の基盤を整備しました。
M00の完了証跡は[Repository Foundation completion evidence](Docs/Milestones/M00-Repository-Foundation.md)に集約しています。
`M01 Runtime Foundation`では、PlatformとRenderingから独立したFoundation、Result、Assert、Log、Fatalと、その依存方向・公開Header・エラー経路を検証するTestを整備しました。
`M02`から`M05`では、Windows Window、D3D12 Backend、Swap Chain、Frame同期、固定色Clear、Resize／Minimize／Restore／Closeを段階的に統合しました。

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
        Platform/
            Public/Cue/Platform/
            Windows/
        RHI/
            Public/Cue/RHI/
            D3D12/
        RuntimeHost/
            Main.cpp
    Tests/
        Foundation/
        Platform/
        RHI/
        RuntimeHost/
```

Repository Root は CMake の Build 入口、License、Repository 設定などに使用します。
確定したModule境界と診断方針は、Project Policyに記載したADRを正本とします。

## Requirements

- Windows x64
- Visual Studio 2026（Desktop development with C++）
- CMake 4.2.0以上
- Windows SDK 10.0.26100.0以上
- DirectX 12対応GPU。自動検証ではWARPも使用できます

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

## Run RuntimeHost

Debug構成のRuntimeHostを起動すると、Client Areaを固定色でClearしてPresentし続けます。WindowのClose操作でGPU Workを完了させ、Presentation、Backend、Windowの順に停止してExit Code 0で終了します。

```powershell
out/build/windows-vs2026/bin/Debug/CueRuntimeHost.exe --width 1280 --height 720
```

300 FrameのHardware／WARP自動検証は次のコマンドで実行できます。

```powershell
out/build/windows-vs2026/bin/Debug/CueRuntimeHost.exe --render-smoke hardware --width 1280 --height 720
out/build/windows-vs2026/bin/Debug/CueRuntimeHost.exe --render-smoke warp --width 1280 --height 720
```

`BUILD_TESTING=ON`のBuildでは、実Window Eventを経由する50回のResize／Minimize／RestoreとClose優先を`--resize-smoke <hardware|warp>`で検証できます。このModeとWindows Window Lifecycle Probeは製品用`BUILD_TESTING=OFF` Buildには含まれません。

DebugとDevelopmentはD3D12 Debug Layer、InfoQueue、DREDを有効化します。Releaseは設計どおりこれらの診断を無効化するため、診断専用CTest 4件をSkipします。

## Current Limitations

- Windows x64、Visual Studio 2026、DirectX 12だけを検証済みです
- 単一Window、単一Graphics Queue、単一Thread、2 Back Buffer、VSync有効が現在の範囲です
- Device Recovery、Fullscreen、HDR、Multi-window Renderingは未実装です
- Shader、Pipeline、Draw Call、Depth Buffer、FrameGraph、Asset、ECS、Editor、配布PackageはM05の範囲外です
- 現在の可視出力は固定色Back Buffer Clearです。描画性能の改善は測定していません

## Project Policy

- [Project rules](AGENTS.md)
- [Rebuild decision](Docs/Decisions/0001-rebuild-from-first-principles.md)
- [C++ build policy](Docs/Decisions/0003-cpp-build-policy.md)
- [Runtime Foundation module boundaries](Docs/Decisions/0004-runtime-foundation-module-boundaries.md)
- [Error, assert, and log policy](Docs/Decisions/0005-error-assert-log-policy.md)
- [M00 Repository Foundation completion evidence](Docs/Milestones/M00-Repository-Foundation.md)
- [M01 Runtime Foundation completion evidence](Docs/Milestones/M01-Runtime-Foundation.md)
- [M04 D3D12 Frame Infrastructure completion evidence](Docs/Milestones/M04-D3D12-Frame-Infrastructure.md)
- [M05 Render Target Clear completion evidence](Docs/Milestones/M05-Render-Target-Clear.md)
