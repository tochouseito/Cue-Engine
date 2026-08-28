# ADR-0011: Custom Math Conventions

- Status: Accepted
- Date: 2026-08-28
- Decision Owners: CueEngine Project

## Context

M08では、GameCore、Authoring Scene、Editor、将来のRendererから共有する独自Math基盤を確立する。
Math規約が未決定のまま型と演算を追加すると、座標系、行列の合成順、角度単位、失敗時の挙動、Memory Layoutが利用箇所ごとに分岐する。

初期Graphics APIはDirectX 12だが、Math公開契約をDirectXMathまたはD3D12の型と規約へ結合しない。
CPU最適化も公開型へ特定ISAの型、Alignment、命令要件を漏らさず、検証可能なScalar実装を正本として後から追加できる必要がある。

## Legacy Reference

### Legacy Problem

旧CueEngineも、Transform、Camera、Rendering、Editorで共有するVector、Matrix、Quaternionと変換処理を必要としていた。

### Legacy Approach

旧実装は独自のVector、Matrix4、Quaternionを持ち、行VectorとRow-major Matrixを採用していた。
VectorはTemplateとして整数、単精度、倍精度Aliasを提供し、Quaternionと主要な変換処理は単精度を使用していた。

### Legacy Strengths

- DirectXMathの型をEngine全体へ公開しなかった
- 行VectorとRow-majorという基本規約をHeaderで宣言していた
- Vector、Matrix、Quaternionを値型として利用できた
- RadianとDegreeの明示的な変換関数を持っていた

### Legacy Problems

- Zero Vectorの正規化は入力を変更せず成功したように見えた
- Zero Quaternionの正規化と逆演算はIdentityへ置換された
- 特異Matrixの逆演算もIdentityへ置換され、失敗を呼び出し側が検出できなかった
- Matrixの`operator==`は近似比較だが、他の型では比較規則が異なっていた
- 固定の絶対Epsilonと`numeric_limits::epsilon`の倍数が用途を区別せず使用されていた
- Template引数が存在するMatrixでも内部配列が`float`に固定されるなど、型契約とMemory Layoutが一致していなかった
- PerspectiveとOrthographicでDepth変換規約が一致していなかった
- Quaternion積と行Matrixの時間順合成の関係が公開契約として明示されていなかった
- Math型のMemory ImageをSerializationまたはGPU転送へ利用できるかが明確でなかった

### Current Requirements

- DirectXMathを使用しない
- Platform、RHI、Editorから独立したMath Moduleにする
- GameCore TransformとAuthoring Sceneで同じ演算規約を使用する
- 異常値を正常なIdentityへ暗黙変換しない
- Scalar正本を自動Testし、将来の独自SIMD実装と比較できるようにする
- 公開型のMemory LayoutとISA固有最適化を分離する
- Scene永続形式とGPU転送形式をC++ Object Layoutへ依存させない
- 座標系と合成順を型利用者が推測しなくてよいようにする

### Reference Engine Comparison

比較対象の規約は公開Documentationで確認し、CueEngineの要件へそのまま移植せず、境界変換とTrade-offを含めて評価する。

| Engine | 公開されている主な規約 | CueEngineへ採用する点 | 採用しない点と理由 |
| --- | --- | --- | --- |
| Unreal Engine | 左手座標系、`+X = Forward`、`+Y = Right`、`+Z = Up`。`FMatrix`系は行VectorとRow-majorを前提にする。`FMath`はPlatform Math実装を継承する。 | 行Vector、Row-major、MathをEngine基盤として共有する考え方は、時間順の合成を読みやすくし、旧CueEngineの経験とも整合するため採用する。 | Z-upとAxis割り当てはCueEngineのY-up要件に合わない。Platform Math継承と公開Template体系は、Scalar正本とPlatform非依存の小さいM08 Scopeを複雑にするため採用しない。 |
| Unity | 左手座標系、`+X = Right`、`+Y = Up`、`+Z = Forward`。Rotationは内部でQuaternionを保持し、EditorではEuler角を表示する。`Matrix4x4`はColumn-majorを公開する。 | World AxisはGame ProjectとEditorで直感的に共有でき、CueEngineの初期要件と一致するため採用する。Quaternionを正本とし、Euler角を編集用派生値とする考え方も採用する。 | Column-majorとColumn Vector系の境界をCueEngine内部規約には採用しない。Unity API互換を目的にせず、将来のSDK境界で明示変換する。 |
| Godot | 右手座標系、`+X = Right`、`+Y = Up`、Camera等は`-Z = Forward`。`Basis`はRotation、Scale、Shearを表現し、AxisをColumnとして公開する。 | TRSで表現できないShearを一般Matrixとして保持し、分解を失敗可能にする考え方はData Safetyの参考にする。 | 右手座標系、`-Z = Forward`、Columnとして公開するBasisは、CueEngineの左手World Axisと行Vector規約に合わないため採用しない。 |

