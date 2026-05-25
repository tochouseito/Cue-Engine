# スクリプトシステム設計メモ

## 0. 目的

Cue Engine のスクリプトシステムは、使い始めやすさよりも先に、DLL 境界の安全性、ホットリロード耐性、Editor 連携のしやすさを確保する必要があります。

特に重要なのは、Engine 側の内部構造が今後も変化する前提で、Script 側へその都合を漏らさないことです。DLL 境界に C++ オブジェクトや STL をそのまま通す設計は、短期的には楽でも長期的には破綻しやすいため採用しません。

---

## 1. 採用方針

### 1.1 本命は C API 境界 + 関数テーブル + Opaque Handle

スクリプトシステムの中核は、以下の構成を前提にします。

- DLL 境界は C API のみとする
- やり取りする実体は Opaque Handle のみとする
- Engine の機能公開は `CueEngineApi` のような関数テーブル経由で行う
- Script 側は Engine の内部型を知らない

DLL 境界で扱う値は、たとえば以下のようなハンドルと関数テーブルに限定します。

- `EntityHandle`
- `ComponentHandle`
- `ScriptInstanceHandle`
- `CueEngineApi*`

この方式では、`GameScript.dll` は `Engine.dll` の内部 class を参照しません。知るのは C のエントリポイント、関数テーブル、ハンドルだけです。

### 1.2 想定する構成

- `Engine.dll` が `CueEngineApi` を組み立てて Script 側へ渡す
- `GameScript.dll` は `extern "C"` のエントリポイントを持つ
- スクリプトの生成、破棄、更新は C 関数越しに行う
- スクリプトの実体データは DLL 内部で保持する
- Engine 側はスクリプト実体をハンドル経由で管理する

### 1.3 Component の公開方針

既存の ECS Component は Engine 内部用として扱い、Script 側へそのまま公開しません。

Script 側が触るのは、内部 Component の実体ではなく、安定した外部公開 API と必要最小限の POD データです。分離の方針は以下です。

- Engine 内部では既存の Component をそのまま使う
- Script 側へは `EntityHandle` や `ComponentHandle` を公開する
- 必要なデータだけを `CueTransformData` のような POD で受け渡す
- Script 側は `get`、`set`、`has` のような API 経由で Component を操作する
- Script 用の薄い C++ ラッパは作ってよいが、内部 ECS 型は見せない

たとえば `TransformComponent` を Script から使いたい場合でも、外部へ公開するのは内部 struct そのものではなく、以下のような形を前提にします。

- `CueTransformData`
- `has_transform(EntityHandle)`
- `get_transform(EntityHandle, CueTransformData*)`
- `set_transform(EntityHandle, const CueTransformData*)`

`CueTransformData::rotation` は通常の Transform API では弧度法、Degrees API では度数法の Euler 回転として扱います。

この形にしておけば、内部の ECS 実装や Component レイアウトを変更しても、Script 側の契約を保ちやすくなります。

### 1.4 この方式を採用する理由

- ABI を安定させやすい
- DLL 跨ぎの所有権事故やメモリ破壊を減らせる
- Engine 内部実装を変更しても Script 側への影響を抑えやすい
- Component の内部表現を Script 側から隠せる
- ホットリロードの前提を作りやすい
- 将来 C#、Lua、Python など別言語を導入するときも境界設計を使い回せる

### 1.5 欠点

- 直感的な C++ の書き味は落ちる
- 最初の API 設計に手間がかかる
- 関数テーブル設計が雑だと使い勝手が悪くなる

それでも、この案が最も安いです。初期コストは上がりますが、後で境界事故や依存汚染を修正するコストの方がはるかに大きくなります。

---

## 2. 自動生成リフレクションは第2層として載せる

### 2.1 位置付け

`CUE_CLASS()`、`CUE_FIELD()`、`CUE_FUNCTION()` のようなマクロとコード生成によるリフレクションは、有力ですが中核ではありません。これは 1 章の C API 境界の上に載せる補助機能として扱います。

