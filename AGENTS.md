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

# CueEngine コーディング規約 (2026/04/07)

## 目的
実行速度、メモリ効率、安全性、メンテナンス性を同水準で保つ。

## 適用範囲
プロジェクトに含まれるすべてのプログラミング言語が対象。

## 自動整形
- **clang-format** 使用
- スタイル: **Allman**

---

## 命名規則

| 種別 | 規則 | 例 |
|------|------|-----|
| 型 (class/struct/enum) | PascalCase | `RenderEngine` |
| 型名エイリアス | lowerCamelCase | `float4x4` |
| 関数 | snake_case | `get_device()` |
| 引数 | a_ + camelCase | `a_deviceContext` |
| 変数 | camelCase | `frameCount` |
| メンバ変数 | m_ + camelCase | `m_frameCount` |
| 定数 | k_ + camelCase | `k_maxBufferSize` |
| bool | 疑問形 | `isEnabled`, `hasData` |

### 備考
- 頭字語は最初のみ大文字（例: `Http`, `Xml`）

### 命名の内容規則
- 名前はそのスコープで必要な差分だけを書く
- 型名、クラス名、名前空間名など、外側の文脈をメンバ名や変数名に繰り返さない
- 型で分かる情報は名前に含めない
- 名前は 対象 + 役割/用途 + 属性 を基本形とする
- 同じ意味カテゴリの語を重ねない
- 4 語以上になる名前は原則禁止とし、必要なら構造の見直しを優先する
- 同形のメンバが 3 個以上並ぶ場合は、命名で説明せず共通構造体への分離を検討する
- 一般語より具体語を優先する

### 命名の禁止事項
- 型情報を名前に含めることを禁止する
- 例: String, StringView, Ptr, Ref, Array, Vector
- 上位文脈の再記述を禁止する
- 例: StaticMeshPoolDesc のメンバに staticMeshPool を再度含める
- 同義語の重複を禁止する
- 例: bufferResource, nameString, viewHandle
- 「親切そうだから」という理由で語を追加することを禁止する

---

## ファイルとフォルダ

- ファイル、フォルダ名は**大文字始まり**（PascalCase）
- フォルダ構成:
  ```
  Projects/
  ├── Include/    # ヘッダファイル
  ├── Source/     # ソースファイル
  └── Shader/     # シェーダファイル
  ```

---

## ヘッダ規約

- `using namespace` **禁止**
- インクルードガードは `#pragma once` で統一
- インクルード順序:
  1. 自分のヘッダ
  2. 標準ライブラリ
  3. 外部ライブラリ
- 前方宣言を活用し、依存を最小化

---

## 型・所有権・エラー処理

### 型
- メンバ変数はパディングを最小限にするため、サイズの大きい型を先に宣言

### 所有権
- 基本は `std::unique_ptr`
- 共有が必要な場合のみ `std::shared_ptr`
- 生ポインタは**非所有**の意味でのみ使用（所有目的での使用禁止）

### エラー処理
- 例外は使用しない（戻り値でエラーを返す）
- エラーを握りつぶさない
- `assert` はハードウェア障害など**再処理不可能**な場面でのみ使用
- 処理は安全に実行できることを確認してから実行
- 安全でない場合は通知・ログを出力し、処理を取り消す

---

## コメント・ドキュメント

- コメントは **Why（なぜ）** を書く（Whatはコードで表現）
- 公開APIのみ **Doxygen形式** で記述
- 関数は処理の流れや内容を分かりやすいようにする。話し言葉は避ける。
- 1)や2)などは使用しない
- include のコメントは // === *** === で区切る
- 複数の関数のコメントは // --- *** --- で区切る
- 単体は // *** で区切る
- 英語と日本語は半角スペースで区切る
- 言語:
  - C++/その他: **日本語**
  - `.hlsli` (HLSL): **英語**

---

## 外部ライブラリ・ファイル

- 警告はすべて無効化
- 書き換え**禁止**
- 同ソリューション内でプロジェクトは分離
- ソースファイルのみの場合はそのまま使用

---

## 禁止事項

| 項目 | 理由 |
|------|------|
| ヘッダでのマクロ乱用 | 定数は `constexpr` を使用 |
| 暗黙的型変換 | 意図しない変換を防止 |
| `new`/`delete` の直書き | スマートポインタを使用 |
| グローバル可変変数 | 状態管理の複雑化を防止 |

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