Sources:

- [Unreal Engine: Coordinate System and Spaces](https://dev.epicgames.com/documentation/en-us/unreal-engine/coordinate-system-and-spaces-in-unreal-engine)
- [Unreal Engine: TMatrix2x2](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/TMatrix2x2)
- [Unreal Engine: FMath](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/FMath)
- [Unity: Rotation and orientation](https://docs.unity3d.com/2023.2/Documentation/Manual/QuaternionAndEulerRotationsInUnity.html)
- [Unity: Matrix4x4](https://docs.unity3d.com/ScriptReference/Matrix4x4.html)
- [Godot: Introduction to 3D](https://docs.godotengine.org/en/stable/tutorials/3d/introduction_to_3d.html)
- [Godot: Basis](https://docs.godotengine.org/en/stable/classes/class_basis.html)

### Decision Trade-offs

| 観点 | CueEngineでの判断 | 代償 |
| --- | --- | --- |
| Usability | Axis、行Vector、角度単位、合成順を一つの規約に固定し、名前付き変換を提供する。 | 他EngineやDCC Toolとの受け渡しでは規約変換が必要になる。 |
| Runtime Performance | 単純な値型とScalar正本を持ち、将来のSIMDはPrivate実装として追加する。 | M08時点ではPlatform最適化済みMath Libraryより低速な可能性があるため、性能向上を主張しない。 |
| Iteration Speed | GameCore、Scene、Editorが同じ型と固定値Testを共有する。 | `Result<T>`と明示的なToleranceにより、呼び出しCodeは短くならない。 |
| Extensibility | Platform、RHI、Rendererから独立した`Cue.Math` Targetに限定する。 | Integer、倍精度、SIMD、Projectionは必要性ごとに別設計が必要になる。 |
| Portability | Windows SDK、DirectXMath、ISA固有型をPublic Headerへ出さない。 | 外部APIごとに明示的なAdapterが必要になる。 |
| Data Safety | 非有限値、Degenerate値、特異Matrix、分解不能を診断可能な失敗として返す。 | ZeroやIdentityへ暗黙FallbackするAPIより利用側の分岐が増える。 |
| Compatibility | C++ Object Layout、Scene永続形式、GPU Layout、Plugin ABIを分離する。 | Memory Imageの直接保存や直接Uploadはできず、変換Costと実装が必要になる。 |
| Diagnostics | Error Domainと最小Error分類を固定し、Math内部ではLogしない。 | Error生成に`EmergencyHandler`の明示的な受け渡しが必要になる。 |
| Testability | Scalar正本、固定Basis、異常値、依存方向を自動Testする。 | Scalarと最適化経路の双方を将来維持するTest Costが発生する。 |
| Complexity | M08の型と演算集合を単精度の非Template APIへ限定する。 | 汎用Template Math Libraryより再利用範囲は狭い。 |

以上から、AxisはUnityに近く、行列代数はUnreal Engineに近いが、どのEngineのAPIまたはMemory Layoutも互換契約にはしない。
Godotの一般BasisがShearを保持できる点は参考にする一方、CueEngineの`Transform`は編集しやすいTRS、一般合成結果は`Matrix4`として分離する。
この組合せは外部Engineとの直接互換性より、CueEngine内部のUsability、Data Safety、Portability、Testabilityを優先した新規設計である。

### New Design

`Cue.Math`を`Cue.Foundation`だけに依存するFirst-party Static Libraryとして追加する。
M08では単精度のVector、Matrix、Quaternion、Transformを提供し、Scalar実装を正確性の正本にする。

World Spaceは左手座標系とし、正方向を`+X = Right`、`+Y = Up`、`+Z = Forward`とする。
Vectorは行Vector、MatrixはRow-major Storageとし、PointまたはDirectionを`value * matrix`で変換する。

角度の内部標準はRadianとする。
RadianとDegreeは異なるValue Typeで表し、暗黙変換と単位を持たないAngle引数を公開APIで使用しない。

正規化、逆演算、分解などの失敗可能な演算は、既存の`cue::Result<T>`で成功値または診断可能な失敗を明示的に返す。
Zero、特異値、非有限値をZeroまたはIdentityへ暗黙変換しない。

### Validation

- 既定軸と回転方向をBasis Vectorの固定値Testで検証する
- VectorとMatrixの変換結果を手計算した固定値と比較する
- Quaternion合成とMatrix合成の対応をTestする
- Zero、特異Matrix、NaN、Infinityを失敗経路のTestへ含める
- Public Header単体CompileとTarget依存方向を検査する
- Repository全体でDirectXMathのHeaderと型への依存を検査する
- Scalar実装を固定値TestとProperty Testで検証する

## Decision

### Module Boundary

新しいTargetを次の配置で追加する。

```text
Engine/Source/Math/
    CMakeLists.txt
    Public/Cue/Math/
    Private/

Engine/Tests/Math/
```

- Target名は`Cue.Math`とする
- Namespaceは`cue::math`とする
- `Cue.Math`は`Cue.Foundation`へだけ依存できる
- `Cue.Math`はPlatform、RHI、RuntimeHost、GameCore、Editorへ依存しない
- Public HeaderはWindows SDK、DirectXMath、D3D12、Compiler Intrinsic HeaderをIncludeしない
- Math Errorは`Cue.Foundation`のError契約へ変換する

### Scalar Scope

M08のRuntime幾何演算とTransformはIEEE 754単精度の`float`を正本とする。
ConfigureまたはCompile-time Testで`sizeof(float) == 4`と`std::numeric_limits<float>::is_iec559`を要求する。

初期公開型は次のとおりとする。

- `Vector2`
- `Vector3`
- `Vector4`
- `Matrix3`
- `Matrix4`
- `Quaternion`
- `Transform`
- `Radians`
- `Degrees`
- `Tolerance`

M08ではVectorとMatrixをScalar Templateにしない。
整数Vector、倍精度Vector、倍精度World Positionが具体的に必要になった場合は、演算集合、変換精度、Serialization、ABIを別Research Issueで決定する。

単純な四則演算はIEEE 754の演算結果をそのまま返し、NaNまたはInfinityを自動修正しない。
有限値を要求する処理は、状態を変更する前に`is_finite`相当の検査を行う。

### Coordinate System

World Spaceは左手座標系とする。

| Axis | Positive Direction |
| --- | --- |
| X | Right |
| Y | Up |
| Z | Forward |

Cross Productは右手則の代数定義を維持する。
左手World SpaceはBasisとTransform規約であり、Cross ProductのOperand順を暗黙反転しない。
Surface NormalまたはTangent Frameを構築するAPIは、期待する向きが分かる名前、引数順、Testを持つ。

Basisと正回転は次の固定値で定義する。

```text
cross(UnitX, UnitY) = UnitZ
rotate(UnitY, +90 degrees around X) = UnitZ
rotate(UnitZ, +90 degrees around Y) = UnitX
rotate(UnitX, +90 degrees around Z) = UnitY
```

Camera Projection、Clip-space Depth Range、Screen Y方向、Texture UV OriginはWorld Space規約と分離し、Renderer着手時の別ADRで決定する。

### Vector and Matrix Convention

- Vectorは行Vectorとして扱う
- Matrixは`m[row][column]`のRow-major Storageとする
- Point変換は`point * matrix`とする
- Direction変換は平行移動を適用しない明示的なAPIとする
- `Matrix4`の平行移動成分は最終行に置く
- 変換を時間順に`first`、`second`と適用する合成は`first * second`とする
- LocalからWorldへの階層合成は`local * parent * ... * root`とする
- TRS行列は`Scale * Rotation * Translation`とする

MatrixのStorage順と代数上のVector規約は別概念としてDocumentationとTestへ記録する。
外部API、Shader、File Formatへ受け渡す場合は、各境界で明示的に変換する。

### Angle Convention

- 三角関数と回転生成APIは`Radians`を受け取る
- `Degrees`から`Radians`への変換は明示的に行う
- Scalarの`float`をAngleとして受け取る公開APIを追加しない
- Euler角はTransformまたはScene回転の正本にしない
- Euler変換を追加する場合は、軸順とLocal／World Axisのどちらかを関数名と契約へ明記する
- `from_euler`のように回転順を省略したAPIを追加しない

### Comparison and Tolerance

- `operator==`と`operator!=`は全成分の完全一致を表す
- 完全一致は組込み`float`の比較規則に従い、`+0.0f`と`-0.0f`は一致し、NaNを含む同じ位置の成分は一致しない
- 近似比較は`is_near`などの明示的なAPIだけで行う
- 近似比較は絶対Toleranceと相対Toleranceを区別する
- Algorithm固有のToleranceを一つのGlobal定数へ統合しない
- 呼び出し側は単位、Scale、用途に対応した検証済み`Tolerance` Valueを渡す
- `Tolerance`は非負かつ有限な絶対値と相対値だけを保持し、Raw値からの生成失敗は`cue::Result<Tolerance>`で返す
- VectorとQuaternionの正規化およびQuaternion逆演算は検証済み`Tolerance`を明示的に受け取り、Magnitudeと`Tolerance`の絶対値を比較する
- Magnitude計算は最大絶対Componentで一度Scaleしてから二乗和を求め、IntermediateのOverflowとUnderflowを避ける
- Magnitudeが絶対Tolerance以下の値はDegenerateとして失敗し、入力を変更しない
- Quaternionの同一回転判定では`q`と`-q`を同一として扱う専用APIを使用し、成分比較と区別する

Testの期待値に使用するToleranceはProduction APIの暗黙既定値にしない。

### Fallible Operations

次の処理は失敗可能な演算として扱う。

- Zeroまたは十分に小さいVectorの正規化
- Zeroまたは十分に小さいQuaternionの正規化と逆演算
- 特異または数値的に特異なMatrixの逆演算
- 非有限値を含む値の正規化、逆演算、回転生成、分解
- Shearまたは数値的にDegenerateなBasisを含むMatrixからTRSへの分解

ADR-0005に従い、失敗可能な公開APIは`cue::Result<T>`を返し、失敗時に代替値を返さない。
Math固有のResult、Status付きValue、`std::optional`、失敗を表す`bool`を代替Error経路として追加しない。

Error生成には`cue::EmergencyHandler`への非所有参照が必要になるため、失敗可能なMath APIはその参照を明示的に受け取る。
成功経路ではErrorと所有文字列を生成せず、Emergency Handlerを呼び出さない。
失敗時はDomain `Cue.Math`のError Codeと開発者向けSummaryを持つErrorを生成し、Math Module内ではLogしない。

最小Error分類は次のとおりとする。

- Non-finite input
- Degenerate value
- Singular matrix
- Non-decomposable transform

Mutating APIを追加する場合は、全検証に成功するまで対象を変更しないStrong Failure Guaranteeを持たせる。

Matrixの数値的特異判定は固定した絶対Determinantだけで決めず、入力MatrixのScaleと選択したToleranceを考慮する。

呼び出し側が代替値を必要とする場合は、`value_or`相当の操作または用途固有のFallbackを呼び出し側で明示する。
AssertはEngine内部Invariantの検出に使用できるが、Project DataまたはRuntime入力から到達可能なMath Domain Errorの唯一の処理にしない。

### Quaternion Convention

- Component順は`x, y, z, w`とする
- `x, y, z`はVector部、`w`はScalar部とする
- Identityは`{ 0, 0, 0, 1 }`とする
- Quaternion積はHamilton Productとして定義する
- 行Vectorの時間順合成には、Operand順を明示する`compose_rotation(first, second)`相当の名前付きAPIを提供する
- 名前付き合成APIは`to_matrix(compose_rotation(first, second))`と`to_matrix(first) * to_matrix(second)`が一致する契約を持つ
- Raw Quaternion Valueは任意のComponentを保持できるが、Rotationとして使用するQuaternionは有限かつ指定Tolerance内で単位長であることを要求する
- `to_matrix`、Vector回転、`compose_rotation`はQuaternionと検証済み`Tolerance`を検証し、非有限または非単位Quaternionを`cue::Result<T>`の失敗として返す
- Rotation利用APIはQuaternionを暗黙正規化しない。正規化が必要な呼び出し側は、先に明示的なQuaternion正規化APIを呼ぶ
- Quaternionの符号は永続Identityとして扱わない

QuaternionからEuler角への変換は表示または編集用の派生値であり、Transformの正本へ戻す場合は回転順と不連続性を明示する。

### Transform Convention

`Transform`は次の値を所有する。

- Translation: `Vector3`
- Rotation: 正規化済み`Quaternion`
- Scale: `Vector3`

既定値はIdentity Transformとする。
PointへはScale、Rotation、Translationの順に適用する。

通常の`Transform`生成はTranslation、Rotation、Scaleと検証済み`Tolerance`を受ける失敗可能なFactoryを使用する。
TranslationとScaleは有限値を要求し、RotationはQuaternion規約に従って有限かつ単位長を要求する。
Factoryは不正なRotationを暗黙正規化しない。

有限な負ScaleはMirror Transformとして許可する。
有限なZero ScaleもForward変換とAuthoring Dataでは許可するが、そのTransformと生成Matrixは可逆ではない。
Transformの逆変換は`cue::Result<Matrix4>`を返し、いずれかのScale Magnitudeが指定Tolerance以下の場合に失敗する。

非一様Scaleを含むTransform同士の合成ではShearが生じ、一般には同じTRS表現へ正確に戻せない。
そのため、任意のTransform合成結果は`Matrix4`を正本として返す。
TRSへ戻す処理は失敗可能な明示的分解APIとし、Shearと特異Scaleを検出する。
非特異なReflectionは負Scaleを持つTransformとして許可し、どのAxisへ符号を割り当てるかを実装契約と固定値Testで決定する。
分解後の個別Scale符号が元のTransformと一致することは保証せず、再構築したMatrixが入力MatrixとTolerance内で一致することを保証する。

### Initialization

- `Vector2`、`Vector3`、`Vector4`の既定値はZeroとする
- `Matrix3`、`Matrix4`の既定値はIdentityとする
- `Quaternion`の既定値はIdentityとする
- `Transform`の既定値はIdentityとする
- Zero Matrixは名前付きFactoryで明示的に生成する
- 未初期化状態を公開APIの既定挙動にしない

### Memory Layout and SIMD

M08のMath型はStandard-layoutかつTrivially-copyableな値型とし、Componentを宣言順に連続配置する。

- `Vector2`は2個の`float`
- `Vector3`は3個の`float`
- `Vector4`は4個の`float`
- `Matrix3`は9個の`float`
- `Matrix4`は16個の`float`
- `Quaternion`は4個の`float`

Public型へISA固有のVector型を含めず、`float`より強いAlignmentをM08では要求しない。
将来のSIMD実装はPrivateなLoad／Storeと演算経路に限定し、Scalar実装と同じ公開型を使用する。

このLayoutは同一Build内のValue SemanticsとTestのための契約であり、安定ABIではない。
Math型のMemory ImageをScene、Prefab、Save Data、Network、Plugin ABIの永続形式として使用しない。
GPU Buffer Layoutにも直接使用せず、Renderer所有の明示的なGPU Data型へ変換する。

### Determinism

M08は異なるCompiler、CPU、Build構成間のBitwise Determinismを保証しない。
同じScalar実装と入力に対して、規定したTolerance内で数学的に一致することを保証する。

Fused Operation、Fast Math、演算順変更を導入する場合は、Scalar正本との差、Replay、Serialization、Network Determinismへの影響を別Research Issueで決定する。

## Consequences

### Positive

- GameCore、Scene、Editorが同じ座標系と合成順を共有できる
- DirectXMathとGraphics APIから独立したMath公開APIになる
- Zero、特異値、非有限値の失敗を呼び出し側が検出できる
- Scalar正本に対して将来の独自SIMD実装を比較できる
- C++ Layout、永続形式、GPU Layoutを分離できる
- Transformが表現できないShearを暗黙に失わない

### Negative

- 単純な正規化や逆演算でも失敗処理が必要になる
- 行Vectorに慣れていない利用者向けのDocumentationとTestが必要になる
- Integer Vectorと倍精度型はM08では提供されない
- ProjectionとClip-space規約はRenderer着手時まで未決定となる
- Transform合成結果を常にTRSとして扱うことはできない

## Enforcement

- Issue #127から#129はこのADRに従ってCue.Mathを実装・検証する
- Public HeaderのDirectXMath、Windows SDK、D3D12、Compiler Intrinsic依存をBuildで拒否する
- Coordinate Basis、Matrix合成、Quaternion合成、TRS順序を固定値Testで検証する
- Fallible OperationがZeroまたはIdentityを暗黙Fallbackとして返す実装を禁止する
- `operator==`へ近似比較を実装しない
- Math型を直接SerializationまたはGPU UploadするCodeをReviewで拒否する
- SIMD経路を追加する前にScalar正本との差を検証するIssueを作成する

## Follow-up

- Issue #127: Scalar、Angle、Vector型と基本演算
- Issue #128: Matrix、Quaternion、Transform
- Issue #129: 正確性、異常値、依存方向、DirectXMath非依存のCompletion Gate
- Renderer着手時: Projection、Clip-space、Screen、Shader Matrix境界のADR
- 必要性確認後: 倍精度World Position、Integer Vector、独自SIMD、DeterminismのResearch Issue
