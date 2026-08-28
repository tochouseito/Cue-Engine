# ADR-0012: System and Graphics Capability Contract

- Status: Accepted
- Date: 2026-08-29
- Decision Owners: CueEngine Project

## Context

M08では、実行中のMachineとGraphics Adapterが利用できる機能を、Game Projectの互換性判定、Runtime機能選択、診断へ渡す基盤を確立する。

Capabilityを単一の`bool`で表すと、Hardwareが未対応なのか、Queryできなかったのか、CueEngineが未実装なのか、実装済みだが設定で無効なのかを区別できない。TierとVersionを`bool`へ潰すと、より高いCapabilityが下位Capabilityを包含する関係も失われる。

System CapabilityはPlatformが取得し、Graphics CapabilityはRHI BackendがNative APIから取得する。一方、Game Projectは特定Machineの結果を保存するのではなく、実行に必要なCapability条件を保持する必要がある。

このADRはCapabilityの公開Vocabulary、所有権、Snapshot、Thread Safety、Project互換性判定への入力を決定する。実際のWindows QueryとD3D12 QueryはIssue #131と#132で実装する。

## Legacy Reference

### Legacy Problem

旧CueEngineもCPU命令、Graphics Feature Level、Shader Model、Optional Graphics Featureを確認し、利用可能な実装経路を選ぶ必要があった。

### Legacy Approach

旧実装ではNative APIの戻り値または個別の`bool`を利用箇所で直接確認する設計が中心で、System、RHI、Game Project互換性の共通契約は確立していなかった。

### Legacy Strengths

- Native APIに近い場所でCapabilityを取得できた
- 必要なFeatureだけを小さく確認できた
- Graphics初期化時に必須Feature不足を検出できた

### Legacy Problems

- Supported、Implemented、Enabledの意味が呼び出し側ごとに異なった
- Query失敗がUnsupportedへ変換されると、DriverまたはAPI失敗をHardware不足と誤診断した
- Native enumと構造体が上位へ漏れると、PlatformまたはGraphics APIの追加が難しくなった
- 現在Machineの値とProjectが要求する値の境界が不明確だった
- TierまたはVersionを`bool`へ変換すると、将来の上位値を表現できなかった
- Resource FormatやSample Countに依存する値を起動時Snapshotへ固定すると、Query入力が失われた

### Current Requirements

- Native OS型、CPUID Register、D3D12／DXGI型を公開APIへ出さない
- Hardware Support、Query結果、Engine実装、Runtime有効状態を区別する
- Unknown、Unsupported、QueryFailedを診断可能にする
- SystemとGraphicsの所有Moduleを分離する
- Immutable Snapshotを複数Threadから安全に参照できるようにする
- TierとVersionを型付き値として保持する
- Game ProjectにはRequirementを保存し、現在MachineのHardware Snapshotを保存しない
- Resource条件依存Queryを固定Snapshotへ混ぜない

## Reference Engine Comparison

| Engine | 参考にする点 | CueEngineでそのまま採用しない点 |
| --- | --- | --- |
| Unreal Engine | RHI Feature LevelとShader Platformを分け、利用可能なRendering経路を上位から判断できる | GlobalなCapability値とPlatform固有TableをCueEngineの小さいM08契約へそのまま導入しない |
| Unity | `SystemInfo`により現在DeviceのFeatureとLimitを共通入口から参照できる | Hardware結果を`bool`中心のFlat APIとして公開せず、Query失敗とEngine実装状態を別に保持する |
| Godot | `RenderingDevice`がFeature Queryと数値Limit Queryを分けて公開する | Resource依存Queryまで固定Snapshotへ集約せず、RHI所有の明示的なQuery契約に分ける |

CueEngineは共通入口のUsabilityを取り入れる一方、Data SafetyとDiagnosticsを優先し、Hardware、Query、Implementation、Enablementを独立した値として保持する。

## Decision

### Capability Vocabulary

PlatformとRHIの双方が使用するPlatform非依存Vocabularyは`Cue.Foundation`が所有する。`Cue.Foundation`は実際のHardware Queryを行わず、状態を表す値型だけを提供する。

