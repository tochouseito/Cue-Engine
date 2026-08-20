# CueEngine

CueEngineは、C++を中心に一から再設計・再実装するモジュール型3Dゲームエンジンです。
新CueEngineの正本は`Rebuild`ブランチです。

旧CueEngineのコードは、問題設定、技術的知見、性能計測、失敗事例を調査するための参考資料として扱います。
新実装へ直接コピー、移植、改名は行いません。

現在は`M00 Repository Foundation`として、再現可能なConfigure、Build、Test、CI、開発手順の基盤を整備しています。
Runtime、Renderer、Editorなどの機能実装はまだ対象外です。

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
cmake --build --preset windows-vs2026-release
```

現段階ではRepository Configure基盤だけを定義しているため、Build対象のC++ Targetや実行ファイルはまだありません。
最小TargetとCTestは後続のM00 Issueで追加します。

## Project Policy

- [Project rules](AGENTS.md)
- [Rebuild decision](Docs/Decisions/0001-rebuild-from-first-principles.md)
