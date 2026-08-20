# ADR-0001: Rebuild CueEngine from First Principles

- Status: Accepted
- Date: 2026-08-20
- Decision Owners: CueEngine Project

## Context

旧CueEngineでは、DirectX 12、GPU Driven Rendering、FrameGraph、Editor、ECS、GameScript Hot Reloadなど、多くの技術実験を行いました。

一方、新しい目標はRuntime Rendererだけではなく、Editor、Asset Pipeline、Scripting、Platform、Build、Cook、Package、Profiler、Diagnostics、Pluginまで含む統合ゲームエンジンです。

旧実装を継ぎ足すと、過去の対象範囲に合わせたModule境界、所有権、Serialization、Global State、Platform依存、Tool構造が新しい設計を制約する可能性があります。

## Decision

新しいCueEngineは`Rebuild`ブランチ上で一から設計・実装します。

旧CueEngineのBranch、Commit、Source Codeは参考資料として利用できますが、新実装へ直接コピー、移植、改名しません。

旧実装から利用するものは次です。

- 解決していた問題
- 技術的知見
- Algorithm
- 性能計測
- Failure Case
- Debugging Experience
- Requirement

各Subsystemは、現在の要件に基づいてOwnership、Lifetime、Dependency、Threading、Serialization、Versioning、Error Handling、Testingを再定義します。

`Rebuild`ブランチを新CueEngineの正本とします。

## Consequences

### Positive

- 旧Module境界に縛られない
- Editor/Asset/Toolsを初期設計へ含められる
- Platform/RHI境界を再定義できる
- APIとData FormatへVersioningを導入できる
- Automated TestとDiagnosticsを基盤から組み込める

### Negative

- 旧機能を再実装する必要がある
- 初期は見た目の進捗が遅い
- 旧コードを短期的なShortcutに使えない
- 同じ技術問題を再検証するCostが発生する

## Enforcement

旧実装を参照したPRまたはResearch Issueでは、次を記録します。

1. Legacy Problem
2. Legacy Approach
3. Legacy Strengths
4. Legacy Problems
5. Current Requirements
6. New Design
7. Validation