```cpp
namespace cue
{
enum class CapabilityQueryStatus
{
    NotQueried,
    Succeeded,
    Failed,
};

enum class CapabilitySupport
{
    Unknown,
    Unsupported,
    Supported,
};

enum class CapabilityImplementation
{
    NotImplemented,
    Implemented,
};

enum class CapabilityEnablement
{
    NotApplicable,
    Disabled,
    Enabled,
};

class CapabilitySupportState final
{
  public:
    /// @brief 未QueryでSupportが不明な状態を返す
    [[nodiscard]] static constexpr CapabilitySupportState not_queried() noexcept;

    /// @brief Query失敗によりSupportが不明な状態を返す
    [[nodiscard]] static constexpr CapabilitySupportState query_failed() noexcept;

    /// @brief Query成功により対応済みと判明した状態を返す
    [[nodiscard]] static constexpr CapabilitySupportState supported() noexcept;

    /// @brief Query成功により未対応と判明した状態を返す
    [[nodiscard]] static constexpr CapabilitySupportState unsupported() noexcept;

    /// @brief Query結果の分類を返す
    [[nodiscard]] constexpr CapabilityQueryStatus query_status() const noexcept;

    /// @brief Hardware Supportの分類を返す
    [[nodiscard]] constexpr CapabilitySupport support() const noexcept;

  private:
    /// @brief 検証済みのQuery状態とSupport状態から内部生成する
    constexpr CapabilitySupportState(
        CapabilityQueryStatus a_queryStatus,
        CapabilitySupport a_support) noexcept;

    CapabilityQueryStatus m_queryStatus;
    CapabilitySupport m_support;
};

class CapabilityState final
{
  public:
    /// @brief 未QueryかつEngine未実装で有効化対象外の状態を返す
    [[nodiscard]] static constexpr CapabilityState not_queried_not_implemented() noexcept;

    /// @brief 未QueryかつEngine実装済みで有効化対象外の状態を返す
    [[nodiscard]] static constexpr CapabilityState not_queried_implemented() noexcept;

    /// @brief Query失敗かつEngine未実装で有効化対象外の状態を返す
    [[nodiscard]] static constexpr CapabilityState query_failed_not_implemented() noexcept;

    /// @brief Query失敗かつEngine実装済みで有効化対象外の状態を返す
    [[nodiscard]] static constexpr CapabilityState query_failed_implemented() noexcept;

    /// @brief Hardware未対応かつEngine未実装で有効化対象外の状態を返す
    [[nodiscard]] static constexpr CapabilityState unsupported_not_implemented() noexcept;

    /// @brief Hardware未対応かつEngine実装済みで有効化対象外の状態を返す
    [[nodiscard]] static constexpr CapabilityState unsupported_implemented() noexcept;

    /// @brief Hardware対応済みだがEngine未実装の状態を返す
    [[nodiscard]] static constexpr CapabilityState supported_not_implemented() noexcept;

    /// @brief Hardware対応・Engine実装済みだがRuntime無効の状態を返す
    [[nodiscard]] static constexpr CapabilityState supported_disabled() noexcept;

    /// @brief Hardware対応・Engine実装済みでRuntime有効の状態を返す
    [[nodiscard]] static constexpr CapabilityState supported_enabled() noexcept;

    /// @brief Hardware Support状態を返す
    [[nodiscard]] constexpr CapabilitySupportState hardware() const noexcept;

    /// @brief CueEngine実装状態を返す
    [[nodiscard]] constexpr CapabilityImplementation implementation() const noexcept;

    /// @brief Runtime有効状態を返す
    [[nodiscard]] constexpr CapabilityEnablement enablement() const noexcept;

  private:
    /// @brief 検証済みの3状態から内部生成する
    constexpr CapabilityState(
        CapabilitySupportState a_hardware,
        CapabilityImplementation a_implementation,
        CapabilityEnablement a_enablement) noexcept;

    CapabilitySupportState m_hardware;
    CapabilityImplementation m_implementation;
    CapabilityEnablement m_enablement;
};
} // namespace cue
```

状態型はAggregateにせずFieldを非公開にする。`CapabilitySupportState`は有効な4状態、`CapabilityState`は有効な9状態の引数なし名前付きFactoryだけから生成する。Default Constructor、任意値Constructor、Public Setterを提供せず、呼び出し側がInvariantを迂回できない型とする。

