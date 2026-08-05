# foo_component_update_checker 開発ログ

foobar2000 64bit版コンポーネント。導入済みコンポーネントの更新有無をインターネット上(初期はGitHub Releases)で確認し、あれば通知する。DLLの自動ダウンロード・自動インストールは行わない。

---

## 2026-08-05 プロジェクト発足・設計方針の確定

### 経緯

他のAI(ChatGPT)と設計を詰めていたが、以下5文書が出力されていた。

- Project Charter
- Design Principles(DP-0001〜DP-0040)
- Glossary
- Requirements Specification
- Architecture Design(約1400行)

内容の方向性自体は妥当だったが、分量が個人開発の規模に対して明らかに過剰(企業のガバナンス文書レベル、ADR/RFC運用、トレーサビリティID体系、Design Freeze手続きなど)。foo_albumtrainと同じく「実装しながら開発ログ・READMEを更新する」進め方に一本化することにした。

以下、5文書から実装に使う部分だけを抽出したもの。

### Mission

foobar2000の軽快な再生体験を妨げずに、導入済みコンポーネントの更新情報を必要なときだけ知らせる。更新確認は補助機能であり、音楽再生より常に優先度が低い。

### 非目標(やらないこと)

- DLLの自動ダウンロード・自動インストール・自動置換・自動再起動
- コンポーネントの安全性/品質の保証
- 音楽ライブラリ・再生履歴・プレイリスト等の外部送信
- 32bit版への積極対応

### 基本方針(判断に迷ったとき立ち返る基準)

- **Playback First**: ネットワーク通信・フォルダ走査・JSON解析は非同期。メインスレッドをブロックしない
- **Non-Intrusive**: 自動確認時、更新なし/通信失敗は原則通知しない。更新ありのときだけ小さく通知
- **Offline Friendly**: 通信不能でもfoobar2000本体は普通に使えること
- **Accurate Before Convenient**: バージョン比較できない場合は誤判定せず「比較不能」として公開ページへの導線だけ出す
- **Identity Before Name**: コンポーネント識別は表示名だけに頼らない。GUID + DLL名を併用(SDKから安定した単一GUIDが必ず取れるとは限らないため、GUID単独に依存する設計にはしない)
- **No Automatic Installation**: 更新確認・通知・公開ページ誘導までが責任範囲。それ以降はユーザー任せ

### Phase 1 スコープ(最初に作るもの)

1. 導入済みコンポーネントの検出(DLL名、GUID候補、表示名、バージョン、作者)
2. ユーザーによるGitHub Repository URLの手動登録
3. GitHub Releases APIから最新リリース取得
4. バージョン比較(数値/SemVer寄り、非対応表記は「比較不能」)
5. 結果一覧表示 + 各コンポーネントのリリースページをブラウザで開く
6. 手動確認(Helpメニュー)
7. 自動確認(起動後、遅延実行、既定間隔7日、更新ありの時だけ通知)
8. 設定・キャッシュはJSON保存(config / user-components / update-cache 程度に分離。Registryは将来用なのでPhase 1では持たなくてよい)

Phase 1でやらないもの: Remote Registry、Verified Entry、GitLab対応、SQLite、Preferences統合。

### ラフなモジュール構成(クラス分割の指針。ファイルは実装しながら決める)

- **Scanner**: foobar2000 SDKからインストール済みコンポーネント一覧を取得(GitHub APIやUIには触れない)
- **Identity解決**: DLL名+GUID候補+表示名からComponent Identityを作る。GUID一致を強い根拠、DLL名一致をフォールバックとして扱う
- **Repository Mapping**: どのコンポーネントをどのRepositoryと紐付けるか(Phase 1はユーザー登録のみ)
- **GitHub Release Provider**: GitHub Releases APIを叩いて共通のRelease情報(バージョン、URL、公開日、prerelease/draftフラグ)に変換する。GitHub固有の型はここから外に出さない
- **Version比較**: Raw文字列は保持したまま正規化→比較。比較不能は握りつぶさず明示する状態として返す
- **Notification Policy**: 自動確認は「更新ありのときだけ通知」、手動確認は「常に結果を返す」で分岐
- **設定・キャッシュ(JSON)**: 設定/ユーザー登録/更新結果キャッシュはファイルを分ける。1つ壊れても他に影響させない

### 未検証のSDK事項(実装前に確認する)

1. ロード済みコンポーネントの名称/バージョン/モジュール情報を列挙する正式な方法
2. DLL識別に使えるGUID情報の範囲(安定して取れるか)
3. 初期化完了後にバックグラウンド処理を安全に始める方法
4. 終了時のキャンセル・待機の作法
5. Main Threadへ処理を戻す正式な方法
6. HelpメニューへのMain Menu Command登録方法
7. SDK/helpersにHTTP機能があるか、なければWinHTTPを使う
8. Portable Modeと通常Modeでの保存先の違い

### 次にやること

- 上記SDK検証事項を実際に手を動かして確認
- v0.1として「コンポーネント一覧取得→手動でGitHub URL登録→手動確認→結果表示」だけの最小構成を作る

---

## 2026-08-06 リポジトリ作成・ビルド環境構築

GitHubにprivateリポジトリ`foo_component_update_checker`を作成。foo_albumtrainの`.vcxproj`/`.slnx`を土台に流用してプロジェクト一式を用意した。

### 構成

- ソリューション形式は`.slnx`(XML形式)。foo_albumtrainと同じく、SDK側の5プロジェクト(`pfc`, `foobar2000_SDK`, `foobar2000_component_client`, `shared`, `columns_ui_sdk`)をBuildDependencyとしてソリューションに含める構成
- `SDK-2025-03-07`フォルダと`WTL10_01_Release`フォルダはfoo_albumtrainからコピーして流用(SDK本体はリポジトリにコミットしない方針)
- `dllmain.cpp`に`DECLARE_COMPONENT_VERSION`のみの最小構成を配置

### つまずいた点

- **C4819警告(文字コード)**: `dllmain.cpp`に日本語コメントを含めたままBOM無しで保存していたため発生。BOM付きUTF-8で保存し直して解消。日本語コメントを含むソースファイルは今後もBOM付きUTF-8で統一する
- **LNK1104: foobar2000_SDK.lib が見つからない**: 最初にSDK側のプロジェクトをソリューションに含めていなかったため、`.lib`が一度もビルドされていなかったのが原因。foo_albumtrainの`.slnx`を参考に、SDK5プロジェクトをBuildDependencyとして追加したら解消

### 結果

6プロジェクト(本体 + SDK5つ)がDebug x64構成でビルド成功。`foo_component_update_checker.dll`の生成を確認。

### 次にやること

- SDK検証タスク(前回リストアップした8項目)に着手
- 特に「ロード済みコンポーネント一覧を取得する方法」から手を動かして確認する
