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
VS_USER_PROPS: <none>
SOURCE_INCLUDE_DIRECTORIES: <none>
SOURCE_VS_SETTINGS: <none>
PROJECT_INCLUDE_DIRECTORIES: Foundation only
PROJECT_HEADER_TRAVERSAL: recursive; Foundation only
TARGET_OBJECT_SOURCES: <none>
LINK_OPTIONS: $<$<CONFIG:Development>:/DEBUG>
TARGET_GRAPH_OUTGOING_EDGES: <none>
Forbidden platform link inputs: Windows SDK, DXGI, D3D12
Cycle review: generated CMake target graph has no outgoing Cue.Foundation edge
```

`Cue.Foundation.Dependencies`は、Custom Targetを含めてCMakeが生成したTarget Graphを検査し、`Cue.Foundation`から出るEdgeがないことを確認します。これにより、Link、Object Source、`add_dependencies`に加え、Target Generator Expressionを使うCustom Commandや生成SourceからCustom TargetへのEdgeも検出します。Foundationから出るTarget Edgeを許可しないため、Foundationを含むTarget依存循環も成立しません。

Foundation配下のC++ SourceとHeaderのLiteralな直接Includeは、選択中Windows SDKの`um`、`shared`、`winrt`、`cppwinrt`、`ucrt`で解決し、Macro形式と絶対PathのIncludeは拒否します。Foundation内で解決できるProject Headerは未登録の拡張子も含めて再帰的に走査し、Foundation外で解決されるProject HeaderとFoundation外のTarget Include Directoryは拒否します。Source単位のInclude Directoryと`VS_SETTINGS`、Target単位の`VS_USER_PROPS`は空必須です。Header SetとTarget Sourceの全ファイルも拡張子に関係なく同じ内容検査へ含めます。前4 DirectoryのHeaderと、UCRT内のISO C標準Header以外を拒否するため、限定したPlatform Header名の列挙には依存しません。PCHと公開Compile Optionを空必須とし、非公開Compile OptionとFoundation Target定義ScopeでDebug／Development／Releaseへ適用されるGlobal C++ Flagは現在必要な警告、準拠、UTF-8、Runtime既定、最適化・Debug情報のAllowlistだけを許可します。Targetの`COMPILE_FLAGS`とSource単位の`COMPILE_FLAGS`／`COMPILE_OPTIONS`も空必須とし、`SOURCES`とHeader Set内のGenerator Expression、およびFoundation外のHeader Set Fileを禁止します。加えて、Source上のMSVC `#pragma comment(...)`を拒否し、生成された`Cue.Foundation.lib`のLinker Directiveを`dumpbin`で検査します。`/`と`-`のOption Prefixを正規化し、MSVC Runtime以外の`DEFAULTLIB`と未承認Directiveを拒否するため、`__pragma(comment(...))`などCMakeのTarget Propertyに現れない暗黙依存も検出します。

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
- Dependency Testは、生成Target GraphのOutgoing Edgeがないことと、Windows SDK／UCRT Platform Header解決検査の成功を出力
- Negative Injectionとして`INTERFACE_LINK_OPTIONS`の`/DEFAULTLIB:d3d12.lib`、`SOURCES`の`$<TARGET_OBJECTS:...>`、Custom Commandの`$<TARGET_FILE:...>`、生成SourceのCustom Target依存、`processthreadsapi.h`と`io.h`の直接Include、Macro経由と絶対Pathの`windows.h` Include、Foundation外のProject Headerへの相対Include、未登録のFoundation内`.ipp`を介した`windows.h` Include、Foundation外のTarget Include DirectoryとSource単位Include Directory、`VS_USER_PROPS`とSource単位`VS_SETTINGS`、公開PCHの`windows.h`、非公開・条件Generator Expression内・`SHELL:`内・分割Generator Expression内・RootとTarget定義ScopeのGlobal Debug Flagにある`/FIwindows.h`、TargetとSource単位の`COMPILE_FLAGS`による`/FIwindows.h`、条件付きSource Generator ExpressionとSource単位`/FIwindows.h`の組み合わせ、公開Header Setの条件Generator Expression、Header Set内`.ipp`とprivate Target Source `.hxx`からの`windows.h` Include、`#pragma comment(lib, "d3d12.lib")`、`__pragma(comment(lib, "d3d12.lib"))`、`__pragma(comment(linker, "-defaultlib:d3d12.lib"))`がそれぞれGateを失敗させることを確認
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