不正な組合せは回復可能なRuntime入力ではなくProgrammer Errorである。任意のenum値または3状態を受け取るFactoryを提供せず、型の公開生成経路から不正状態を表現不能にする。各Factoryは固定した有効値だけをPrivate Constructorへ渡し、全Factoryの戻り値をCompile-time AssertionとUnit Testで検証する。無効状態を`Result`の通常分岐またはContextなしのRuntime Assertへ変換しない。

`Unknown`、`Unsupported`、`QueryFailed`は次の組合せで表す。

| Meaning | `queryStatus` | `support` |
| --- | --- | --- |
| Query対象外またはまだQueryしていないため不明 | `NotQueried` | `Unknown` |
| Query成功により未対応と判明 | `Succeeded` | `Unsupported` |
| Query成功により対応と判明 | `Succeeded` | `Supported` |
| Query自体が失敗したため不明 | `Failed` | `Unknown` |

次の組合せは不正な状態としてFactoryまたはTestで拒否する。

- `NotQueried`と`Supported`または`Unsupported`
- `Failed`と`Supported`または`Unsupported`
- `Succeeded`と`Unknown`
- `Enabled`または`Disabled`と`NotImplemented`
- `Enabled`または`Disabled`と`Succeeded + Supported`以外のHardware状態
- `NotApplicable`と`Succeeded + Supported + Implemented`

`QueryFailed`を`Unsupported`へ変換しない。Native Errorの詳細はModule境界で`cue::Error`とLogへの変換を試みるが、Immutable SnapshotにはNative Error型または所有権を持つError Chainを格納しない。Log配送失敗はQuery Reportの独立した状態として呼び出し側へ返す。

### Supported, Implemented, and Enabled

各状態の責務を次のように固定する。

| State | Owner | Meaning |
| --- | --- | --- |
| Supported | PlatformまたはRHI Query | 現在のHardware、OS、Driver、Backend組合せが機能を提供できる |
| Implemented | CueEngine Module | 現在BuildのCueEngineに、その機能を利用するProduction経路が存在する |
| Enabled | Runtime Composition | 現在の起動設定、Project Profile、Fallback選択を反映し、実際にその経路を使用する |

HardwareがSupportedでもCueEngineが未実装ならEnabledにしない。HardwareがUnsupported、Unknown、またはQueryFailedなら、対応するProduction経路をEnabledにしない。

`Disabled`は「SupportedかつImplementedだが、設定またはPolicyにより使用しない」状態を表す。`NotApplicable`は未対応、Query結果不明、未実装などにより有効化候補ではない状態を表す。

### Tier and Version Values

TierとVersionを`bool`へ変換しない。各Feature Familyは意味を持つ専用enumまたはVersion値型を使用する。

```cpp
struct CapabilityVersion final
{
    std::uint16_t major;
    std::uint16_t minor;
};

enum class ResourceBindingTier
{
    Tier1,
    Tier2,
    Tier3,
};
```

具体的なTier enumは所有Moduleに置き、Native enum値と数値互換を契約にしない。Query結果は`CapabilityQueryStatus`と組み合わせ、Query失敗時にTier 0などの架空値へFallbackしない。

Versionは辞書順で比較可能なMajor／Minor値とし、文字列またはNative定数を永続Identityにしない。上位VersionまたはTierが下位要件を満たすかはFeature Familyごとの比較関数で判定する。

未知の将来Native値は、既知の最大値へ切り下げず`Unknown`または変換失敗として扱う。上位値を安全に包含できることが仕様上保証され、かつ元の数値を保持できる専用型を設計した場合だけ別判断とする。

### System Capability Snapshot

`Cue.Platform`は`SystemCapabilitySnapshot`の型とPlatform非依存契約を所有する。Windows実装は`Cue.Platform.Windows`の`query_windows_system_capabilities()`から完成済みSnapshotを所有値で返し、Composition RootがRuntimeで使用するSnapshotを所有する。

- Process ArchitectureとNative Machine Architecture
- Logical Processor Count
- Physical Memory Byte数
- Page SizeとCache Line Size
- CPU Instruction Feature Support
- CPU ContextをOSが保存・復元できるかを含む実行可能状態

CPU命令はCPUID BitだけでSupportedとしない。AVX系などOS Context Saveが必要な機能は、CPU SupportとOS Supportの両方を満たした場合だけ実行可能なSupportedとする。

