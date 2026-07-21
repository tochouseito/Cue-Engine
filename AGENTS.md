# AGENTS.md

## Language

- 回答は日本語で行う。
- コード識別子、API名、HLSL識別子、外部ツール名は英語のまま扱う。

## Project

- CueEngine は C++ / DirectX12 ベースの自作ゲームエンジン。
- Editor は Windows / ImGui 前提。
- Runtime は将来的にマルチプラットフォーム対応を目指す。
- MeshShader は将来候補。現在の最適化や検証は、明示されない限り従来パイプライン前提で考える。

## Coding Rules

コーディング作業を行う前に、必ず以下を確認して従う。

- `Engine\Documents\CODING_RULES.md`

運用:

- 命名、所有権、エラー処理、コメント、include順、ファイル配置は CodingRules を優先する。
- 既存コードと CodingRules が衝突する場合は、周辺コードの一貫性を優先し、最終報告で理由を書く。
- ルールに反する変更をした場合は、最終報告で必ず理由を書く。

## Build

- ビルド方式は MSBuild。
- ソリューションは `Cue Engine.slnx`。
- 既定構成は `Debug|x64`。
- NuGet restore は行わない。
- コード変更後は原則 `pwsh -NoProfile -File scripts/codex_build.ps1` を実行する。
- ビルドを実行できない場合は、理由と代替確認内容を書く。

## MCP Usage

### Default

- コード調査は Serena を優先する。
- CueEngine 固有のビルド、起動、ログ収集は `cue_engine` MCP を使う。
- 複数PC・別セッションで参照すべき決定事項は `cue_memory` MCP を使う。
- 外部ライブラリやAPIの現行仕様確認は Context7 を使う。
- GitHub上の Issue / PR / remote repository 状態確認は GitHub MCP を使う。
- MCP を使う目的が曖昧な場合は、通常のファイル調査と提案に戻る。

### CueEngine MCP

Use for:

- build
- run demo
- collect logs
- list scenes
- read project state

Do not use for:

- arbitrary shell command execution
- deleting files
- changing Git state

### CueMemory MCP

Use for:

- project decisions
- known pitfalls
- recurring CueEngine context
- investigation summaries

Do not store:

- tokens
- passwords
- private credentials
- full conversation dumps
- large logs
- capture files

### PIX / Nsight MCP

- PIX / Nsight MCP は、GPU capture、profiling、rendering investigation、performance validation が明示されたときだけ使う。
- PIX は D3D12 frame / resource / event inspection を優先する場面で使う。
- Nsight Graphics は NVIDIA GPU 側の解析を優先する場面で使う。
- パフォーマンス改善は、測定値、測定条件、比較対象なしに断定しない。
- Capture output は設定済み capture directory 内に限定する。
- 明示されない限り、CueEngine 以外の exe を対象にしない。

### CueEditor MCP

- CueEditor MCP は、起動中 CueEditor の確認や操作が明示されたときだけ使う。
- 初期方針は read-mostly。
- 選択、Debug view 切替、スクリーンショット取得は承認付きで許可する。
- Scene mutation、保存、Component削除、Entity一括操作は、ユーザーが明示した場合だけ行う。

## Rendering / D3D12 Review Focus

レンダリングコードを変更する場合は、特に以下を確認する。

- ResourceBarrier correctness
- DescriptorHeap ownership and lifetime
- RTV / DSV / SRV / UAV state
- ExecuteIndirect command stride and max count
- Fence and frame resource synchronization
- Upload / Default buffer lifetime
- CBV alignment
- UAV clear handles
- ImGui descriptor heap separation
- PIX marker consistency with FrameGraph pass names

## Performance Claims

- パフォーマンス改善を主張する場合は、測定条件、比較対象、数値を書く。
- PIX / Nsight / engine log なしに「速くなった」と断定しない。
- 測定できない場合は、測定不能だった理由と次に見るべき指標を書く。

## Git

- ユーザーが明示しない限り commit / push / branch 作成はしない。
- GitHub MCP は原則 read-only。
- Issue作成、PR作成、コメント投稿はユーザーが明示した場合のみ行う。

## Done

最終報告には以下を含める。

- 変更内容
- 実行した検証
- 実行できなかった検証
- 残るリスク

## Subagent Usage

Subagents are not used for small edits, simple questions, formatting changes, or local one-file fixes.

Use subagents when the task benefits from parallel investigation, independent review, or deeper analysis.

Preferred subagent roles:

### cue-explorer

Use `cue-explorer` for read-only CueEngine repository investigation.

Responsibilities:
- locate relevant systems, files, classes, and ownership boundaries
- trace call paths and data flow
- summarize existing design before implementation
- identify where a requested change should be made

Use when:
- the task spans multiple engine modules
- the implementation location is unclear
- DirectX 12, FrameGraph, Renderer, ECS, Editor, or GameScript interactions need tracing

Do not ask `cue-explorer` to edit files.

### cue-reviewer

Use `cue-reviewer` for CueEngine-specific code review.

Responsibilities:
- review correctness, maintainability, and regression risk
- check DirectX 12 resource lifetime, barriers, descriptor heap usage, synchronization, and command list behavior
- check GPU-driven rendering, ExecuteIndirect, Hi-Z, LOD, clustered lighting, and FrameGraph risks
- point out missing tests, missing validation, or profiling gaps

Use when:
- rendering or GPU behavior changes
- resource lifetime or synchronization is involved
- a large refactor is proposed
- the user asks for review

`cue-reviewer` should usually be read-only unless explicitly asked to patch.

### cue-bug-analyzer

Use `cue-bug-analyzer` for bug investigation and failure triage.

Responsibilities:
- inspect build errors, runtime logs, crash traces, validation errors, DRED/InfoQueue messages, and repro steps
- separate likely root causes from symptoms
- propose the smallest verification steps
- recommend minimal fixes before broad refactoring

Use when:
- build fails
- Codex/MCP startup fails
- Visual Studio, MSBuild, CMake, or PowerShell commands fail
- DirectX 12 validation, DRED, GPU crash, or rendering corruption occurs
- behavior changed after a recent patch

Prefer investigation first, then patch only after the likely cause is clear.

### oss-analyzer

Use `oss-analyzer` for open-source codebase or library analysis.

Responsibilities:
- inspect third-party repositories, samples, documentation, and implementation patterns
- summarize architecture and reusable ideas
- identify licensing or integration risks
- compare the external approach with CueEngine's current design

Use when:
- analyzing libraries such as meshoptimizer, DirectStorage samples, DX12 samples, RenderDoc-related tools, MCP servers, or other engine code
- checking whether an OSS implementation pattern is suitable for CueEngine
- reviewing how another project solved a rendering, tooling, or build problem

Do not copy code directly into CueEngine without license confirmation and design review.

## Subagent Invocation Policy

When the user explicitly says:
- "マルチで調査して"
- "サブエージェントを使って"
- "並列で見て"
- "レビュー役も立てて"

then use the appropriate subagents.

Default mapping:
- Repository investigation: `cue-explorer`
- Rendering / D3D12 / performance review: `cue-reviewer`
- Build failure / crash / runtime bug: `cue-bug-analyzer`
- External repository / OSS / sample analysis: `oss-analyzer`

For large tasks, prefer this flow:
1. `cue-explorer` investigates the relevant CueEngine code.
2. `cue-reviewer` reviews risks and constraints.
3. `cue-bug-analyzer` is used only if there is a concrete failure.
4. `oss-analyzer` is used only when external code or libraries are part of the task.

The main agent is responsible for final decisions, implementation, and integration.
Subagent output is advisory, not automatically accepted.
