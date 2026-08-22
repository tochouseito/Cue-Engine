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

## Reference Engine Comparison

| Engine | Relevant Approach | Strengths | Trade-offs |
| --- | --- | --- | --- |
| Unreal Engine | [`check`、`verify`、`ensure`](https://dev.epicgames.com/documentation/unreal-engine/asserts-in-unreal-engine?lang=en-US)を用途とBuild構成で分け、[`UE_LOG`](https://dev.epicgames.com/documentation/unreal-engine/logging-in-unreal-engine?lang=en-US)はCategoryとVerbosityを持つ | 大規模Projectで診断意図を細かく表現でき、Shipping時のCostも制御しやすい | MacroとCategoryの選択肢が多く、使い分けと設定の学習Costが高い。M01で同等の機能量を導入するとFoundationのScopeを超える |
| Unity | [`Debug.Assert`](https://docs.unity3d.com/ja/6000.0/ScriptReference/Debug.Assert.html)と[`Debug.Log`](https://docs.unity3d.com/ja/current/ScriptReference/Debug.Log.html)がConsoleおよびObject Contextへ統合される | Editor上で直感的に利用でき、Objectと診断を結び付けやすくIterationが速い | Managed ObjectとEditor統合を前提とする利便性をNative Runtime Foundationへそのまま適用できない。AllocationとEditor依存をRuntime境界へ持ち込まない設計が必要になる |
| Godot | [`ERR_FAIL_*` Macro](https://docs.godotengine.org/en/stable/contributing/development/core_and_modules/common_engine_methods_and_macros.html)で診断と早期Returnを組み合わせ、[`Logger`](https://docs.godotengine.org/en/stable/classes/class_logger.html)を追加できる | 継続可能な失敗を簡潔に報告でき、Custom Loggerによる拡張点を持つ | Error出力とControl FlowがMacroへ結合しやすく、下位層での重複Logを避けるには運用規則が必要になる |
| CueEngine M01 | Recoverable Errorを`Result`、Programmer ErrorをAssert、継続不能状態をFatalへ分離し、LoggerとSinkを明示所有・注入する | Module境界、所有権、Test差し替え、Platform非依存性をReviewしやすい。Global状態なしで診断経路を検証できる | Unreal EngineのCategory／動的Verbosity、UnityのObject Context、Godotの簡潔な早期Return MacroはM01では持たない。Call SiteのResult処理と依存注入が増え、同期LoggerのI/O Costを負う |

公式資料で確認した機能をCueEngineへ適用する観点から、必須の比較軸を次のように評価します。これは各Engine全体の優劣ではなく、M01のError／Assert／Log方針に限定した設計上の評価です。

| Viewpoint | Unreal Engine approach | Unity approach | Godot approach | CueEngine M01 decision |
| --- | --- | --- | --- | --- |
| Usability | 用途別MacroとCategoryを選ぶ | 単純な`Debug` APIとObject Contextを使う | Error Macroで診断とReturnを同時記述する | `Result`処理とContext注入を明示するため記述量は増える |
| Runtime Performance | Build別Assert除去とVerbosity FilterでCostを制御する | Message変換とConsole統合のCostを許容する | Macroによる早期Returnと必要時の出力を行う | Release Assertは無評価だが、Error所有と同期SinkにAllocation／I/O Costがある |
| Iteration Speed | Editor Output、Log Category、`ensure`で調査しやすい | ConsoleからObject Contextへ辿りやすい | Editor OutputとTrace付きErrorを利用できる | Source LocationとCauseを持つが、Editor統合は後続Milestoneとなる |
| Extensibility | Category、Verbosity、Output経路の選択肢が多い | `ILogger`とEditor側の表示機能へ統合される | Custom `Logger`を登録できる | 一意所有する`LogSink`を差し替えられるが、CategoryとStructured Fieldは未実装 |
| Portability | Engine Coreの抽象化内で複数Platformへ提供される | Managed APIとして複数Player Platformへ提供される | Engine Core MacroとLoggerとして複数Platformへ提供される | Public ValueにNative型を含めず、Platform固有変換をPrivate Targetへ隔離する |
| Data Safety | `check`／`verify`／`ensure`の選択をCall Siteへ委ねる | AssertまたはException後の継続判断がManaged実行環境と利用側に依存する | 継続を重視するError Macroが多い | Recoverable、Programmer、Fatalを分離し、予期しない例外をFatalへ固定する |
| Compatibility | Unreal Engine固有Macroと型へ依存する | UnityEngine ObjectとManaged APIへ依存する | Godot Core Macro、String、Objectへ依存する | First-party C++ APIに限定し、Plugin ABIへSTL、例外、`Result`を公開しない |
| Diagnostics | Category、Verbosity、Assert Family、Crash Reporter連携を持つ | Console Message、Object Context、Exception Logを持つ | File、Function、Line、Trace、Custom Loggerを持つ | Error Code、Native Error、Context、Cause、Source Locationを一つのRecordへ渡す |
| Testability | Engine MacroとOutput Deviceを含むHarnessが必要になる | Unity Test環境とConsole／Assertion挙動へ依存する | Engine Macroと登録Loggerを含むHarnessが必要になる | Logger、Sink、Fatal HandlerをInstance注入し、Process Testで終了も検証する |
| Complexity | 機能が豊富な分、Macro、Category、設定の選択が多い | Call Siteは単純だがManaged／Editor統合全体へ依存する | Macroは簡潔だが診断とControl Flowが結合する | 機能量を絞る代わりに、所有権、Lifetime、失敗経路を明示する必要がある |

比較の結果、M01では診断機能の量より、Runtime境界の明確さ、Portability、Data Safety、Testabilityを優先します。Category、Structured Field、Editor Object Context、非同期出力は実測要件と所有権設計が揃った後のIssueで検討します。

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
- First-party Module内で`std::bad_alloc`を捕捉した場合は、通常処理、通常Log、Fatal、Assertの区別なく、所有文字列、`Error`、`LogRecord`、残りのSink出力を新規生成せず、注入済みFatal HandlerのEmergency Entry Pointを直接呼ぶ。AdapterとModule Public Entry Pointは、この経路へ到達できる非所有参照を構築時に受け取る
- Fatal HandlerのOwnerは、その非所有参照を保持する全Module、Adapter、Loggerより長く生存する。Composition Rootは参照側をすべて破棄してからFatal Handlerを最後に破棄する
- Emergency Entry Pointは静的文字列Viewだけを受け取り、追加Allocationを行わず、`noexcept`かつ呼び出し側へ戻らない。既定Handlerは`std::abort`を使用する

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
- 0個以上のCause Frame

Context Frameは次を保持します。

- 呼び出しBoundaryで追加するUTF-8 Message
- `std::source_location`から取得したFile、Function、Line、Column

Native Error情報はNative Headerへ依存しない次のValueで保持します。

- Domainを表すUTF-8文字列
- Native値を損失なく格納する符号付き64-bit Code

Cause Frameは、再分類前のErrorを構造化して保持する非再帰Valueです。

- `ErrorCode`
- UTF-8 Summary
- 0個以上のContext Frame
- 任意のNative Error情報

新しいErrorへ再分類する場合、直前のErrorの所有Dataを最初のCause FrameへMoveし、そのErrorが既に持つCause Frameを後ろへ移します。これにより、新しいPrimary ErrorからImmediate Cause、Root Causeの順に辿れます。

再分類先の抽象Error自身にNative Errorがある場合は、`Error::reclassify`のNative Error付きOverloadを
使用する。このOverloadは新しいPrimaryへNative Errorを所有させ、直前のErrorと既存Causeを同じ順序で
Cause ChainへMoveする。Native Error付きPrimaryを必要とする呼出側が、CodeやNative値を文字列Contextへ
変換して構造を失うことを許可しない。この追加はFirst-party Static Library向けSource APIであり、安定ABI
またはPlugin ABIにはしない。

Error系列の所有型はMove-onlyとします。

- `ErrorCode`、`Error`、Context Frame、Cause FrameはCopy constructorとCopy assignmentを削除する
- Move constructorとMove assignmentは`noexcept`とし、所有Bufferを移して追加Allocationを行わない
- 所有UTF-8文字列を生成するConstructorは非公開とし、Error生成、Context追加、再分類はEmergency Handlerへの非所有参照を受けるFactory／Mutation APIだけで行う
- Allocationを伴うAPIは内部で`std::bad_alloc`を捕捉し、追加AllocationなしでEmergency Entry Pointを呼ぶ。部分更新したErrorを呼び出し側へ返さない
- M01ではCopy可能なError Snapshot APIを提供しない。必要性が確認された場合はAllocation失敗契約と一緒に別Issueで設計する

`Result<T>`の規則:

- 成功時の`T`または失敗時の`Error`の正確に一方だけを保持する
- Default構築を許可せず、明示的なSuccessまたはFailure Factoryから生成する
- `Result<T>`と`Result<void>`はCopyを禁止し、Move-onlyとする
- `T`は`noexcept` Move ConstructibleかつDestructibleであることをCompile-timeに要求し、Resultの状態移動から例外を出さない
- `Result<void>`は成功状態またはErrorの一方だけを保持する
- `has_value()`と明示的`operator bool`で状態を確認できる
- `try_value()`と`try_error()`は非所有Pointerを返し、状態が一致しない場合は`nullptr`を返す
- 返却PointerはResultが同じ状態で生存している間だけ有効とする
- Probe APIは`&`と`const &`修飾だけを提供し、`&&`と`const &&`からの呼び出しを削除して一時ResultからのPointer取得を禁止する
- 状態を前提とするAccess APIを追加する場合、誤用はProgrammer ErrorとしてAssertする
- ErrorへContextまたはCauseを追加する操作はError所有権を維持し、元のError Code、Native Error、既存Causeを失わない
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

- 再分類しない伝播ではPrimary Error Codeを保持し、Context追加で上書きしない
- Context追加でError Codeを別Codeへ上書きしない
- 抽象Boundaryで意味が変わる場合だけ、新しいErrorを作る。この場合のPrimary Error Codeは新しい抽象Levelの失敗を表す
- 再分類前のPrimary Error Code、Summary、Context、Native情報、既存CauseはCause Chainへ構造化して残す
- Root Cause CodeはCause Chainの最後のFrameにあり、CauseがないErrorではPrimary Error Code自身をRoot Cause Codeとする
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

Fatal DispatcherはRecord構築とLogger呼び出しを例外境界で囲み、Sink失敗、予期しないSink例外、Mutex取得失敗があってもFatal Handlerを必ず呼びます。Fatal用`log_and_flush`はMutexを待機せず`try_lock`し、競合時はRecord出力を諦めてEmergency Entry Pointへ直行します。`std::bad_alloc`でも通常のRecord構築、残りのSink処理、結果集約を中止してEmergency Entry Pointへ直行します。Fatal Handler自身の契約違反、OSによる強制終了、Process破損のようにC++で制御できない事象は保証対象外です。

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

`LogRecord`はLevel、UTF-8 Message、Source Location、任意の`Error`をValueで所有するMove-only Typeです。Errorを持つRecordはError所有権をMoveで受け取り、Error Code、Summary、Context、Native Error、Cause ChainをSinkへ渡せます。Sinkが受け取る`LogRecord`参照は同期`write`呼び出し中だけ有効であり、Sinkは参照または内部Pointerを保持しません。Timestamp、Thread ID、Category、Structured Fieldは実測要件が確認されるまで必須にしません。

LoggerとSinkの規則:

- Loggerは通常のObjectとしてComposition Rootまたは上位Ownerが一意所有する
- LoggerはEmergency Entry Pointを持つFatal Handlerへの非所有参照を受け取る。Fatal HandlerのOwnerはLoggerより長く生存し、Composition RootはLoggerを先に破棄する
- Loggerは0個以上の`std::unique_ptr<LogSink>`を受け取り、Sinkを一意所有する
- Global Logger、Global Sink Registry、Service Locatorを導入しない
- `Logger::log`と`Logger::flush`は同期かつ非例外APIとし、同じLogger内部Mutexで`write`と`flush`を直列化する。通常Log Recordの構築もLogger Entry Pointの例外境界内で行う
- 各Logger Entry PointはMutex取得前に、Allocation不要なThread-local Sink Dispatch Guardを確認する。いずれかのLoggerがSinkを呼び出しているThreadでは、同一・別Instanceを問わずLoggerへ再入せずEmergency Entry Pointを呼ぶ
- Sink Dispatch Guardは呼び出しStack上のScope ObjectとThread-localなDepth Counterだけで構成し、Heap Allocationを行わない。Sink呼び出しの直前にCounterを増やし、Scope終了時に以前の値へ戻す
- 通常APIのMutex待機取得が例外を投げた場合、Loggerは内部で捕捉し、診断経路へ再入せずFatal HandlerのEmergency Entry Pointを直接呼ぶ。Mutex再入やResource異常を回復可能なLog失敗へ降格しない
- Fatal経路は`Logger::log_and_flush`を使用し、Mutexを`try_lock`で一度だけ取得する。取得成功時は同じLock保持中にFatal Recordの全Sinkへの`write`と全Sinkの`flush`を完了し、他ThreadのRecordを割り込ませない
- Fatal用Mutexが競合した場合は待機せず`Contended`を返し、Fatal DispatcherはLoggerを迂回してEmergency Entry Pointを呼ぶ
- 一つのRecordは構成された各Sinkへ一度ずつ、登録順に渡す
- SinkはLoggerのLock保持中に呼ばれ、同じThreadからは直接・間接を問わずいずれのLoggerへも再入してはならない
- M01のSinkは`write`／`flush`処理を呼び出されたThread上で完了し、Worker Thread、Task Queue、非同期Callbackへ処理またはRecord Dataを委譲しない
- 非同期SinkはLoggerとSinkのShutdown Protocol、Task所有権、Drain、破棄順序を決める別Issueまで導入しない
- Loggerが直列化するため、個別Sinkは同じLoggerからの並行`write`へ対応する必要がない
- 複数Loggerから同じSinkを共有しない。共有要件が確認された場合は別の同期所有設計を行う
- Sinkの`write`と`flush`は成功可否をAllocation不要な値で返し、例外を投げない契約とする
- LoggerはSinkが非例外で失敗を返した場合だけ残りのSinkを処理し、`Success`、`SinkFailure`、`Contended`のAllocation不要な結果を返す。Sink失敗を同じLoggerへ再Logしない
- Loggerは契約違反のSink例外を各呼び出し単位で捕捉し、種類を問わず残りのSink処理を中止してEmergency Entry Pointを直接呼ぶ。例外をModule境界へ出さず、通常実行へ戻らない
- 通常Log Record構築、Mutex取得、Sinkの`write`／`flush`のいずれかで`std::bad_alloc`を捕捉した場合も、Loggerは残りの処理と結果集約を中止してEmergency Entry Pointを直接呼ぶ
- Fatal Dispatcherは`log_and_flush`の失敗結果または例外にかかわらずFatal Handlerを呼び、診断I/O失敗を理由に安全でない実行へ復帰しない
- Console Sinkは出力またはFlush失敗を`false`で報告してRecordを破棄する。再帰Logや追加Allocationを伴うFallbackは行わない

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
- 境界内部で発生した文書化済みの回復可能例外だけをError Codeへ変換する。予期しない例外は境界内で捕捉してFatal終了し、Recoverable Errorへ降格しない

## Test Strategy

### Result and Error

- SuccessがValueだけを保持する
- FailureがErrorだけを保持する
- `try_value()`と`try_error()`が状態に応じてPointerまたは`nullptr`を返す
- Probe APIが右辺値Resultから呼び出せないことをCompile-time Testで検証する
- `Result<void>`の成功と失敗を検証する
- Context追加後もError Code、Native Error、既存Contextが保持される
- Error再分類後も元Error Code、Summary、Context、Native Error、既存CauseがCause Chainに保持される
- Native Error付き再分類後は、新しいPrimaryのNative Errorと、Immediate CauseからRoot Causeまでの順序が
  構造化されたまま保持される
- Error再分類後のPrimary Error Codeが新しい抽象Levelを表し、Root Cause CodeがCause Chain末尾から取得できる
- ErrorとResultがCopyできないことをCompile-time Testで検証する
- Move後のResultが元の状態と値を保持する

### Logger

- Test SinkでLevel、Message、Source Location、出力順を検証する
- 複数ThreadからLogし、Record欠落とData Raceがないことを検証する
- 複数Sinkへ一度ずつ登録順に出力されることを検証する
- Flushが全Sinkへ伝播することを検証する
- Fatalの`log_and_flush`中に他ThreadのRecordが割り込まず、`write`と`flush`が競合しないことを検証する
- Fatal時にLogger Mutexが競合すると待機せず`Contended`を返すことを検証する
- Sinkから同一または別Loggerへ再入するとMutex取得前に検出され、Emergency Entry Pointから規定終了することをProcess Testで検証する
- First-party SinkがWorker Thread、Task Queue、非同期Callbackへ処理またはRecord Dataを委譲していないことを実装Reviewで確認する
- 通常LoggerのMutex取得例外が内部で捕捉され、Emergency Entry Pointから規定終了することをProcess Testで検証する
- Sinkが非例外で失敗を返す場合は残りのSinkが処理され、Sinkが例外を投げる場合は残りのSinkが呼ばれずEmergency Entry Pointから規定終了することを検証する
- Error付きRecordがError全体をValue所有し、元ErrorのLifetime終了後もSink処理中に参照できることを検証する

### Assert and Fatal

- DebugとDevelopmentでAssert条件が評価されることを検証する
- ReleaseでAssert条件が評価されないことを副作用Counterで検証する
- 成功条件ではLogとFatal Handlerが呼ばれないことを検証する
- 失敗条件は子Processで実行し、Fatal Recordと規定終了Codeを検証する
- Fatal Handlerが呼び出し側へ戻らないことを型とProcess Testで検証する
- Sinkの`write`／`flush`が非例外で失敗を返した後にもFatal Handlerが呼ばれ、Sink例外ではEmergency Entry Pointが呼ばれることをProcess Testで検証する
- Fatal時のLogger Mutex競合とSinkの`std::bad_alloc`後にEmergency Entry Pointから規定終了することをProcess Testで検証する
- `std::bad_alloc`用経路が通常のLogRecordを構築せずEmergency Entry Pointから規定終了することをProcess Testで検証する
- Platform／RHI Adapterで捕捉した`std::bad_alloc`も同じ非Allocation経路から規定終了することをProcess Testで検証する

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
- Errorの再分類ではCause FrameをValue所有するため追加Allocationが発生する
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
- Issue #47でNative Error付き`Error::reclassify` Overloadと保持順序のTestを追加する