SnapshotはWindows Platform初期化中に完成させ、公開後は変更しない。Native Handle、CPUID Register配列、Windows構造体、可変Cacheを含めない。Query全体は各FieldのQuery状態を含むSnapshotを必ずReport内の値で返し、個別のOS Query失敗をQuery全体の`Result`失敗へ変換しない。

```cpp
struct SystemCapabilityQueryReport final
{
    /// @brief 完成済みSystem Capability Snapshot
    SystemCapabilitySnapshot snapshot;
    /// @brief Native Error診断の全Log配送結果
    LogResult diagnosticResult;
};

/// @brief 現在MachineのWindows System Capabilityと診断配送結果を所有値で返す
/// @param a_assertContext Query失敗のNative Errorを同期Logする非所有診断Context
/// @return 完成済みSnapshotと全診断の集約LogResult
[[nodiscard]] SystemCapabilityQueryReport query_windows_system_capabilities(
    const AssertContext &a_assertContext) noexcept;
```

個別のOS Queryが失敗した場合は、そのFieldを`Failed + Unknown`にして、`a_assertContext.logger()`へNative Errorを同期出力する。全診断が配送できた場合は`diagnosticResult = Success`とする。一件でもSink配送に失敗した場合は`diagnosticResult = SinkFailure`とする。このAPIは通常の`Logger::log()`を使用するため、`Contended`を返却可能状態として扱わない。

`SinkFailure`でもQueryを再試行せず、Loggerへ再帰せず、暗黙にFatal終了せず、完成済みSnapshotを返す。呼び出し側は`diagnosticResult`を確認し、Console以外のEmergency診断または起動終了が必要かをComposition RootのPolicyとして判断する。これによりNative Error配送の欠落を無視せず、System Capability Query自体をGlobal状態へ結合しない。

Sink CallbackからのLogger再入は既存Loggerの契約違反であり、LoggerがEmergency Handlerを呼び出してProcessを終了する。この経路からSystem Capability Query Reportは返らない。Logger再入を回復可能なCapability Query失敗へ変換しない。

返却値は呼び出し側が所有し、`WindowSystem`または存在しないPlatform Runtime Objectの寿命へ結び付けない。別ThreadへCopyまたはMoveしたSnapshotは、元のPlatform Objectの有無に依存せず安全に読み取れる。`AssertContext`とその参照先はQuery呼び出し完了まで有効とし、返却Snapshotへ保持しない。

Query APIは共有可変状態、Lazy Cache、Thread-local Cacheを持たず、任意Threadから呼び出せる。同じまたは異なる`AssertContext`を使用した並行呼び出しを許可し、各呼び出しは独立したReportを返す。共有する`Logger`は既存のThread-safe契約に従い、`FatalHandler`と任意Debug Break Callbackの並行利用可否は呼び出し側が保証する。M08のComposition Rootは初期化Threadから一度だけ呼び出すが、これは公開APIのThread Affinity要件ではない。

`Cue.Platform`はHardware Supportの事実だけを報告し、GameCore、Renderer、Script等がその命令を実装・有効化しているかは判断しない。System Featureの完全な`CapabilityState`は、Platform SnapshotとEngine Implementation CatalogとRuntime設定をComposition Rootが組み合わせて作る。

### Graphics Capability Snapshot

`Cue.RHI`はPlatform非依存のGraphics Capability型を所有し、各BackendがNative Query結果を変換する。D3D12 BackendはD3D12／DXGI enum、`HRESULT`、COM Objectを公開Snapshotへ格納しない。

Graphics Snapshotには次の種類を含める。

- AdapterとBackendのIdentityおよびMemory分類
- Baseline Feature LevelとShader Model Version
- Root Signature Version
- Resource Binding、Ray Tracing、Mesh Shader、Variable Rate Shading、Sampler Feedback等のTier
- Wave Operation、Enhanced Barrier等のFeature State
- UMAとCache Coherent UMA等のArchitecture State

Optional `CheckFeatureSupport`が失敗しても、Baseline Backend生成条件を満たしていればBackend生成全体を失敗させない。そのFeatureだけを`Failed + Unknown`として記録し、Native失敗は診断Logへ残す。