先に境界契約を固め、その後で Editor 連携や保存復元のためにメタデータを足していく構成にします。

### 2.2 役割

コードジェネレータは、たとえば以下の情報を生成します。

- クラス登録コード
- フィールド列挙コード
- ファクトリ
- シリアライズ情報
- Editor 表示用メタデータ

### 2.3 利点

- Unity や Unreal Engine に近い運用感へ寄せやすい
- Inspector と連携しやすい
- スクリプト変数の保存と復元を実装しやすい
- シーン JSON と接続しやすい
- `GameObject`、`Component`、`Script` の関係を明示しやすい

### 2.4 注意点

- ツールチェーンを自作する必要がある
- パーサが甘いとすぐ壊れる
- C++ の完全構文解析を自前でやるのは重すぎる

そのため、最初から完全なリフレクションは狙いません。初期段階では、以下だけ拾えれば十分です。

- class 名
- 継承
- 公開フィールド
- 基本型
- enum
- `Component` 参照

C++ 全文法を食べる設計は避けます。そこに踏み込むと、ツールの維持コストが実装本体を圧迫します。

---

## 3. 採用案のまとめ

本命は以下の組み合わせです。

- C API 境界
- Opaque Handle
- `EngineApi` 関数テーブル
- 外部公開用の POD と Component 操作 API
- Script 側の薄い C++ ラッパ
- 自動登録マクロ
- 必要最小限のリフレクションコード生成

この構成は、以下の用途に向いています。

- Cue Engine の中核として長期運用する
- Editor と連携する
- ホットリロードを行う
- PDB を使ったデバッグを維持する
- 将来の別言語対応へ備える

問題は、初期設計がやや重いことだけです。しかし、ここを省くと後から境界再設計が必要になり、全体の作業量が増えます。

---

## 4. 開発ツール連携

### 4.1 Script の編集環境は Visual Studio を前提にする

Script は Cue Engine 独自エディタだけで閉じず、通常の C++ 開発と同じように Visual Studio 上で編集、ビルド、デバッグできるようにします。

狙いは、以下の開発体験を最初から確保することです。

- Script のソースを Visual Studio で直接編集できる
- `GameScript.dll` を Visual Studio から単体ビルドできる
- Editor 側からビルド要求を出せる
- ビルド開始、成功、失敗を Editor へ通知できる
- 実行中プロセスへ Visual Studio デバッガをアタッチできる

Script システムが独自ワークフローに閉じると、日常的な修正とデバッグのコストが上がります。既存の IDE とデバッガをそのまま使えるようにする方が、長期的には安定します。

### 4.2 Visual Studio 連携用の C# プロジェクトを用意する

Visual Studio との連携機能は、Script Runtime 本体へ直接混ぜず、Editor 用の補助ツールとして C# プロジェクトを別で用意します。

この C# プロジェクトの責務は、たとえば以下です。

- Visual Studio へのビルド要求
- ビルド状態の取得
- ビルド結果の通知
- 実行中プロセスへのデバッガアタッチ要求
- 必要なら Solution や Project の検出

Visual Studio 連携を C# 側へ分離する理由は、Windows と Visual Studio 固有の自動化処理を Editor 本体や Engine 本体へ汚染させないためです。これは Runtime の移植性とも衝突しません。

### 4.3 Editor と補助ツールの関係

Editor はビルドシステムやデバッガそのものを持つのではなく、C# の補助ツールへ要求を出す側に徹します。

役割分担は以下を前提にします。

- Editor は「何をしたいか」を要求する
- C# ツールは Visual Studio 固有 API や自動化処理を扱う
- 実際の Script Runtime や ABI 契約は C++ 側で維持する

この分離により、Script の実行契約と開発補助契約を切り分けられます。Visual Studio 連携の都合で Script ABI を歪めないことが重要です。

### 4.4 通知とアタッチで必要になる最小機能

最初に必要なのは、過剰な統合ではなく、以下の最小機能です。

