# ADR-0005: Error, Assert, and Log Policy

- Status: Accepted
- Date: 2026-08-21
- Decision Owners: CueEngine Project

## Context

Runtime Foundationと将来のPlatform／RHIは、失敗を呼び出し側へ伝播する経路と、Programmer Errorや継続不能状態を診断する経路を共有する必要があります。分類が曖昧なまま個別実装すると、Recoverable ErrorをAssertで停止したり、同じ失敗を複数階層でLogしたり、Native ErrorやSource Locationを失ったりするRiskがあります。

このADRはError分類、C++例外、ResultとErrorのValue Semantics、Source LocationとNative Error、Assert、Fatal、Log、DLL／Plugin境界、Test方法を決定します。外部Logging Libraryは採用しません。

## Legacy Reference

### Legacy Problem

旧CueEngineは、HRESULTやWin32 Errorを呼び出し元へ伝え、Programmer ErrorをAssertで検出し、実行状況をLoggerへ出力する必要がありました。

### Legacy Approach

旧実装は`Result`、`CUE_ASSERT`、Logger、HRESULT変換を個別に提供していました。

### Legacy Strengths

- 戻り値による失敗表現が存在した
- Source位置でProgrammer Errorを停止できた
- RenderingとPlatformの失敗をLogへ出力できた

### Legacy Problems

- Result、Assert、Logの使い分けがCall Siteごとに異なり、Recoverable ErrorとProgrammer Errorが混在した
- 下位層と上位層の両方で同じErrorをLogし、重複診断が発生しやすかった
- HRESULT変換後にNative値や追加Contextが失われる経路があった
- Global Loggerへ依存すると、所有権、初期化順序、Test差し替え、Thread Safetyが不明瞭になった

### Current Requirements

- Recoverable、Programmer、Fatal Errorを区別する
- PlatformとBackendの公開APIへNative型を漏らさない
- ErrorへContextを追加しながら呼び出し側へ伝播できる
- AssertとFatalの動作をBuild構成ごとに再現できる
- LoggerとSinkの所有権、Lifetime、Thread Safetyを明示する
- DLL／Plugin境界へC++例外やSTL所有権を公開しない
- Global可変Singletonを導入しない

### New Design

Recoverable Errorは`Result<T>`で返し、Programmer ErrorはAssert、継続不能状態はFatal経路で扱います。Loggerは明示的に所有・注入されるInstanceとし、Errorを処理するBoundaryだけが必要に応じてLogします。

### Validation

- Window生成失敗、Device生成失敗、GPU実行失敗を分類へ適用する
- Resultの成功、失敗、Context追加をUnit Testする
- Test SinkでLog Recordと同期動作を検証する
- AssertのBuild構成別有効化とFatal終了経路をProcess Testで検証する

## Decision

### Error Classification

| Category | Meaning | Mechanism | Control Flow |
| --- | --- | --- | --- |
| Recoverable Error | 外部状態、入力、環境、Resource不足など、呼び出し側が代替、通知、再試行、終了判断できる失敗 | `Result<T>`または`Result<void>` | 呼び出し側へErrorを返す |
| Programmer Error | API前提違反、不正State、Index範囲外、所有権やThread規則違反 | Assert | Debug／Developmentで診断後に終了。Releaseでは式を評価しない |
| Fatal Error | Data破損、必須Subsystem喪失、回復手順のない内部失敗など、安全な継続ができない状態 | Fatal診断とFatal Handler | 必ずProcessを終了し、呼び出し側へ戻らない |

分類規則:

- AssertでRecoverable Errorを代替しない
- ResultでProgrammer Errorを通常分岐として表現しない
- Fatalは「処理が面倒」という理由で使用しない
- 同じ失敗を下位層でLogしてからResultで返さない。Errorを最終的に処理するBoundaryが一度だけLogする
- Errorを処理せず破棄する場合は、意図をCodeまたはCommentで明示する

代表例:

| Scenario | Category | Handling |
| --- | --- | --- |
| Window生成時にOS Resourceを確保できない | Recoverable | Platform境界でNative Errorを変換し、RuntimeHostへResultで返す |
| 呼び出し側が破棄済みWindowを操作する | Programmer | API前提をAssertし、Debug／Developmentで終了する |
| D3D12 Device生成がCapability不足で失敗する | Recoverable | Backend境界でHRESULTを変換し、Adapter選択または起動終了を上位で判断する |
| GPU実行中にDevice Removedを検出する | RecoverableからFatalへ昇格可能 | Backendは診断可能なErrorを返し、Recovery未実装のRuntimeHostがFatal終了を判断する |
| Error保持状態のResultをValueとして扱う | Programmer | Access APIの前提違反としてAssert対象にする。基本Resultは安全なProbe APIも提供する |

