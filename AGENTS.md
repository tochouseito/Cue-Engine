# AGENTS.md (Cue Engine)

## 目的（このリポジトリで Codex にやらせること）
- 機械的リファクタ（命名変更、ファイル分割、include整理、重複排除）
- ドキュメント更新（設計メモ、API説明、規約の追記）
- コードレビュー/設計レビュー（危険箇所の指摘、改善案の提示）
- バグ修正（ただし “根拠なく推測で直す” のは禁止。必ず再現・ログ・原因→修正）

## 言語
- すべて日本語で回答・コメント。
- 例外：HLSL の識別子や外部API名は英語のまま。

## ビルド（必須）
- ビルド方式：MSBuild で `Cue Engine.slnx`
- 指定：`Debug|x64`
- NuGet は使わない（restore もしない）
- 変更を入れたら、原則として **`scripts/codex_build.ps1` を実行して通す**。
  - 実行コマンド: `pwsh -NoProfile -File scripts/codex_build.ps1`

## A/B/C（MCP 3種の役割分担）
A) **Serena**: “コードベース検索/参照” 専用（どのファイルを触るべきか、依存関係、参照箇所の列挙）
B) **Sequential Thinking**: “設計検討の思考手順” 専用（アーキ案の比較、トレードオフ、決定基準の明確化）
C) **Memory Bank**: “決定事項の固定化” 専用（決めたことを後で蒸し返さないための要点保存）

運用ルール（重要）:
- **デフォルトは Serena だけ**使う（＝普段のコーディング支援）。
- 設計検討が必要なときだけ Sequential Thinking を使う。
- 結論が出たら Memory Bank に「決定事項・理由・影響範囲」を保存してから実装に入る。
- MCP を乱用して “ツールを回してるだけ” になったら即中止して、通常の調査＋提案に戻る。

## コーディング規約（このファイルが正）
### 1) フォーマット
- clang-format 使用
- Allman（波括弧は改行）

### 2) 命名（厳守）
- 型: PascalCase（例: `LinearArena`, `FixedBlockPool`）
- 型名エイリアス: lowerCamelCase（例: `float4x4`）
- 関数: snake_case（例: `create`, `output_debug_string`）
- 変数: camelCase
- メンバ: `m_` プレフィックス（例: `m_nodes`）
- `using namespace` 禁止
- 頭字語は最初のみ大文字（例: `Http`, `Xml`）

### 3) 制御構文
- if/for/while/switch など **必ず `{}` を付ける**（単行でも例外なし）

### 4) 例外・エラー処理
- 例外禁止
- 失敗は戻り値（Result / ErrorCode 等）で返す
- `assert` は “不変条件の破綻” のみに限定

### 5) 所有権
- 所有: `std::unique_ptr` 優先
- 共有所有が必要な場合のみ `std::shared_ptr`
- 生ポインタは **非所有参照のみ**
- DLL境界など共有は “寿命の責任者” を明確化する

### 6) 関数内コメント（必須）
- 関数内の処理フローは `// 1) ...` `// 2) ...` の番号付きで書く
- “何をしているか” ではなく “なぜそうするか” を優先

### 7) 禁止事項
- ヘッダでのマクロ乱用（定数は `constexpr`）
- 暗黙的型変換（意図しない変換防止）
- `new`/`delete` の直書き（スマートポインタ使用）
- グローバル可変変数（状態が壊れる）

### 8) 外部ライブラリ
- 外部ライブラリの中身は書き換え禁止（必要ならラップ側で対応）
- 同ソリューション内ではプロジェクト分離

# Cue Engine：ここまでの合意まとめ（設計メモ）

## 0. 前提と目的

* **開発環境は Windows のみ**。
* **Editor は Windows 専用**でよい。
* ただし **Engine で作るゲーム（Runtime）はマルチプラットフォーム対応**を目指す。
* 目標は「綺麗な抽象化」ではなく、**移植不能になる依存漏れを防ぎつつ、各APIの性能/機能を最大限に出せる構造**。

---

## 1. 大枠アーキテクチャ（Editor と Runtime を分離）

### 1.1 コンポーネント

* **Runtime（移植対象）**

  * Core / GameCore / Asset / Scene / Job / Physics / (RHI抽象 + backend)
  * OS依存を持たない（持つのは Platform 抽象のみ）
* **Editor（Windows専用）**

  * Win32/ImGui/ツール群/アセット管理/ビルド司令塔
  * Runtime を生成・起動・デバッグ・ビルド指揮する

