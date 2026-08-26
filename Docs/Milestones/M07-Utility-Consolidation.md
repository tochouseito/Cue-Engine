# M07 Utility Consolidation Completion Evidence

M07は、RuntimeHost、Foundation、Platform Windows、RHI D3D12、TestSupport、Build検証に散在していた
同一契約の処理を、所有Moduleと公開境界を変えずに共通化するMilestoneである。

検証日は2026-08-27、実装完了時の`Rebuild` Commitは
`81f7e975b2b91dab64dbae5f50a71338ff52ee9c`である。新しいArchitecture、安定ABI、永続形式、
Command Line Parser全体、Platform非依存Unicode Codec、構造化Suppressed Errorは対象外とする。

## Scope Delivered

| Issue | Pull Request | Result |
| --- | --- | --- |
| [#103 Foundation: 安全な10進整数変換](https://github.com/tochouseito/CueEngine/issues/103) | [#112](https://github.com/tochouseito/CueEngine/pull/112) | 非負10進整数変換をFoundationへ集約し、RuntimeHost固有Validationと分離 |
| [#104 D3D12: Native ErrorとObject命名](https://github.com/tochouseito/CueEngine/issues/104) | [#113](https://github.com/tochouseito/CueEngine/pull/113) | D3D12 Private UtilityへError生成とObject命名を集約 |
| [#105 Platform: Windows ErrorとClient Size](https://github.com/tochouseito/CueEngine/issues/105) | [#114](https://github.com/tochouseito/CueEngine/pull/114) | Platform Windows Private UtilityへWin32 Error生成とClient Size取得を集約 |
| [#106 TestSupport: D3D12 Probe Helper](https://github.com/tochouseito/CueEngine/issues/106) | [#115](https://github.com/tochouseito/CueEngine/pull/115) | WARP準備、InfoQueue Error集計、Error照合等をTestSupport内部へ集約 |
| [#107 TestSupport: RHI Process Fixture](https://github.com/tochouseito/CueEngine/issues/107) | [#116](https://github.com/tochouseito/CueEngine/pull/116) | FatalHandler、Log収集、Scenario起動をProcess Test Fixtureへ集約 |
| [#108 Build: CMake依存検証Utility](https://github.com/tochouseito/CueEngine/issues/108) | [#117](https://github.com/tochouseito/CueEngine/pull/117) | C++ Comment除去とToken走査をCMake共通Moduleへ集約 |
| [#109 Windows UTF共有境界Research](https://github.com/tochouseito/CueEngine/issues/109) | [#119](https://github.com/tochouseito/CueEngine/pull/119) | ADR-0009で低層Windows Foundation境界を決定 |
| [#110 Secondary Error合成Research](https://github.com/tochouseito/CueEngine/issues/110) | [#121](https://github.com/tochouseito/CueEngine/pull/121) | ADR-0010でPrimary identityを維持する診断合成契約を決定 |
| [#118 Foundation Windows UTF Core](https://github.com/tochouseito/CueEngine/issues/118) | [#123](https://github.com/tochouseito/CueEngine/pull/123) | `Cue.Foundation.Windows`へWin32 UTF変換Primitiveを集約 |
| [#120 Foundation Secondary Error診断合成](https://github.com/tochouseito/CueEngine/issues/120) | [#124](https://github.com/tochouseito/CueEngine/pull/124) | `Error::append_secondary_diagnostics`へ4経路の重複を集約 |
| [#111 Completion Gate](https://github.com/tochouseito/CueEngine/issues/111) | 本変更 | M07の境界、検証結果、未決定事項、残存Riskを統合監査 |

## Ownership Boundaries

- Platform非依存の数値変換は`Cue.Foundation`が所有し、`0`禁止などの用途固有Validationは呼び出し側が所有する。
- Win32 Window処理は`Cue.Platform.Windows`のPrivate境界に留め、Platform固有型を上位公開APIへ出さない。
- D3D12 Error生成とNative Object命名は`Cue.RHI.D3D12`のPrivate境界に留める。
- ProbeとProcess FixtureはTestSupportだけが所有し、Production APIへTest都合を追加しない。
- CMake依存検証UtilityはBuild検証だけが利用し、Runtime Targetへ依存を追加しない。
- Windows UTF変換Primitiveは`Cue.Foundation.Windows`が所有し、PlatformとD3D12はPrivate Linkする。
  Platform非依存の`Cue.Foundation`本体へWindows依存を追加せず、RHIからPlatform Windowsへ依存しない。
- Secondary Error合成はError表現を所有する`Cue.Foundation`が担当する。RuntimeHostとD3D12は用途固有の
  ContextとLabelだけを渡し、Primary ErrorのCode、Summary、Native Error、Cause Chain、Root Causeを変更しない。

## Research Decisions and Follow-up

[ADR-0009](../Decisions/0009-windows-utf-conversion-foundation-boundary.md)は、Win32 UTF変換を
`Cue.Foundation.Windows`へ配置することを決定した。PrimitiveはUTF-8／UTF-16のstrict変換とWin32 Native
Codeだけを返し、PlatformとD3D12のError identityやDRED fallbackは各呼び出し側が維持する。

[ADR-0010](../Decisions/0010-secondary-error-diagnostic-composition.md)は、独立したSecondary Errorを
Primary ErrorのCause Chainへ混ぜず、既存Contextへ診断展開する契約を決定した。自己参照はDebug／Development
で注入済み`AssertContext`を使って停止し、ReleaseではPrimary Errorを変更せず復帰する。Allocation失敗は
注入済みEmergency HandlerのFatal経路へ移行する。

次の事項はM07で確定していないため、必要性が具体化した時点でResearch IssueまたはADRとして扱う。

- Platform非依存Unicode Codec、Normalization、Locale変換
- `Error`の安定DLL／Plugin ABIとSerialization形式
- 構造化Suppressed Error、Context上限、Truncation Policy
- 汎用Command Line Parserと浮動小数点／符号付き数値Parser

## Build and Test Evidence

`81f7e975b2b91dab64dbae5f50a71338ff52ee9c`と同一の実装内容で、次を実行した。

```powershell
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug --output-on-failure
cmake --build --preset windows-vs2026-development
ctest --preset windows-vs2026-development --output-on-failure
cmake --build --preset windows-vs2026-release
ctest --preset windows-vs2026-release --output-on-failure
git diff --check
```

結果:

- CMake Configure成功
- Debug Build成功、CTest `147/147`成功、失敗0件
- Development Build成功、CTest `147/147`成功、失敗0件
- Release Build成功、CTestは147件中失敗0件。Debug診断を禁止する既存方針により4件Skip
- Foundation、Platform、RHI、RuntimeHostのDependency Testが成功
- Hardware／WARPのDevice、Presentation、Render、Resize Smokeが成功
- `Cue.Foundation.Error.SecondarySelfReference`が各Buildの規定挙動で成功
- `git diff --check`はエラーなし
- Repositoryに`scripts/codex_build.ps1`は存在しないため、正式なCMake／CTest Presetを使用

## Review and CI Audit

M07の各PRは公式Codex Review後に統合した。指摘された問題は日本語で回答し、修正後のHeadを再レビューした。
PR #124ではPrimary Contextを`a_label`として渡した場合の無効参照Riskが検出され、Mutation前の所有コピーと
回帰Testを追加した。最新Headの再レビューは重大な問題なし、未解決Review Threadは0件、mergeableはtrueだった。

Windows CIはM07前半のPRで成功を確認した。M07後半では、完了済みRunが長時間`queued`表示されることと、
PR #124の`pull_request` EventにCheck Runが生成されないActions基盤側の異常が発生した。PR作成、空Commit、
実装修正Pushの各Eventを試しても`no checks reported`だったため、Issue #118／#120（PR #123／#124）では
最新Codeと同一内容をローカルWindows環境でDebug／Development／Releaseの全Buildと全CTestを再実行し、
例外理由と結果をPRへ記録した。
Completion Gate PRでもWindows CIの生成を再試行し、成否を最終監査へ追記する。

## Acceptance Gates

- [x] M07 Implementation Issue #103から#108、#118、#120が完了している
- [x] Research Issue #109と#110の判断をADR-0009とADR-0010へ記録している
- [x] CMake Configureが成功する
- [x] Debug、Development、ReleaseのBuildが成功する
- [x] 全CTestで失敗がない
- [x] `git diff --check`が成功する
- [x] 公式Codex Reviewの指摘を修正し、未解決Threadが0件である
- [x] Implementation PRのmergeabilityとExpected Head SHAを確認してMergeした
- [ ] Completion Gate PRのWindows CI、Codex Review、未解決Thread、mergeabilityを確認する

## Remaining Risks

- `Cue.Foundation.Windows`はWindows x64だけで検証しており、非Windows Hostは未実装である。
- UTF変換APIとSecondary Error APIは内部C++境界であり、安定ABIとしては未保証である。
- Secondary Errorは構造化FieldではなくContextへ展開するため、将来のEditor診断UIでは再設計余地がある。
- Secondary ErrorのContext数、Cause数、文字列長には上限がなく、入力全体の転記に比例するAllocationと走査Costが
  発生する。Allocation失敗時はEmergency HandlerのFatal経路へ移行するため、容量上限とTruncation Policyは
  将来のResearchで決定する必要がある。
- CMake Utilityは現在の依存検証に必要なComment／Token走査であり、完全なC++ Parserではない。
- `Cue.RHI.D3D12.SwapChain.RecoverySignalDeviceRemoved`はWindows CIで過去に一度不安定終了したが、
  同一CodeのローカルDebug／Development／Releaseと反復確認では成功している。
- GitHub ActionsのRunner／Event生成異常はEngine Code外の運用Riskとして残る。

## Next Work

M07のCompletion GateをMergeしてMilestoneを閉じた後、次のMilestoneは未確定のArchitectureをResearch Issueで
先に決定し、M07のUtilityへScope外機能を暗黙に追加しない。