Optional Query失敗のNative Errorを`Logger::log()`へ渡し、`LogResult::Success`なら`Failed + Unknown`を保持したBackend生成を継続する。`LogResult::SinkFailure`の場合は、Optional Query失敗そのものではなく診断配送失敗をPrimary Errorとし、同じNative ErrorをCauseとして再構築した`Result`失敗を`create_d3d12_backend()`から返す。Native ErrorがMove済みになるため、既存D3D12診断Helperと同様にNative CodeからCauseを再構築する。

したがって、Optional Capabilityの未対応またはQuery失敗だけではBackend全体を失敗させないが、診断配送Subsystemの失敗はBackend生成失敗となる。これをHardware UnsupportedまたはCapability QueryFailedとして報告せず、`Cue.RHI.D3D12`の診断配送Errorとして区別する。Logger再入は既存Logger契約どおりEmergency Handlerが非復帰終了し、Backend生成`Result`は返らない。

必須Baseline CapabilityのQuery失敗または未対応はBackend生成失敗とする。必須条件とOptional条件の一覧はBackend Policyで明示し、Query関数の偶発的な失敗処理へ埋め込まない。

Graphics BackendはSnapshotをBackend生成成功時に完成させ、Backend Objectの破棄開始前までImmutableな`const`参照として公開する。`shutdown()`はNative Resourceの停止と解放を行うが、Backend Objectが生存している間はSnapshotを無効化または変更しない。

### Snapshot Ownership and Thread Safety

SystemとGraphics Snapshotは次の規則に従う。

- 完成後に変更されない値型とする
- Lazy Query、内部Mutex、Native Handle、非所有Pointerを持たない
- Ownerから独立してCopyした値は安全に読み取れる
- 同じ独立Copyに対する異なるThreadからの同時Readを許可する
- Query処理中のBuilderまたはNative一時値を公開しない
- Owner Objectの破棄開始後まで`const`参照を保持できると保証しない

System Snapshotは`query_windows_system_capabilities()`の呼び出し側が所有する独立値であり、`const`参照を返すPlatform Ownerを設けない。RHI BackendはBackend Objectの破棄開始前までGraphics Snapshotを所有する。Backendの破棄中または破棄後も必要なGraphics診断値は、呼び出し側が破棄開始前にSnapshotをCopyして保持する。

ADR-0007に従い、`GraphicsBackend::capabilities()`の呼び出しと、その戻り値であるBackend所有`const`参照の読み取りはBackend生成Threadに限定する。Thread違反はProgrammer Errorとして既存RHI Assert契約で処理する。Worker ThreadへBackend所有参照を渡さず、Backend生成Thread上でSnapshotをCopyしてから独立値をMoveまたはCopyする。独立CopyのReadにはRHI Thread Affinityを適用しない。

Snapshotを更新する必要が生じるHot-plug、Adapter変更、Device Recoveryは、Generation付き再公開と利用側同期を決定する別ADRまで対象外とする。M08では起動時Snapshotを固定する。

### Resource-dependent Queries

次はResource Descriptorまたは入力値に依存するため、固定Snapshotへ格納しない。

- Format Support
- Format Plane Count
- FormatとSample CountごとのMultisample Quality Level
- Resource Dimension、Layout、Heap条件に依存するSupport
- Queue、Node、Protected Session等の入力依存Support

これらは将来`Cue.RHI`が所有するLive Query契約として、Platform非依存InputとResultを受け取る。Native enumへの変換とNative API呼び出しはBackend Private実装に限定する。

Live QueryのThread Affinity、Cache、Failure Policy、Device Removal時の挙動は、そのQueryを最初に使用するIssueで決定する。Snapshotに全Format×全条件の表を先回りして保存しない。

### Project Compatibility Input

Game Projectは現在MachineのSystemまたはGraphics Snapshotを保存しない。Project Dataへ保存できるのは、Version付きのCapability Requirementまたは選択可能なProfileだけとする。

互換性判定は概念上、次の入力を受け取る。

```text
ProjectCapabilityRequirements
    + SystemCapabilitySnapshot
    + GraphicsCapabilitySnapshot
    + EngineImplementationCatalog
    + RuntimeEnablementPolicy
    -> CompatibilityReport
```

RequirementはRequiredとPreferredを区別する。Required CapabilityがUnsupported、Unknown、QueryFailed、NotImplemented、Disabled、NotApplicableのいずれかなら、実行不可または明示Fallbackが必要な診断を返す。Preferred Capabilityの場合はWarningとFallback候補を返せる。