### 1.2 依存方向の鉄則

* Runtime 上層（Core/GameCore）から **Win32型（HWND 等）や Windows.h を見せない**。
* OS依存は **platform_win** の実装に閉じ込める。

---

## 2. ループは Platform、Engine は tick するだけ

### 2.1 合意（重要）

* **メインループ（メッセージポンプ、ライフサイクル、時間管理）は Platform/Host 側が所有**。
* **Engine は `tick()` されるだけ**。

### 2.2 意図

* Windows / Android / iOS などでライフサイクルが異なっても、
  **Engine 側に「OSの流儀」を侵入させない**ため。

### 2.3 役割分担（概念）

* Host（platform_* / app_host）

  * 起動/終了、イベント処理、入力収集、タイムステップ決定
  * `engine.tick(frame_context)` を毎フレーム呼ぶ
* Engine

  * Update / Render を実行（OS依存は Platform 抽象経由でのみ使用）

---

## 3. 開発/リリースのバイナリ構成

### 3.1 開発構成（ホットリロード）

* Engine：DLL（または Editor 静的リンクでも可、ただし後述の案Bを守る）
* モジュール：LIB
* Editor：EXE
* GameScript：DLL（ホットリロード用）

### 3.2 リリース構成（単体配布）

* Game.lib + Engine.lib + Modules.lib → **App.exe に静的リンク**
* DLL依存を減らし、配布安定・審査/環境差の影響を減らす

---

## 4. GameScript 連携：案B（APIテーブル + ハンドル）

### 4.1 採用方針（案B）

* **Host（Editor/App）が Engine を保持**し、Game は Engine に直接リンクしない。
* Host が **EngineApi（関数ポインタテーブル）**を組み立て、Game 側へ渡す。
* ゲーム側は **ハンドル**でやり取りし、Engine 内部オブジェクトを直接保持しない。

### 4.2 なぜ案Bか（致命傷の回避）

* Editor.exe と GameScript.dll の両方に Engine.lib をリンクすると、
  **Engine の静的状態が二重化**して破綻しやすい（別世界のシングルトン/キャッシュが生まれる）。
* DLL境界での **C++ ABI/所有権/例外**は事故率が高い。

### 4.3 リリース（App.exe）ではどうするか

* リリースでは `Game.lib` を App.exe に静的リンクするが、
  **APIテーブル渡し/ハンドルの契約はそのまま維持**する。
* 違いは「関数の取り出し方」だけ。

  * 開発：`LoadLibrary/GetProcAddress` で `GameExports` を取得
  * リリース：静的リンク済み関数を直接参照して `GameExports` を構築

### 4.4 推奨：入口API（例）

* `game_get_abi_version()`
* `game_get_exports(GameExports* out)`
* `game_create(const EngineApi* api, GameHandle* out)`
* `game_update(GameHandle, float dt)`
* `game_render(GameHandle)`
* `game_destroy(GameHandle)`

---

## 5. DLL/境界のルール（Windowsでも必須）

* **所有権を境界で跨がせない**（返すのはハンドル/ID）。
* どうしても跨ぐなら、

  * 「確保した側が解放」または
  * Engine 側 allocator API を公開して統一。
* **例外を境界で跨がない**。
* DLL境界は原則 **C API + POD + length**。
* 文字列は UTF-8 を基本とし、変換は Platform 側で行う。

---

## 6. グラフィックス抽象の結論（性能/機能を捨てない）

### 6.1 できるが、やり方を間違えると死ぬ

* D3D12 / Vulkan / Metal は大枠は似ているが、
  **バインディング/同期/レンダーパス等の前提差が大きい**。
* 「全部同じ呼び出しで統一」は、どこかで **性能か機能が必ず死ぬ**。

### 6.2 採用方針

* **薄いRHI（共通の配管のみ）**

  * Device / Queue / CommandList
  * Buffer / Texture / Sampler
  * Pipeline（中身は backend 依存）
  * Swapchain
  * 同期プリミティブ
  * バリアは“宣言”まで（実装は backend lowering）
* **Extensions（脱出ハッチ）**

  * Bindless、MeshShader、RayTracing、Async最適化などは **拡張として別口**
  * `supports(X)` で分岐し、フォールバックも用意
* **RenderGraph/FrameGraph（宣言）→ backend lowering（最適化）**

  * 上位は「読む/書く/依存」だけを宣言
  * backend が各APIに最適な形で落とす

---