### C++ Exceptions

- First-party Runtime Moduleは、期待される失敗や制御FlowにC++例外を使用しない
- Public APIは例外を投げる契約を持たず、Recoverable Errorを`Result`で返す
- Module境界、DLL境界、Plugin境界をC++例外が越えることを禁止する
- Engine Codeは通常経路で`throw`を使用しない
- 標準Libraryまたは外部Libraryが例外を投げ得る呼び出しは、AdapterまたはModuleのPublic Entry Pointより内側で捕捉し、例外をModule境界へ出さない
- 文書化された回復可能な外部例外はErrorへ変換し、それ以外の予期しない例外は同じModule内でFatalへ変換する
- 各最終ExecutableのComposition Rootは、Module境界の契約違反やEntry Point自身の例外に備えた最後のFallbackとして捕捉できる。捕捉後に通常実行へ復帰せずFatal終了する
- Allocation失敗からのRecovery方針はM01では決定しない。`std::bad_alloc`をRecoverable Errorとして暗黙変換しない

Compilerの例外機能を無効化するかは、外部Libraryと標準Libraryの要件を調査する別Decisionとします。このADRは言語機能が有効でもEngine API契約に使用しないことを定めます。

### Error and Result Value Semantics

`ErrorCode`は次を所有するValue Typeです。

- Domainを表すUTF-8文字列
- Domain内の符号付き64-bit Code

`Error`は次を所有するValue Typeです。

- `ErrorCode`
- 利用者向けではなく開発者向けのUTF-8 Summary
- 0個以上のContext Frame
- 任意のNative Error情報

Context Frameは次を保持します。

- 呼び出しBoundaryで追加するUTF-8 Message
- `std::source_location`から取得したFile、Function、Line、Column

Native Error情報はNative Headerへ依存しない次のValueで保持します。

- Domainを表すUTF-8文字列
- Native値を損失なく格納する符号付き64-bit Code

`Result<T>`の規則:

- 成功時の`T`または失敗時の`Error`の正確に一方だけを保持する
- Default構築を許可せず、明示的なSuccessまたはFailure Factoryから生成する
- Copy／Move可否は`T`の性質に従い、ErrorはValueとしてCopy／Moveできる
- `Result<void>`は成功状態またはErrorの一方だけを保持する
- `has_value()`と明示的`operator bool`で状態を確認できる
- `try_value()`と`try_error()`は非所有Pointerを返し、状態が一致しない場合は`nullptr`を返す
- 返却PointerはResultが同じ状態で生存している間だけ有効とする
- Probe APIは`&`と`const &`修飾だけを提供し、`&&`と`const &&`からの呼び出しを削除して一時ResultからのPointer取得を禁止する
- 状態を前提とするAccess APIを追加する場合、誤用はProgrammer ErrorとしてAssertする
- ErrorへContextを追加する操作はError所有権を維持し、元のError CodeとNative Errorを失わない
- Errorを含むResultから成功値をDefault生成しない

ErrorとResultはM01のFirst-party Static Library境界で使用するC++ APIです。安定ABIではなく、Plugin境界へそのまま公開しません。

### Error Propagation

下位層はErrorへ自身の失敗理由を記録し、上位Boundaryは処理の意味をContextとして追加します。

```text
Cue.Platform.Windows
    ErrorCode: Cue.Platform.WindowCreate / 1
    Native Error: Win32 / 8

CueRuntimeHost
    Context: Main window initialization failed
```

- Error Codeは最初の失敗原因を保持する
- Context追加でError Codeを別Codeへ上書きしない
- 抽象Boundaryで意味が変わる場合だけ、新しいErrorを作り、元ErrorのSummaryとNative情報をContextへ残す
- 下位層は返却前にLogしない
- RuntimeHostなど処理を終了、代替、利用者通知へ変換するBoundaryがLog Levelを決める

### Native Error Conversion

HRESULTとWin32 Errorの変換関数はWindowsまたはD3D12実装TargetのPrivateへ置きます。

