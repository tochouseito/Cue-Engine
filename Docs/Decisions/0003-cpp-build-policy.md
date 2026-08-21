# ADR-0003: C++ Build Policy and Minimum Target

- Status: Accepted
- Date: 2026-08-21
- Decision Owners: CueEngine Project

## Context

M00 Repository Foundation では、Configure だけでなく、C++ Target を同じ条件で Build できる基盤が必要です。

一方、Runtime、Renderer、Editor などの Module 境界や、Test Framework、外部 Library はまだ決定していません。将来の Module 構造を先回りせず、Compiler 要件と Build 構成を検証できる最小 Target が必要です。

## Decision

First-party C++ Target は C++20 以上を要求し、Compiler 固有の設定は Target 単位で適用します。

最初の Windows Compiler である MSVC には、次の Compile Option を適用します。

- `/W4`: 高い Warning Level を使用する
- `/WX`: First-party Code の Warning を Build Error として扱う
- `/permissive-`: MSVC の標準準拠 Mode を使用する
- `/Zc:__cplusplus`: `__cplusplus` へ選択中の C++ 標準 Version を反映する
- `/utf-8`: Source と Execution Character Set を UTF-8 に固定する

Build 構成は次の3種類とします。

| Configuration | Optimization | Debug Information | `NDEBUG` |
|---|---|---|---|
| Debug | CMake と MSVC の既定 Debug 設定 | 有効 | 定義しない |
| Development | `/O2` | `/Zi` と Linker の `/DEBUG` | 定義しない |
| Release | CMake と MSVC の既定 Release 設定 | 既定 | 定義する |

`Development` は実行時の最適化を保ちながら、Debugger と Profiler で診断可能にする構成です。Assert 方針が ADR で決まるまでは `NDEBUG` を定義しません。

Build Policy を実際の C++ Compile と Link で検証するため、依存を持たない実行 Target `CueBuildProbe` を追加します。この Target は Engine Module ではなく、M00 の Build 基盤を検証する Probe です。

各 Build 構成では CTest から `CueBuildProbe.Smoke` を実行し、最小 Target が起動して正常終了することを検証します。

## Consequences

### Positive

- C++ 標準と MSVC の Warning Policy を Build 時に検証できる
- Debug、Development、Release の3構成を同じ Preset から Build できる
- Development Build を Debugger と Profiler へ接続できる
- Windows の Active Code Page に依存せず UTF-8 Source を Compile できる
- CTest Preset から最小 Target の起動を再現できる
- 将来の Module 構造や外部 Library を先回りして固定しない

### Negative

- MSVC 以外の Compiler Option は未定義である
- C++20 より新しい標準機能は現時点で要求できない
- Test Coverage は最小 Target の Smoke Test に限られる

## Follow-up

- Unit Test Framework と Assertion 方針を別の M00 Issue で決定する
- CI で Configure、Build、Test を実行する別の M00 Issue を追加する
- Windows 以外の Platform 着手時に Compiler Warning Policy を ADR で拡張する
