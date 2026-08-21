# CueEngine Agent Instructions

この文書は、CueEngineリポジトリ上で作業するCodexおよび他の開発エージェントが常に守るプロジェクトルールです。

## 1. Project Identity

CueEngineは、旧CueEngineで得た実装経験を材料にしながら、ゲームエンジン全体を`Rebuild`ブランチ上で一から再設計・再実装するプロジェクトです。

目標は、限定した対象領域において、Unity、Unreal Engine、Godotに匹敵する制作体験、拡張性、実行性能、診断能力を持つモジュール型3Dゲームエンジンを構築することです。

初期の想定は次のとおりです。

- C++中心
- Windows x64を最初のホスト
- DirectX 12を最初のGraphics API
- 3Dゲームを主対象
- CMakeを正式なビルド定義
- Engineが所有するSourceの正本は`Engine/Source`
- Runtime、Editor、Toolsを分離
- Source AssetとRuntime Assetを分離
- 将来、Unity、Unreal Engine、Godotでも利用できるクロスエンジンSDKを抽出

これらは初期方針であり、正確な言語標準、ツール最低バージョン、外部依存、ABIなどはADRで決定します。

## 1.1 Coding Rules

コード、Shader、Build設定を変更する前に、`Engine/Documents/CODING_RULES.md`を確認してください。

- 命名、インデント、Brace、所有権、コメント、Include順はCoding Rulesを優先します。
- Architecture、ABI、永続形式、Error Handlingなどの未決定事項は、Coding Rulesだけで確定せずADRを作成します。
- 既存コードとCoding Rulesが衝突する場合は、周辺コードの一貫性を優先し、最終報告で理由を書きます。

## 2. Authoritative Branch

- 新CueEngineの統合先は`Rebuild`ブランチです。
- 旧ブランチを変更しないでください。
- `Rebuild`へ直接作業コミットを行わず、原則としてIssue単位のブランチを使います。
- 現在のブランチ、追跡先、未コミット変更を作業前に必ず確認してください。

## 3. Legacy CueEngine Policy

旧CueEngineは参考資料です。旧コードを新実装へ直接コピー、移植、改名、部分抽出しないでください。

旧実装を参照する必要がある場合は、次を明記します。

1. 旧実装が解決していた問題
2. 旧実装の前提と制約
3. 旧実装で発生した問題や保守上の弱点
4. 新CueEngineの現在の要件
5. 新規に設計する解決策
6. 検証方法

旧版と同じ名前や構造を採用する場合も、現在の要件から妥当性を説明してください。

## 4. Scope Discipline

- 現在のIssueとMilestoneに必要な最小範囲だけを実装してください。
- 将来必要になりそうという理由だけで、先回りして機能を追加しないでください。
- 未確定事項を暗黙の実装判断で固定せず、Research IssueまたはADR候補として記録してください。
- `L`サイズのIssueへそのまま着手せず、`S`または`M`へ分割してください。
- 高リスクな公開API、永続形式、ABI、スレッド、GPU同期は、可能な限り先にResearch Issueを置いてください。

## 5. Architecture Invariants

次の規則は、Accepted ADRで変更されない限り守ってください。

- RuntimeはEditorへ依存しない。
- Editor固有型をRuntimeの公開APIへ出さない。
- Platform固有型をPlatform非依存の上位APIへ漏らさない。
- Source AssetをRuntimeが直接読み込む構造にしない。
- Asset参照は永続IDを基本とし、ファイルパスだけを恒久的な同一性にしない。
- Authoring Scene、Runtime World、Asset/Resource、Editor Documentを区別する。
- Object、Entity、Component、Asset、Serviceを一つの万能基底型へ統合しない。
- 永続データ形式にはVersionとMigration方針を持たせる。
- 所有権、寿命、スレッド、失敗時の挙動を公開APIごとに明示する。
- 無制限なGlobal Singletonを導入しない。
- DLLやPlugin境界へSTL型、C++例外、生ポインタ所有権を安易に公開しない。
- CMakeを唯一の正式なビルド定義とし、生成されたIDEプロジェクトを正本にしない。
- RendererはRuntime Worldへ直接密結合せず、抽出されたRender Dataを受け取る構造を目指す。
- Editor操作、Serialization、Hot Reload、Prefab Override、Undo/Redoは共通Schema/Reflection基盤を共有できるように設計する。
- ただし、高性能Runtime内部データまで必ずReflection Objectにする必要はない。

## 6. Reference Engine Policy

Unreal Engine、Unity、Godotは模倣対象ではなく、設計比較の参考です。

設計時は次を比較してください。

- Usability
- Runtime Performance
- Iteration Speed
- Extensibility
- Portability
- Data Safety
- Compatibility
- Diagnostics
- Testability
- Complexity

各エンジンの長所だけでなく、導入した場合の代償も記録してください。

## 7. Required Work Sequence

作業を始める前に、最低限次を行います。

1. `git status --short --branch`
2. 現在のブランチ確認
3. 関連する`AGENTS.md`、ADR、Milestone、Issueの確認
4. 関連ファイルと既存テストの調査
5. 変更対象、非対象、危険、検証方法の整理

ユーザーから実装開始を明示されていない場合、最初は調査と計画だけを行い、ファイルを変更しないでください。

## 8. Git Safety

明示的な依頼なしに、次を実行しないでください。

- `git reset --hard`
- `git clean`
- 強制Push
- Rebaseによる共有履歴変更
- ユーザーの未コミット変更の破棄
- Branch削除
- Commit
- Push
- Merge
- Tag作成
- Release作成

ユーザーの変更と自分の変更を混同しないでください。予期しない変更を発見した場合は、勝手に修正・削除せず報告してください。

## 8.1 GitHub Code Review

- GitHub 上の Pull Request Review、Inline Review Comment、Review Thread への返信は日本語で記述してください。
- コード識別子、API 名、外部 Tool 名、Error Message は英語のまま扱えます。

## 9. Validation

変更は、関連する検証に成功するまで完了扱いにしません。

適用可能なものを実行してください。

- CMake Configure
- Debug Build
- Development Build
- Release Build
- Unit Test
- Integration Test
- CTest
- Static Analysis
- Format Check
- `git diff --check`
- 実行確認
- エラー経路確認
- メモリ・GPU・スレッド検証

実行できなかった検証は、理由と未検証範囲を明記してください。警告や失敗を隠さないでください。

## 10. Completion Report

作業報告には次を含めます。

- 目的
- 実際に変更したファイル
- 主要な設計判断
- 実行したコマンド
- 検証結果
- 発見した既存問題
- 残っているリスク
- Scope外としてIssue化すべき事項
- 次に行うべき一つの作業

「完了率」より、Acceptance Gateを何個満たしたかを示してください。

## 11. Documentation

次の場合は文書を更新してください。

- 公開APIやモジュール境界を変更した
- 永続形式やVersion方針を決めた
- 新しい依存関係を導入した
- 重要な設計トレードオフを決めた
- 既存方針と異なる実装を採用した
- 旧CueEngineまたは他エンジンとの比較から正式判断を行った

重要な判断はADRへ残します。会話だけに設計を残さないでください。

## 12. Uncertainty

不明な事実を推測で断定しないでください。

特に次は端末上で確認してください。

- 現在のリポジトリ構成
- 使用中のCMake、Compiler、Visual StudioのVersion
- 現在のブランチとRemote
- 既存のCI
- 既存の外部依存
- 既存ファイルが空かどうか
- GitHub Issue番号
- GitHub Project Fieldの現在値

確認できない外部情報が設計に影響する場合は、仮定として明記してください。
