# ADR-0001: Windows UTF 変換を Foundation の Windows 固有 Module へ分離する

- Status: Accepted
- Date: 2026-08-26
- Research Issue: [#109](https://github.com/tochouseito/CueEngine/issues/109)
- Implementation Issue: [#118](https://github.com/tochouseito/CueEngine/issues/118)

## Context

現在の Windows Runtime 経路には、Win32 の UTF-16 と Engine 内部の UTF-8 を相互変換する処理が複数あります。

- Cue.Platform.Windows
  - Window Title の UTF-8 から UTF-16 への変換
  - Command Line Argument の UTF-16 から UTF-8 への変換
  - 失敗を Cue.Platform.Windows の Error Code と Win32 Native Error に変換する
- Cue.RHI.D3D12
  - DXGI Adapter 名の UTF-16 から UTF-8 への変換
  - 失敗を Cue.RHI.D3D12 の Error Code と Win32 Native Error に変換する
  - DRED Object 名では変換失敗を Error にせず、既定名へ Fall Back する

これらは WideCharToMultiByte または MultiByteToWideChar を使う二段階変換、Strict Flag、長さ検証、Allocation 失敗処理が重複しています。一方で、呼び出し側が要求する Error Domain と失敗時の挙動は同一ではありません。

次の Architecture Invariant を維持する必要があります。

- Cue.Foundation 本体を Windows API に依存させない
- Cue.RHI.D3D12 から Window System を所有する Cue.Platform.Windows へ依存させない
- Platform 固有型を Platform 非依存の公開 API へ漏らさない
- 呼び出し側固有の Error identity と DRED の Best-effort 診断を維持する

## Decision

Windows UTF 変換の機械的な処理を、新しい Cue.Foundation.Windows Static Library へ集約します。

### Module Boundary

- Source は Engine/Source/Foundation/Windows に配置する
- Target 名は Cue.Foundation.Windows とする
- Cue.Foundation.Windows は Cue.Foundation にのみ依存する
- Cue.Foundation 本体には Windows Header、Windows Library、wchar_t API を追加しない
- Cue.Platform.Windows と Cue.RHI.D3D12 は Cue.Foundation.Windows を Private Link する
- Cue.RHI.D3D12 から Cue.Platform.Windows への依存は追加しない
- Runtime、Editor、Tools の公開 API へ Cue.Foundation.Windows を再公開しない

### Conversion Contract

Cue.Foundation.Windows は次の Primitive を提供します。

- UTF-8 から Windows UTF-16 への Strict 変換
- Windows UTF-16 から UTF-8 への Strict 変換
- 成功時に呼び出し側が渡した所有 std::string または std::wstring を更新する
- 失敗時は出力を空にし、失敗種別と Win32 Native Error Code を返す
- 空入力は空出力として成功する
- 明示された長さを使い、埋め込み NUL を終端として扱わない
- MB_ERR_INVALID_CHARS と WC_ERR_INVALID_CHARS を使い、不正 Sequence を置換しない
- int で表現できない入力長は Native 呼び出し前に拒否する
- Allocation 失敗は AssertContext の Fatal Handler へ委譲し、例外を公開境界から出さない
- 共有可変状態を持たず、引数と AssertContext の寿命が有効なら並行呼び出し可能とする

Primitive は Engine の Error を生成しません。失敗種別と Native Code のみを返し、意味付けは呼び出し側が行います。

### Error Ownership

- Platform の Window Title と Command Line は、既存の Cue.Platform.Windows Error Code と Summary を維持する
- DXGI Adapter 名は、既存の Cue.RHI.D3D12 Error Code 23 と Summary を維持する
- DRED Object 名は Best-effort のままとし、変換失敗時は既定名へ Fall Back する
- 共通 Primitive の Error Domain を新設して上位へそのまま伝播しない

これにより、変換 Algorithm は共有しながら、Primary Error identity と診断 Context の所有者を変更しません。

## Rejected Alternatives

### Cue.Foundation 本体へ Win32 変換を追加する

不採用です。Platform 非依存の最下層 Target に Windows Header と Win32 契約が入り、将来の非 Windows Runtime が不要な依存を受けます。

### Cue.Platform.Windows の Public API を D3D12 から利用する

不採用です。RHI の Adapter 列挙と DRED 診断が Window System Module に依存し、Rendering と Platform Windowing の境界が逆転します。

### Platform の Private Header を D3D12 から直接 Include する

不採用です。CMake Target 境界を迂回し、Private 実装の所有権と変更影響範囲が不明確になります。

### Platform 非依存の独自 Unicode Codec を実装する

今回は不採用です。Windows Runtime だけが必要とする範囲に対して、Unicode Codec、char16_t / wchar_t 変換、全 Platform の検証責任を同時に導入します。非 Windows Host が必要になった時点で別 Research Issue とします。

### 現在の重複を維持する

不採用です。Strict Flag、長さ検証、Allocation 失敗処理の修正が複数箇所へ分散し、同一不具合の再発リスクが残ります。

## Consequences

### Positive

- Win32 UTF 変換の Algorithm と境界条件を一箇所で検証できる
- Platform と RHI はそれぞれの Error Domain と Fall Back 方針を維持できる
- Cue.Foundation 本体の Cross-platform 性を維持できる
- Window System と RHI の直接依存を避けられる

### Negative

- Windows 専用の Foundation Submodule と CMake Target が一つ増える
- Internal API であっても std::string / std::wstring を扱うため、将来 DLL Boundary に出す場合は ABI 再設計が必要になる
- wchar_t が UTF-16 である前提は Windows Target 内に限定して明示的に検証する必要がある

## Verification

Issue #118 では次を検証します。

- Empty、ASCII、日本語、補助平面文字、埋め込み NUL の往復
- 不正 UTF-8、不正 Surrogate、入力長 Overflow の拒否
- 失敗後に出力が空であること
- Platform の既存 Error Code / Native Error
- DXGI Adapter 名の既存 Error Code / Native Error
- DRED 名変換失敗時の Fall Back
- Foundation Dependency Test で Cue.Foundation 本体に Windows 依存がないこと
- RHI Dependency Test で Cue.Platform.Windows への依存がないこと
- Debug / Development / Release Build と全 Test

## Non-goals

- Platform 非依存 Unicode Normalization
- Locale 依存変換
- UTF-32 変換
- Public Plugin ABI
- Error Serialization 形式の決定