互換性判定はProject Fileを現在Machine向けに書き換えない。Hardware Snapshot、Adapter名、Vendor ID、Memory量を「前回動作したMachine」の正本として永続化しない。診断Evidenceとして別LogまたはSession Reportへ記録することは許可する。

Requirement SchemaにはVersionとMigration方針を持たせる。M08ではProject永続形式自体を確定せず、後続のGame Project MilestoneでSchemaとMigrationを決定する。

### Public Boundary

- `Cue.Foundation`: 共通状態VocabularyとVersion値型
- `Cue.Platform`: System Snapshot型
- `Cue.Platform.Windows`: Windows System Queryと所有Snapshot値を返すFactory
- `Cue.RHI`: Graphics Snapshot、Graphics Tier型、将来のLive Query契約
- `Cue.RHI.D3D12`: D3D12／DXGIから公開値への変換
- Composition Root: Implementation CatalogとEnablement Policyの統合
- Game Project: Version付きRequirement／Profile

PlatformはRHIへ依存せず、RHI公開契約へWindows型を渡さない。RHIはGame Project永続形式を所有しない。

## Validation

- 状態Vocabularyの全有効組合せと不正組合せをUnit Testする
- Query失敗とUnsupportedが異なる値になることをTestする
- Supported、Implemented、Enabledの全組合せをSynthetic InputでTestする
- Public Header単体CompileでWindows SDK、CPUID Intrinsic Header、D3D12／DXGI Header依存がないことを確認する
- TierとVersionの比較を境界値でTestする
- System QueryをSynthetic CPUID／OS入力で検証する
- D3D12 QueryをSynthetic Native結果で検証し、Optional Query失敗後もBackend生成可能であることをTestする
- Hardware／WARP Smokeで実環境の診断値を確認する
- ProjectへHardware SnapshotをSerializeする実装をReviewで拒否する

## Consequences

### Positive

- Hardware不足、Query失敗、Engine未実装、設定無効を別々に診断できる
- PlatformとGraphics API固有型を上位へ漏らさずにCapabilityを比較できる
- Project Requirementと現在Machineの事実を分離できる
- Optional Query失敗がBackend全体の不必要な起動失敗にならない
- TierとVersionの将来拡張を`bool`より安全に扱える
- Immutable SnapshotのReadに追加同期を必要としない

### Negative

- 単純な`bool`より型数と状態遷移Testが増える
- Query成功でもImplementationとEnablementを別途統合する必要がある
- Native Error詳細はSnapshotだけでは完結せず、LogまたはError Chainとの関連付けが必要になる
- 起動後のHot-plugまたはDevice RecoveryをM08 Snapshotだけでは表現できない

## Enforcement

- Issue #131はSystem SnapshotとSynthetic Queryをこの契約に従って実装する
- Issue #132はGraphics SnapshotとD3D12 Queryをこの契約に従って実装する
- Native enum、Native Handle、`HRESULT`をCapability公開型へ追加しない
- Optional Query失敗をUnsupportedへ変換しない
- TierとVersionを単一`bool`へ変換しない
- Resource条件依存Queryを固定Snapshotへ追加しない
- Project Dataへ現在MachineのSnapshotを保存しない

## Follow-up

- Issue #131: Windows System Capability Queryと`SystemCapabilitySnapshot`
- Issue #132: D3D12 Graphics Capability QueryとGraphics Snapshot拡張
- Issue #133: Runtime診断、3構成Test、M08 Completion Gate
- Game Project Milestone: Version付きCapability Requirement／Profile Schema
- Renderer着手時: FeatureごとのImplementation CatalogとEnablement Policy
- Device Recovery着手時: Snapshot Generation、再公開、Hot-plug同期

## References

- [Microsoft: Capability querying](https://learn.microsoft.com/en-us/windows/win32/direct3d12/capability-querying)
- [Microsoft: ID3D12Device::CheckFeatureSupport](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-checkfeaturesupport)
- [Microsoft: IsProcessorFeaturePresent](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocessorfeaturepresent)
- [Unreal Engine: RHI](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/RHI)
- [Unity: SystemInfo](https://docs.unity3d.com/ScriptReference/SystemInfo.html)
- [Godot: RenderingDevice](https://docs.godotengine.org/en/stable/classes/class_renderingdevice.html)
