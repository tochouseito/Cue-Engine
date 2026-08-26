# ADR-0010: Secondary Error Diagnostic Composition

- Status: Accepted
- Date: 2026-08-26
- Decision Owners: CueEngine Project
- Research Issue: [#110](https://github.com/tochouseito/CueEngine/issues/110)
- Implementation Issue: [#120](https://github.com/tochouseito/CueEngine/issues/120)
- Extends: ADR-0005 Error, Assert, and Log Policy

## Context

一つの処理が失敗した後の Cleanup、Rollback、Device Removal Recovery でも別の失敗が起きる場合があります。このとき最初の失敗を Primary Error として維持しながら、後続の Secondary Error も失わずに診断へ残す必要があります。

現在は同じ目的の処理が次の4箇所に重複しています。

- RuntimeHost は Secondary Error の Summary、Code、Native Error、Context、Cause Chain を Primary Error の Context へ転記する
- D3D12 Backend は Shutdown / Rollback の Secondary Error と Cause Chain を同様に転記する
- D3D12 Queue は Secondary Error と Cause Chain に加え、`ErrorCause` 単体を転記する経路を持つ
- D3D12 Frame Command は Secondary Error の Summary、Code、Native Error、Context だけを転記し、Cause Chain は転記しない

各実装は文字列構築、Allocation 失敗処理、転記順序がわずかに異なります。このままでは Cause や Native Error の保持量が呼び出し側によって変わり、修正が複数箇所へ分散します。

ADR-0005 は Error の Primary identity、Context、Native Error、再分類による Cause Chain を決定していますが、独立して発生した Secondary Error の構造化所有方法は決めていません。M07 では既存の Error Layout と将来の ABI / Serialization を先回りして変更せず、現在の診断表現を共通化する必要があります。

## Decision

`Cue.Foundation` の `Error` に、Secondary Error を Primary Error の Context へ診断展開する共通 Mutation API を追加します。

### API Boundary

実装 Issue #120 では、次の責務を持つ Overload を `Error` へ追加します。

```cpp
/// @brief Secondary Error の診断を Primary Error の Context へ転記する
void Error::append_secondary_diagnostics(
    const AssertContext& a_assertContext,
    const Error& a_secondaryError,
    std::string_view a_context,
    std::string_view a_label,
    std::source_location a_location = std::source_location::current()) noexcept;

/// @brief Secondary Cause の診断を Primary Error の Context へ転記する
void Error::append_secondary_diagnostics(
    EmergencyHandler& a_emergencyHandler,
    const ErrorCause& a_secondaryCause,
    std::string_view a_context,
    std::string_view a_label,
    std::source_location a_location = std::source_location::current()) noexcept;
```

- 呼び出し対象の `Error` が Primary Error であり、所有権は呼び出し側に残る
- Secondary Error / ErrorCause は呼び出し中だけ参照し、所有権を移動しない
- Secondary Error を受ける Overload では、`a_secondaryError` が呼び出し対象の Primary Error とは異なる Object を参照することを前提条件とする。Debug / Development は注入された`AssertContext`を使う`CUE_ASSERT`で違反を診断して終了する
- Release は Assert 式を生成せず、自己参照の場合は Primary Error を変更せずに戻る安全 Guard を残す。自己参照による合成は全 Build で行わない
- `a_context` は「どの後続処理が失敗したか」を表す呼び出し側固有 Message とする
- `a_label` は `Secondary Runtime Error`、`Secondary shutdown Error`、`Secondary Queue Error`、`Secondary Frame Command Error` のような診断 Prefix とする
- `a_location` は共通 Helper ではなく実際の呼び出し位置を、新しく生成する境界 Context と識別 Context に記録する
- 既存の `retain_secondary_error` などの呼び出し側 Wrapper を残す場合、その Wrapper も既定値付き `std::source_location` を受け取り、`a_location` へ転送する。転送できない Wrapper は削除し、実際の Call Site から共通 API を直接呼び出す
- API は共有可変状態を持たない。Primary Error の Mutation は ADR-0005 と同様に外部同期を要求する

### Diagnostic Order and Format

`Error` Overload は次の順序で Context を追加します。

1. 呼び出し側の `a_context`
2. Secondary Error の Summary
3. `<label> Code=<domain>/<value>`
4. Native Error がある場合は `<label> NativeError=<domain>/<value>`
5. Secondary Error 自身の Context を元の順序で追加
6. 各 Cause を Immediate Cause から Root Cause の順で追加
   - `<label> Cause`
   - Cause Summary
   - `<label> Cause Code=<domain>/<value>`
   - Native Error がある場合は `<label> Cause NativeError=<domain>/<value>`
   - Cause Context を元の順序で追加

`ErrorCause` Overload は `a_context`、Summary、`<label> Code=...`、任意 Native Error、Context の順で追加します。

Secondary Error / Cause が既に持つ Context を転記するときは Message だけでなく元の `SourceLocation` も保持します。境界 Message、Summary、Code、Native Error、Cause Marker は `a_location` を使用します。

### Primary Identity and Structure

- Primary Error の Code、Summary、Native Error、既存 Context、既存 Cause Chain、Root Cause を変更しない
- Secondary Error は Primary Error の Cause Chain へ追加しない
- Secondary Error 専用の所有 Field、Suppressed Error 配列、再帰 Error Tree を追加しない
- `Error`、`ErrorCause`、`ErrorContext` の Data Member と Layout を変更しない
- Plugin ABI、DLL ABI、Serialization 形式は決定しない

Secondary Error を Cause Chain へ追加しない理由は、Cause Chain が ADR-0005 で「同じ失敗を上位抽象へ再分類した履歴」を表すためです。Cleanup などで独立して発生した失敗を同じ Chain に混ぜると、Root Cause の意味と順序が変わります。

### Capacity and Allocation Failure

- M07 では Context 数、Cause 数、文字列長に新しい上限や Truncation を導入しない
- Secondary Error とその既存 Cause / Context を省略せず、有限の入力 Span 全体を転記する
- Secondary Error Overload の文字列構築または Context 追加の Allocation 失敗は`a_assertContext.fatal_handler()`の Emergency Entry Point へ渡し、通常実行へ戻らない
- `ErrorCause` Overload の Allocation 失敗は`a_emergencyHandler`の Emergency Entry Point へ渡し、通常実行へ戻らない
- Allocation 失敗を Secondary Error や部分成功 Result へ変換しない
- `noexcept` 境界から C++ 例外を出さない

既存 `Error::add_context` と同じ Fatal 契約を使用し、部分的な診断だけを持つ Error を通常経路へ返しません。

## Rejected Alternatives

### Secondary Error を Cause Chain へ追加する

不採用です。再分類履歴と独立した後続失敗が混在し、`root_code()` が最初の処理失敗を表さなくなる可能性があります。

### Suppressed Error 配列を Error に追加する

今回は不採用です。構造化表示には有効ですが、Error Layout、Copy / Move、Logger、将来 Serialization、Plugin Adapter への影響を同時に決める必要があります。必要性を実測した時点で別 Research Issue とします。

### RuntimeHost または D3D12 TestSupport に共通 Helper を置く

不採用です。Foundation の Error 表現を操作する処理が上位 Module に残り、RuntimeHost と複数の D3D12 所有者から再利用できません。

### Cause / Context 数を固定上限で切り捨てる

不採用です。現在は上限契約がなく、M07 で暗黙に診断情報を減らす根拠がありません。診断量が問題になった場合は、測定条件と表示 / 保存要件を揃えて別途設計します。

## Consequences

### Positive

- Secondary Error の保持順序と情報量を Foundation で一貫して検証できる
- D3D12 Frame Command でも Secondary Cause Chain を失わなくなる
- 呼び出し側固有 Label と Context を残しながら重複実装を削除できる
- Primary identity と既存 Error Layout を維持できる
- 転記元 Context の SourceLocation を保持できる

### Negative

- Secondary Error は構造化 Field ではなく Context として保持されるため、将来の UI は文字列 Format を解析せずそのまま表示する必要がある
- Secondary Error が大きい場合は転記分の Allocation と Context 走査 Cost が発生する
- `Error` の公開 C++ API が増えるが、安定 ABI としては提供しない

## Verification

Issue #120 では次を検証します。

- Primary Error の Code、Summary、Native Error、既存 Context、Cause Chain、Root Cause が変わらないこと
- Secondary Error の Summary、Code、Native Error、Context、全 Cause と Cause Context が規定順で追加されること
- Secondary `ErrorCause` 単体が規定順で追加されること
- 転記元 Context の SourceLocation と、新規 Context の呼び出し位置が保持されること
- Native Error または Cause がない場合も余分な Context を追加しないこと
- RuntimeHost、D3D12 Backend、D3D12 Queue、D3D12 Frame Command の既存 Label と Failure Matrix を維持すること
- Label、Code、Native Error などの診断文字列構築中の Allocation 失敗が、注入済み Emergency Handler の規定経路で終了すること
- Context 追加中の Allocation 失敗が、注入済み Emergency Handler の規定経路で終了すること
- 自己参照を渡した Process Test が、Debug / Development では注入済み`AssertContext`の Assert 経路で終了し、Release では Primary Error を変更せず復帰すること
- Debug / Development / Release Build と全 Test

## Non-goals

- Error Serialization 形式
- Suppressed Error の構造化公開 API
- DLL / Plugin ABI
- Context 容量上限と Truncation Policy
- 非同期 Log または Editor 診断 UI
