# M00 Repository Foundation Completion Evidence

## Purpose

この文書は、`M00 Repository Foundation`で構築したリポジトリ基盤を新規Cloneから再現できることと、次のMilestoneへ進むためのAcceptance Gateを満たしたことを記録します。

検証日は2026-08-21です。M01以降のModule、Window、Runtime Foundation、DirectX 12、配布Packageはこの検証の対象外です。

## Scope Delivered

| Issue | Pull Request | Result |
| --- | --- | --- |
| [#22 Rebuild方針](https://github.com/tochouseito/CueEngine/issues/22) | [#23](https://github.com/tochouseito/CueEngine/pull/23) | Rebuild方針と旧CueEngine参照方針を文書化 |
| [#24 CMake初期化](https://github.com/tochouseito/CueEngine/issues/24) | [#25](https://github.com/tochouseito/CueEngine/pull/25) | CMake Configureと3構成Presetを整備 |
| [#26 最小TargetとCTest](https://github.com/tochouseito/CueEngine/issues/26) | [#27](https://github.com/tochouseito/CueEngine/pull/27) | `CueBuildProbe`と`CueBuildProbe.Smoke`を整備 |
| [#57 Engine Source配置](https://github.com/tochouseito/CueEngine/issues/57) | [#58](https://github.com/tochouseito/CueEngine/pull/58) | Engine所有Sourceを`Engine/Source`へ集約 |
| [#28 Windows CI](https://github.com/tochouseito/CueEngine/issues/28) | [#59](https://github.com/tochouseito/CueEngine/pull/59) | Debug、Development、ReleaseのWindows CIを整備 |
| [#29 GitHub Template](https://github.com/tochouseito/CueEngine/issues/29) | [#60](https://github.com/tochouseito/CueEngine/pull/60) | IssueとPull RequestのTemplateを整備 |
| [#30 M00完了Gate](https://github.com/tochouseito/CueEngine/issues/30) | [#61](https://github.com/tochouseito/CueEngine/pull/61) | Clean Checkout再現性と完了Gateを検証 |

GitHubの既定Branchは`Rebuild`です。Issue Templateとして`Bug Report`、`Implementation`、`Research`、Pull Request Templateとして`.github/pull_request_template.md`がGitHubに認識されていることを確認しました。

## Clean Checkout Validation

GitHubからRepository外の隔離Directoryへ新規Cloneし、READMEのConfigure、Build、Test手順を実行しました。

- 既定BranchのCloneでは、`Rebuild`がCheckoutされることをCommit `196363de3a788976daf6bfe400cbfe4741090195`で確認しました。
- 追加後のREADMEを含むPull Request Commit `e66d7c2eb7db8cb03682921cab206f07946d2db1`は、`feature/Engine/M00/30-foundation-completion-gate`を新規Cloneして同じ手順を再検証しました。
- 再検証後の追補はこの証跡文書だけを変更し、README、CMake、Build、Test設定は変更していません。

### Environment

| Component | Version |
| --- | --- |
| Host | Windows x64 |
| CMake | 4.2.3 |
| Visual Studio | 18.9.12105.275 |
| MSVC Tools | 14.51.36231 |
| MSVC Compiler | 19.51.36256.0 |
| Windows SDK | 10.0.26100.0 |

### Commands

```powershell
git clone https://github.com/tochouseito/CueEngine.git
Set-Location CueEngine
git branch --show-current
cmake --list-presets
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug
out/build/windows-vs2026/bin/Debug/CueBuildProbe.exe
cmake --build --preset windows-vs2026-development
ctest --preset windows-vs2026-development
out/build/windows-vs2026/bin/Development/CueBuildProbe.exe
cmake --build --preset windows-vs2026-release
ctest --preset windows-vs2026-release
out/build/windows-vs2026/bin/Release/CueBuildProbe.exe
git status --short
git ls-files out .vs CMakeUserPresets.json
git diff --check
```

### Results

- 既定BranchをCloneしたときのBranchは`Rebuild`でした。
- 追加後のREADMEを含むPull Request Commitを新規Cloneして再検証しました。
- CMake Configureは成功しました。
- Debug、Development、ReleaseのBuildはすべて成功しました。
- 各構成のCTestは`1/1`成功しました。
- 各構成の`CueBuildProbe`は終了Code `0`でした。
- `out`、`.vs`、`CMakeUserPresets.json`にTracked Fileはありませんでした。
- 検証後の`git status --short`は空で、Cleanでした。
- `git diff --check`は成功しました。

## GitHub Actions Evidence

Pull Request #61で実行された[Windows Build and Test run 32477101743](https://github.com/tochouseito/CueEngine/actions/runs/32477101743)で、Debug、Development、Releaseの全Jobが成功しました。`Rebuild`への直近Pushでも[run 32475812037](https://github.com/tochouseito/CueEngine/actions/runs/32475812037)が成功しています。

GitHub Actions環境の記録は次のとおりです。

| Component | Version |
| --- | --- |
| Runner Image | `windows-2025-vs2026` |
| CMake / CTest | 4.4.2 |
| Visual Studio | 18.9.12112.369 |
| MSVC Tools | 14.51.36231 |
| Windows SDK | 10.0.26100.0 |

各JobはCheckout、Version記録、CMake Configure、対象構成のBuild、対象構成のCTestを実行し、CTestは`1/1`成功しました。

## Acceptance Gates

- [x] Rebuild方針が文書化されている
- [x] CMake Configureが成功する
- [x] Debug構成がBuildできる
- [x] Development構成がBuildできる
- [x] Release構成がBuildできる
- [x] CTestで最小Testが成功する
- [x] GitHub ActionsでConfigure、Build、Testが成功する
- [x] 新規CloneのClean環境でREADMEの手順を再現できる
- [x] Trackedされる生成物やUser固有設定がない
- [x] M00のIssue、Pull Request、検証結果、既知Riskをこの文書へ集約した
- [x] TagとGitHub Releaseの作成内容を準備した

## Known Risks and Deferred Work

- 現在のTestは`CueBuildProbe.Smoke`だけであり、Repository Foundationの最小起動確認に限定されます。
- CIはWindows、Visual Studio 2026、MSVCだけを対象とし、Linux、macOS、他Compilerは未検証です。
- GitHub-hosted RunnerのTool Versionは更新されるため、将来のCI実行ではこの記録と異なる可能性があります。
- M01以降のModule境界、公開API、Error Handling、ABIはまだ確定していません。必要なResearch IssueまたはADRを先行させます。
- Runtime、Window、Renderer、DirectX 12の動作はM00では検証していません。

## Release Preparation

TagとGitHub Releaseは、ユーザーの明示承認を得た後に作成します。このM00完了作業では作成しません。

- Tag: `rebuild-m00-repository-foundation`
- Target: Issue #30のPull Requestをマージした`Rebuild` Commit。作成時にCommit SHAを固定する
- Release title: `CueEngine Rebuild M00 Repository Foundation`

Release notes案:

```markdown
# CueEngine Rebuild M00 Repository Foundation

CueEngineを一から再設計・再実装するためのRepository Foundationを確立しました。

## Included

- Rebuild方針と旧CueEngine参照方針
- CMakeを正本とするWindows x64 / Visual Studio 2026のBuild基盤
- Debug、Development、ReleaseのCMake Preset
- Engine所有Sourceの`Engine/Source`への集約
- 最小実行Target `CueBuildProbe`
- CTestによる最小Smoke Test
- 3構成のGitHub Actions Windows CI
- IssueおよびPull Request Template
- Clean Checkoutでの再現性検証

## Validation

- Clean CloneからCMake Configure成功
- Debug、Development、ReleaseのBuild成功
- 全3構成のCTest成功
- 全3構成の`CueBuildProbe`が終了Code 0
- GitHub Actionsの全3構成が成功

詳細は`Docs/Milestones/M00-Repository-Foundation.md`を参照してください。

## Scope

このReleaseはRepository Foundationのみを対象とします。Runtime、Window、Renderer、DirectX 12などのEngine機能は含みません。
```