- Win32境界は`GetLastError`値を直ちに取得し、Domain `Win32`と整数値をErrorへ保持する
- D3D12／DXGI境界はHRESULTを符号付き32-bit値として解釈し、符号拡張して符号付き64-bit値へ変換する。例えば`0x80004005`は`-2147467259`として保持する
- Public Header、Result、Errorは`HRESULT`、`DWORD`、`HANDLE`、COM型を使用しない
- Native Messageの文字列化は診断強化であり、変換失敗時もNative Domainと値を失わない
- Platform／Backend固有Error CodeからFoundationの汎用Codeへ無理に集約しない。Domain付きCodeで衝突を避ける

### Assert Policy

Assertは明示的な診断Contextを受け取り、Global Loggerを参照しません。

予定するCall Site形式:

```cpp
CUE_ASSERT(assertContext, condition, "message");
```

| Configuration | Assert | Condition Evaluation | Failure Behavior |
| --- | --- | --- | --- |
| Debug | Enabled | 評価する | Source LocationとMessageを同期出力し、Debuggerが接続されていればBreakを試み、Fatal Handlerで終了する |
| Development | Enabled | 評価する | Source LocationとMessageを同期出力し、Fatal Handlerで終了する |
| Release | Disabled | 評価しない | Codeを生成しない |

- Assert式へ副作用を持たせない
- Releaseでも評価が必要な条件にAssertを使用しない
- Debugger Breakは診断補助であり、終了を置き換えない
- Assert失敗後に通常実行へ戻るHandlerをProductionで許可しない
- Assert ContextはLoggerとFatal Handlerへの非所有参照を保持する。Composition RootはAssert Contextを先に破棄し、その後にLoggerとFatal Handlerを破棄する
- `CUE_ENABLE_ASSERTS`はDebugとDevelopmentで`1`、Releaseで`0`としてTargetから公開する

### Fatal Policy

Fatal経路は次の順序で実行します。

1. Fatal Log RecordへLevel、Message、Source Location、利用可能なErrorを格納する
2. Loggerが同一Mutexを保持したまま全Sinkへ同期出力する
3. Loggerが同じCritical Section内で全SinkをFlushする
4. 注入されたFatal Handlerを呼び出す
5. Fatal HandlerはProcessを終了し、呼び出し側へ戻らない

Fatal HandlerはComposition Rootが一意所有し、Fatal経路は非所有参照を受け取ります。Production既定Handlerは`std::abort`またはPlatform固有の終了処理を使用します。Test Processは終了Codeを固定するHandlerへ差し替え、親CTestから終了を検証できます。

### Log Policy

Log Levelは次の6種類です。

| Level | Intended Use |
| --- | --- |
| Trace | 高頻度な詳細追跡。既定では無効化可能 |
| Debug | 開発時のState確認 |
| Info | 正常なLifecycleと重要な選択結果 |
| Warning | 継続可能だが注意が必要な状態 |
| Error | 現在の操作が失敗したがProcessは継続可能 |
| Fatal | 安全な継続ができずProcessを終了する状態 |

`LogRecord`はLevel、UTF-8 Message、Source Locationを保持します。Timestamp、Thread ID、Category、Structured Fieldは実測要件が確認されるまで必須にしません。

LoggerとSinkの規則:

- Loggerは通常のObjectとしてComposition Rootまたは上位Ownerが一意所有する
- Loggerは0個以上の`std::unique_ptr<LogSink>`を受け取り、Sinkを一意所有する
- Global Logger、Global Sink Registry、Service Locatorを導入しない
- `Logger::log`と`Logger::flush`は同期APIとし、同じLogger内部Mutexで`write`と`flush`を直列化する
- Fatal経路は`Logger::log_and_flush`を使用し、一度のLock保持中にFatal Recordの全Sinkへの`write`と全Sinkの`flush`を完了する。他ThreadのRecordはこの区間へ割り込まない
- 一つのRecordは構成された各Sinkへ一度ずつ、登録順に渡す
- SinkはLoggerのLock保持中に呼ばれ、同じLoggerへ再入してはならない
- Loggerが直列化するため、個別Sinkは同じLoggerからの並行`write`へ対応する必要がない
- 複数Loggerから同じSinkを共有しない。共有要件が確認された場合は別の同期所有設計を行う
- Sinkの`write`失敗は例外を投げない。Fallback診断または破棄方針をSink実装ごとに明記する