## 7. Engineプロジェクト設計（ターゲット分割と責務）

> 目的：**依存漏れを防ぎつつ、バックエンド別に最適化できる**構造にする。

### 7.1 推奨ターゲット一覧（最小）

* **CueCore (lib)**

  * OS/GPU非依存の基盤（型、結果型、ハンドル、ログ抽象、データ構造など）
  * 禁止：Windows.h / D3D12/Vulkan/Metalヘッダ / ImGui
* **CuePlatform (lib)**

  * Platform抽象（例：IPlatform/IFileSystem/ITimer 等）と共通部
  * 依存：CueCore
* **CuePlatformWin (lib)**

  * Windows実装（Win32、ウィンドウ、入力、タイマー等）
  * 依存：CuePlatform + CueCore
* **CueGraphicsCore (lib)**

  * 薄いRHI抽象（IRhiDevice/CommandList/Resource/Pipeline 等）
  * 依存：CueCore
* **CueGfxD3D12 (lib)**

  * D3D12バックエンド実装（PSO/RS/Descriptor/Queueなど）
  * 依存：CueGraphicsCore + CuePlatform（surface等に必要な最小接点）+ CueCore
* （将来）**CueGfxVulkan / CueGfxMetal (lib)**

  * Vulkan/Metalバックエンド
* **CueEngine (lib)**

  * 統合ロジック（init/tick/shutdown、シーン/ECS/レンダリングの統括）
  * 依存：CueCore + CuePlatform + CueGraphicsCore
  * 重要：OS/API名で分岐しない。分岐はCapabilities/Extensionsのみ。
* **CueRuntime (INTERFACE or lib)**（任意：便利のための“束ね役”）

  * CueCore/CuePlatform/CueGraphicsCore/CueEngine をまとめてリンクするだけ

### 7.2 アプリ（Host）ターゲット

* **CueEditor (exe, Windows)**

  * WinMain/ループ所有者。ImGui等のEditor機能。
  * Engineを初期化して tick する。GameScriptのロード/ホットリロード。
* **CueApp (exe, Windows)**

  * 製品実行用Host。WinMain/ループ所有者。Engineをtick。
* （将来）**CueTool* (exe, Console)**

  * パッカー/ビルダー/アセット変換などのCLI。

### 7.3 Game側（案B：APIテーブル + ハンドル）

* **GamePlugin (開発：dll / リリース：lib)**

  * Engineにリンクしない（原則）。Hostから渡されたEngineApiだけで動く。
  * 入口：GameExports（create/update/render/destroy等）

### 7.4 依存関係（固定）

* CuePlatform → CueCore
* CueGraphicsCore → CueCore
* 各backend（D3D12等）→ CueGraphicsCore (+ 必要最小のCuePlatform)
* CueEngine → CueCore + CuePlatform + CueGraphicsCore
* Host（Editor/App）→ CueEngine + （対象プラットフォーム実装/対象backend）

> **循環依存が出たら設計ミス**。その場で分割/責務を修正する。

### 7.5 分岐の置き場所

* **OS/プラットフォーム分岐**：Hostのリンク構成（ターゲット分割）で解決
* **GPU機能分岐**：Engine内は `Capabilities/Extensions` で分岐（OS/API名は見ない）

---

## 8. ビルド司令塔としての Editor（IDE依存を避ける）

* Editor は Windows 専用でも、**各プラットフォームのツールチェーンをコマンドで叩く**設計にする。
* Windows：MSBuild（または CMake→MSBuild）
* Linux：CMake→Ninja/Make（CI/WSL/リモート）
* Android：NDK+CMake（ネイティブ）→ Gradle（パッケージング）
* iOS：xcodebuild（Mac必須。Editorはリモート実行のトリガー）

---

## 8. 実行形態の整理（Host の切り替え）

* 開発（Editor）：Editor が Host として game をロードし、EngineApi を渡して駆動
* リリース（App）：App が Host として EngineApi を渡して game を駆動
* **Host が変わっても契約（APIテーブル/ハンドル/入口関数）は同一**

---

## 9. 次にやること（優先順位）

1. **案Bの境界契約を確定**（EngineApi / GameExports、所有権、例外、文字列）
2. Host ループ → Engine tick の形を実装し、OS依存を Host/Platform に隔離
3. RHI を薄く固定し、拡張（Extensions）と RenderGraph lowering の逃げ道を用意
4. Editor の Build Pipeline をコマンド駆動で作る（Windows→Android→Linux→iOSの順が現実的）
