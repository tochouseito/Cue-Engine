# CueEngine Rebuild コーディング規約

## 目的

実行速度、メモリ効率、安全性、メンテナンス性を同水準で保つ。

この文書は旧CueEngineのコーディング規約を基礎に、RebuildのArchitecture Invariantsと未決定事項へ合わせて再構成した規約である。
旧実装固有のフォルダ構成や、ADRが必要なArchitecture判断は引き継がない。

## 適用範囲

プロジェクトに含まれるC++、HLSL、CMake、JSON、その他のプログラミング言語とBuild設定を対象とする。

## 自動整形

- `.clang-format`を使用する
- Brace StyleはAllmanとする
- インデントは半角スペース4個とする
- Tabは使用しない
- 継続行のインデントも半角スペース4個とする
- JSONとCMakeも半角スペース4個でインデントする

## 命名規則

| 種別 | 規則 | 例 |
|---|---|---|
| 型（class、struct、enum） | PascalCase | `RenderEngine` |
| 型名Alias | lowerCamelCase | `float4x4` |
| 関数 | snake_case | `get_device()` |
| 引数 | `a_` + camelCase | `a_deviceContext` |
| 変数 | camelCase | `frameCount` |
| structメンバ変数 | camelCase | `frameCount` |
| classメンバ変数 | `m_` + camelCase | `m_frameCount` |
| 定数 | `k_` + camelCase | `k_maxBufferSize` |
| bool | 疑問形 | `isEnabled`、`hasData` |

### 命名の内容規則

- 頭字語は最初のみ大文字にする。例: `Http`、`Xml`
- 名前は、そのScopeで必要な差分だけを書く
- 外側の型名やNamespace名をメンバ名へ繰り返さない
- 型で分かる情報を名前に含めない
- 名前は「対象 + 役割または用途 + 属性」を基本形とする
- 同じ意味Categoryの語を重ねない
- 4語以上になる名前は原則として避け、必要なら構造を見直す
- 同形のメンバが3個以上並ぶ場合は、共通構造体への分離を検討する
- 一般語より具体語を優先する

### 命名の禁止事項

- `String`、`StringView`、`Ptr`、`Ref`、`Array`、`Vector`などの型情報を名前へ含めない
- 上位Contextを名前へ再記述しない
- `bufferResource`、`nameString`、`viewHandle`のような同義語を重複させない
- 「親切そうだから」という理由だけで語を追加しない

## ファイルとフォルダ

- ファイル名とフォルダ名はPascalCaseを基本とする
- CMake、README、License、Tool設定など、外部Toolが要求する標準名は例外とする
- ファイル配置は現在のModule責務とAccepted ADRを優先する
- 旧CueEngineのフォルダ構成を理由に配置を決めない

## Header規約

- `using namespace`を使用しない
- Include Guardは`#pragma once`へ統一する
- Include順は、自分のHeader、標準Library、外部Libraryの順とする
- 前方宣言を活用し、依存を最小化する

## 型と所有権

- メンバ変数は、可読性と意味的なまとまりを損なわない範囲でPaddingを最小化する
- 所有権が一意な場合は`std::unique_ptr`を基本とする
- 共有所有が要件として必要な場合だけ`std::shared_ptr`を使用する
- 生Pointerは非所有参照としてのみ使用し、所有権を持たせない
- 公開APIでは生成、破棄、参照有効期間、Thread Safety、失敗時の状態を明示する

## Error Handling

- Errorを握りつぶさない
- 安全に実行できることを確認してから状態を変更する
- 安全に実行できない場合は、診断可能なErrorまたはLogを返し、処理を取り消す
- Exception、Result、Error Code、Assertの正式方針はM01からM03のResearch IssueとADRで決定する
- ADR決定前に、局所実装だけでプロジェクト全体のError方針を固定しない

## コメントとDocumentation

- コメントにはWhatではなくWhyを書く
- Header先頭には、必要な場合だけファイル責務を簡潔に書く
- すべてのC++関数は、公開範囲や定義場所を問わず、宣言または定義の直前に`/// @brief`形式で目的を書く
- Constructor、Destructor、Operator、`main`、内部補助関数、Test関数も`/// @brief`の対象とする
- Headerで宣言する関数は宣言側へ記述し、Headerに宣言を持たない関数は定義側へ記述する
- `/// @brief`には関数名の言い換えだけでなく、その関数が必要な理由または呼び出し側へ提供する結果を書く
- 話し言葉を避ける
- コメントは敬体を避け、文末の句点を使用しない
- 英語と日本語の間は半角スペースで区切る
- C++とその他のコメントは日本語を基本とする
- `.hlsli`のコメントは英語とする

区切りコメントが必要な場合は次を使用する。

```cpp
// === Include Group ===
// --- Function Group ---
// Single Purpose
```

## 外部Library

- 外部LibraryのSourceを直接書き換えない
- 可能な限りFirst-party Targetと分離する
- 外部Library由来のWarning抑制は、その外部TargetまたはInclude境界だけへ限定する
- First-party CodeのWarningを外部Library対策として無効化しない

## 禁止事項

| 項目 | 理由 |
|---|---|
| HeaderでのMacro乱用 | 定数や型安全な仕組みを優先する |
| 意図が不明な暗黙型変換 | 変換の目的と損失を明示する |
| 所有目的の`new`または`delete`直書き | RAIIと明示的な所有権を使用する |
| Global可変変数 | 状態、寿命、Thread Safetyを不明瞭にしない |