最小Formatは次とします。

```text
[Level] Message (File:Line Function)
```

ConsoleとDebugger Outputの選択規則:

- Consoleを持つExecutableはConsole Sinkを既定とする
- Consoleを持たないWindows Executableは将来のPlatform実装がDebugger Sinkを選択できる
- Composition Rootは通常、Console SinkとDebugger Sinkの両方を自動登録しない
- 明示的に両方を構成した場合、同じRecordが各Sinkへ一度ずつ出力されることを意図した重複として扱う
- M01ではPlatform非依存なLogger、Console Sink、Test Sinkに必要な契約だけを実装し、Windows Debugger SinkはPlatform Milestoneへ延期する

### DLL and Plugin Boundary

- C++例外を境界へ出さない
- `std::string`、`std::vector`、`std::unique_ptr`などSTL所有権を境界へ出さない
- FoundationのC++ `Result`をPlugin ABIへそのまま公開しない
- 将来のPlugin ABIはVersion付きPOD、明示的なBuffer Ownership、Error CodeとUTF-8 Viewを持つAdapterを定義する
- 境界内部で発生した予期しない例外は境界内で捕捉し、Error Codeへ変換するかFatal終了する

## Test Strategy

### Result and Error

- SuccessがValueだけを保持する
- FailureがErrorだけを保持する
- `try_value()`と`try_error()`が状態に応じてPointerまたは`nullptr`を返す
- Probe APIが右辺値Resultから呼び出せないことをCompile-time Testで検証する
- `Result<void>`の成功と失敗を検証する
- Context追加後もError Code、Native Error、既存Contextが保持される
- Copy／Move後も状態と値が保持される

### Logger

- Test SinkでLevel、Message、Source Location、出力順を検証する
- 複数ThreadからLogし、Record欠落とData Raceがないことを検証する
- 複数Sinkへ一度ずつ登録順に出力されることを検証する
- Flushが全Sinkへ伝播することを検証する
- Fatalの`log_and_flush`中に他ThreadのRecordが割り込まず、`write`と`flush`が競合しないことを検証する

### Assert and Fatal

- DebugとDevelopmentでAssert条件が評価されることを検証する
- ReleaseでAssert条件が評価されないことを副作用Counterで検証する
- 成功条件ではLogとFatal Handlerが呼ばれないことを検証する
- 失敗条件は子Processで実行し、Fatal Recordと規定終了Codeを検証する
- Fatal Handlerが呼び出し側へ戻らないことを型とProcess Testで検証する

### Boundary Scenarios

- Window生成失敗はNative Error付きResultとしてRuntimeHostへ到達する
- Device生成失敗はHRESULT付きResultとしてAdapter選択または起動終了へ到達する
- HRESULT `0x80004005`が符号拡張され、`-2147467259`として保持される
- GPU Device RemovedはBackendからResultで返り、Recovery未実装のRuntimeHostがErrorを一度LogしてFatal終了を選択する

## Consequences

### Positive

- Recoverable、Programmer、FatalのCall SiteをReviewできる
- ErrorをLogせず伝播するため、重複Logを防げる
- Source Location、Native Error、追加Contextを失わず診断できる
- LoggerとSinkの所有権、破棄順序、Thread SafetyをInstance単位で管理できる
- AssertとFatalをGlobal状態なしでTestできる

### Negative

- Call SiteはResultの確認とContext追加を明示的に記述する必要がある
- Assert ContextとLoggerを必要な経路へ注入する必要がある
- 同期LoggerはSink I/O時間だけ呼出Threadを停止する
- ErrorとContextのUTF-8文字列所有にAllocationが発生する
- C++例外機能そのものの無効化は未決定のまま残る

## Enforcement

- Public API ReviewでError分類、所有権、Lifetime、Thread Safetyを確認する
- Recoverable ErrorをAssertへ置き換える変更を受け入れない
- Resultを返す下位APIで同じErrorをLogしない
- Plugin境界でFoundationのC++型を直接公開しない
- AssertとLoggerへGlobal可変Singletonを導入しない
- 方針変更はこのADRをSupersedeするADRで行う

## Follow-up

- Issue #33でError、Result、Source Location Contextを実装する
- Issue #34でLogger、Console／Test Sink契約、Assert、Fatal経路を実装する
- Issue #35で3構成のResult、Logger、Assert、依存方向TestをCompletion Gateへ追加する
