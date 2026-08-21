# M01 Runtime Foundation Completion Evidence

## Purpose

この文書は、`M01 Runtime Foundation`でPlatformとRenderingから独立したRuntime基盤を確立し、公開Header、Result、診断経路、Module依存方向を継続的に検証できることを記録します。

検証日は2026-08-21です。Window、DirectX 12、ECS、Asset、EditorはこのMilestoneの対象外です。

## Scope Delivered

| Issue | Pull Request | Result |
| --- | --- | --- |
| [#31 Foundation境界](https://github.com/tochouseito/CueEngine/issues/31) | [#62](https://github.com/tochouseito/CueEngine/pull/62) | Foundation、Platform、RHI、RuntimeHostの責務・依存方向・所有権をADR化 |
| [#32 Error／Assert／Log方針](https://github.com/tochouseito/CueEngine/issues/32) | [#63](https://github.com/tochouseito/CueEngine/pull/63) | Recoverable Error、Assert、Fatal、Logの契約をADR化 |
| [#33 FoundationとResult](https://github.com/tochouseito/CueEngine/issues/33) | [#64](https://github.com/tochouseito/CueEngine/pull/64) | `Cue.Foundation`、Error、Result、緊急終了契約を実装 |
| [#34 Assert／Log／Fatal](https://github.com/tochouseito/CueEngine/issues/34) | [#65](https://github.com/tochouseito/CueEngine/pull/65) | Assert、同期Logger、Console Sink、Fatal経路を実装 |
| [#35 Foundation完了Gate](https://github.com/tochouseito/CueEngine/issues/35) | [#66](https://github.com/tochouseito/CueEngine/pull/66) | 専用Test Target、依存方向検査、完了証跡を整備 |

正式な設計判断は[ADR-0004](../Decisions/0004-runtime-foundation-module-boundaries.md)と[ADR-0005](../Decisions/0005-error-assert-log-policy.md)を正本とします。

## Foundation Test Gate

`Cue.Foundation.Tests`は、Foundationの全Test ExecutableをまとめてBuildする専用Targetです。CTestの`Foundation` Labelには次の15 Testを登録しています。

- ResultのValue／Error、変換、Move-only、Error Context
- Loggerの通常出力、Error付きRecord、複数Thread、Sink失敗
- Assertの成功、失敗、構成別有効化
- Fatal、再入、競合、Sink失敗、例外発生時の終了Code
- Error生成または変更失敗時の緊急終了Code
- 8個の公開Headerを個別Translation UnitでCompileするTest
- Foundation Targetの依存方向とPlatform固有Headerを検査するTest

Test専用Executableと終了Code固定Handlerは`Engine/Tests/Foundation`だけに置き、Production公開APIには追加していません。外部Test Frameworkも導入していません。

## Dependency Evidence

CMake Configure時に`Cue.Foundation`のTarget Propertyを検査し、次のいずれかが存在する場合はConfigureを失敗させます。

- `LINK_LIBRARIES`
- `INTERFACE_LINK_LIBRARIES`
- `MANUALLY_ADDED_DEPENDENCIES`
- `INTERFACE_LINK_OPTIONS`
- `INTERFACE_LINK_LIBRARIES_DIRECT`
- `INTERFACE_SOURCES`
- `SOURCES`内の`$<TARGET_OBJECTS:...>`

Privateな`LINK_OPTIONS`はDevelopment構成の`/DEBUG`を許可しますが、`/DEFAULTLIB:`または`.lib`によるLibrary注入を拒否します。

検査結果はBuild Treeの`Engine/Tests/Foundation/Cue.Foundation.Dependencies.txt`へ出力します。2026-08-21の検証結果は次のとおりです。

```text
Target: Cue.Foundation
LINK_LIBRARIES: <none>
INTERFACE_LINK_LIBRARIES: <none>
MANUALLY_ADDED_DEPENDENCIES: <none>
INTERFACE_LINK_OPTIONS: <none>
INTERFACE_LINK_LIBRARIES_DIRECT: <none>
INTERFACE_SOURCES: <none>
TARGET_OBJECT_SOURCES: <none>
LINK_OPTIONS: $<$<CONFIG:Development>:/DEBUG>
Forbidden platform link inputs: Windows SDK, DXGI, D3D12
Cycle review: no outgoing link, interface source, target object, or manual dependency edge
```

`Cue.Foundation.Dependencies`は、上記Reportに空の依存入力が記録されていることに加え、Foundation配下のC++ SourceとHeaderの直接Includeを選択中Windows SDKの`um`、`shared`、`winrt`、`cppwinrt`で解決し、Platform Headerに一致しないことを検査します。限定したHeader名の列挙ではなく、Windows SDKの実Include Directoryを基準にします。Foundationから出るTarget依存Edgeを許可しないため、Foundationを含むTarget依存循環も成立しません。

## Local Validation

Repositoryの作業Checkoutで次を実行しました。

```powershell
cmake --preset windows-vs2026
cmake --build out/build/windows-vs2026 --config Debug --target Cue.Foundation.Tests
ctest --preset windows-vs2026-debug -L Foundation
cmake --build --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-development
cmake --build --preset windows-vs2026-release
ctest --preset windows-vs2026-debug
ctest --preset windows-vs2026-development
ctest --preset windows-vs2026-release
ctest --test-dir out/build/windows-vs2026 -C Debug -R Cue.Foundation.Dependencies -V
git diff --check
```

結果:

- CMake Configure成功
- `Cue.Foundation.Tests`のDebug Build成功
- Foundation Labelは`15/15`成功
- Debug、Development、ReleaseのBuild成功
- 全3構成のCTestは、それぞれ`16/16`成功
- Dependency Testは、Link／Interface／Target Object／手動Target依存が空であることと、Windows SDK Platform Header解決検査の成功を出力
- Negative Injectionとして`INTERFACE_LINK_OPTIONS`の`/DEFAULTLIB:d3d12.lib`、`SOURCES`の`$<TARGET_OBJECTS:...>`、`processthreadsapi.h`の直接IncludeがそれぞれGateを失敗させることを確認
- `git diff --check`成功

## Clean Checkout Validation

GitHubへPushしたCommit `d37cd7f308fac07247cf1a23fb162b334b6a17e5`を別Directoryへ新規Cloneし、Configure、全3構成Build、全3構成CTestを再実行しました。

- Clone直後のBranchは`feature/Engine/M01/35-foundation-completion-gates`
- Clone直後のWorking TreeはClean
- CMake Configure成功
- Debug、Development、ReleaseのBuild成功
- 全3構成のCTestは、それぞれ`16/16`成功
- 検証後もTracked Fileの変更なし
- `git diff --check`成功

## Acceptance Gates

- [x] Foundation専用Test Targetが存在する
- [x] Resultと診断経路のPositive／Negative Testがある
- [x] 公開Header単体Compile Testがある
- [x] Foundation TargetがWindows SDK、DXGI、D3D12へLinkしないことを検査する
- [x] CMake Target依存の循環を検出またはReviewできる
- [x] Debug、Development、ReleaseのCTest Presetで成功する
- [x] Clean Checkoutで再現する
- [x] M01 Acceptance GateのEvidenceをこの文書へ集約した

## Known Risks and Deferred Work

- Windows SDK Headerの検査はFoundation Sourceに記述された直接Includeを対象とします。標準Library内部の推移的IncludeやToolchainの暗黙依存をCross-platform Compilerで検証する作業は将来のCI拡張範囲です。
- CIはWindows x64、Visual Studio 2026、MSVCだけを対象とします。Linux、macOS、他Compiler、Coverageは未検証です。
- Loggerは同期実行です。非同期Log、Category、Structured Field、Editor統合は実測要件と所有権設計を伴う別Issueで扱います。
- Windows Debugger Sink、Window Integration、GPU TestはM01のScope外です。
- Foundation APIはFirst-party Static Library境界向けであり、安定ABIまたはPlugin ABIではありません。

## Next Work

次のMilestoneでは、Accepted ADRとResearch Issueを先行させたうえで、Platform契約とWindows実装の最小範囲を扱います。M01で延期した機能を暗黙にFoundationへ追加しません。