- Editor から Script ビルドを要求できる
- ビルド中、成功、失敗を受け取れる
- 失敗時にログやエラー要約を Editor で表示できる
- 実行中の Editor または Runtime プロセスへデバッガをアタッチできる

最初から Solution 生成、コード補完支援、完全な IDE 制御まで広げる必要はありません。まずはビルド通知とデバッガアタッチを確実に通すべきです。

---

## 5. 避けるべき設計

### 5.1 DLL 境界で STL や C++ の都合をそのまま通す

`std::string`、`std::vector`、`std::function`、例外、RTTI を DLL 境界で跨がせる設計は採用しません。ABI 依存が強く、所有権や解放責務も曖昧になりやすいためです。

### 5.2 Engine の内部 class を Script 側へ見せる

これをやると、Script 側が Engine 内部実装へ強く依存します。Engine の構造変更がそのまま Script の破壊変更になり、長期運用に耐えません。

### 5.3 ECS の内部 Component をそのまま Script 側へ公開する

`TransformComponent` や `CameraComponent` のような内部 Component を、そのまま Script DLL から `#include` させる設計は採用しません。Script 側へ公開するのは、安定した API、Handle、POD データだけです。

内部 Component は Engine の都合で変更しやすく、ECS の最適化やメモリ配置の変更も入り得ます。その実体を外へ見せると、Script 側が内部事情に引きずられ、ABI と実装の両方が壊れやすくなります。

### 5.4 最初から Unreal Engine 級の完全リフレクションを目指す

初期段階で必要なのは、Inspector 表示、保存復元、最低限の型情報です。巨大なツールチェーンを先に作るのは費用対効果が悪く、実装速度を落とします。

### 5.5 ホットリロード時に既存メモリをそのまま生かす

クラスサイズ変更、vtable 変更、フィールド追加が入ると破綻しやすいため、この方向は避けます。状態は serialize して restore する前提に寄せるべきです。

---

## 6. 段階的な実装順

### 6.1 第1段階

まずは最小構成で Script を動かします。

- `GameScript.dll` をロードする
- C API で `register_scripts()` を呼ぶ
- `create`、`destroy`、`update` を実装する
- `EntityHandle` 経由で最低限の Engine API を使えるようにする
- 内部 Component を直接見せず、外部公開用の POD と `get`、`set`、`has` API を定義する

### 6.2 第2段階

次に、Editor と保存復元の基礎を作ります。

- `CUE_SCRIPT`、`CUE_FIELD` マクロを導入する
- コード生成でスクリプトレジストリを作成する
- Inspector で public field を表示する
- JSON 保存と復元に対応する

### 6.3 第3段階

次に、Visual Studio 連携用の開発ツールを入れます。

- Script 編集用の Visual Studio プロジェクト構成を整える
- Editor から補助 C# ツールへビルド要求を出せるようにする
- ビルド開始、成功、失敗を Editor へ通知できるようにする
- 実行中プロセスへデバッガアタッチできるようにする

### 6.4 第4段階

その後でホットリロードを組み込みます。

- DLL を再読み込みする
- state の serialize と restore を行う
- 変更された型の互換性をチェックする

### 6.5 第5段階

Script から利用できる Engine API を広げます。

- Input
- Audio
- Physics
- Prefab
- Spawn
- イベントシステム接続

### 6.6 第6段階

最後に、パフォーマンス重視のロジックと Script を併用できる形へ進めます。

- ECS System 登録型との併用
- 重い処理を System 側へ逃がす運用

---

## 7. 結論

Cue Engine のスクリプトシステムは、最初から「使いやすい C++ 直結」を目指すのではなく、「壊れにくい DLL 境界」を先に固定するべきです。

採用方針は、C API 境界 + 関数テーブル + Opaque Handle を中核にし、その上へ自動登録と限定的なリフレクションを段階的に載せる形が妥当です。さらに、開発体験の面では Visual Studio を正面から利用し、Editor とは別に C# の補助ツールでビルド通知とデバッガアタッチを扱う構成が適しています。この分離であれば、Runtime の境界安全性を崩さずに、Script 開発の実用性も確保できます。
