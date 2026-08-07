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

---

## 2026-08-06(続き) SDK検証タスク#1: コンポーネント一覧取得

`component_scanner.cpp`を追加。Helpメニューに診断用コマンドを登録し、`componentversion`サービスを実装している登録済みコンポーネントを列挙して`console::print`でログ出力する最小実装を作った。

### つまずいた点

- `enumerate_service<componentversion>()`という自由関数は存在しなかった(C3861)。正しくは、各サービスインターフェースが`FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT`マクロ経由で自動的に持つ静的メソッド`enumerate()`を使う。`componentversion`は同マクロでentrypoint宣言されているため、`componentversion::enumerate()`が使える(`service.h`の`FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT_EX`定義で確認)
- 修正後、ビルド成功

### 分かったこと

- `componentversion::enumerate()`で`for (auto ptr : componentversion::enumerate())`のレンジベースforが使える(`service_enum_t<T>`がbegin/end/operator!=を実装しているため)
- 取得できる情報は`get_file_name` / `get_component_name` / `get_component_version` / `get_about_message`の4つ。**GUIDに相当する情報はcomponentversionインターフェースには含まれていない**ことが判明。DLL名・表示名・バージョンまでは容易に取れるが、GUIDでの識別を前提にした設計(Design Principles DP-0011)は、少なくともこの経路では実現できない。GUID取得の可否は別のSDK検証タスクとして改めて調べる必要がある

### 次にやること

- foobar2000上で実際にHelpメニューから実行し、コンソール出力を確認(ユーザー側で実施予定)
- GUID取得の可否を別途調査(componentversion以外のAPIを探すか、DLL名のみでの識別に設計を寄せるかを判断)
- 残りのSDK検証タスク(Main Thread Dispatcher、Portable Mode保存先など)に順次着手

### 動作確認結果

foobar2000上で実行。Helpメニューから「Check Component Updates (Debug: list components)」を実行し、コンソールに一覧が表示されることを確認。**検出件数はPreferences → Componentsに表示される件数と一致**しており、`componentversion::enumerate()`で導入済みコンポーネントを漏れなく取得できることを確認した。SDK検証タスク#1は完了。

### 次にやること

- GUID取得の可否を別途調査(componentversion以外のAPIを探すか、DLL名のみでの識別に設計を寄せるかを判断)
- 残りのSDK検証タスク(Main Thread Dispatcher、Portable Mode保存先など)に順次着手

### 設計判断: コンポーネント識別はDLL名を主とする

GUID取得の調査に時間をかけるより、Phase 1の規模(ユーザーが手動でGitHub URLを登録する運用)であればDLL名だけで実用上十分と判断。DLL名の重複は実質起きないため、これを主識別子とし、表示名は補助情報として扱う方針に変更した。

これに伴い、Design Principlesの「DP-0011: GUID+DLL名を併用して識別する」は「DLL名を主識別子とし、表示名を補助情報として扱う。GUIDは将来必要になった時点で改めて調査する」に読み替える。

### 次にやること

- 残りのSDK検証タスク(Main Thread Dispatcher、Portable Mode保存先など)に順次着手
- Component Scannerで取得した情報(DLL名・表示名・バージョン)を、Repository Mapping(ユーザー登録)と紐付けるためのデータ構造を検討

---

## 2026-08-06(続き2) SDK検証タスク#3: Main Thread Dispatcherの確認

`main_thread_callback.h`のコメントに「`fb2k::inMainThread`は`threadsLite.h`に移動した」とあり、モダンなSDKでは`main_thread_callback`を自前実装する必要はなく、以下のヘルパーで完結することが分かった。

- `fb2k::inCpuWorkerThread(std::function<void()>)`: ワーカースレッドへ処理を投げる
- `fb2k::inMainThread(std::function<void()>)`: メインスレッドへ処理を戻す(FIFO順、非同期)

検証用に「Check Thread Dispatch (Debug)」コマンドを追加。`inCpuWorkerThread`で1.5秒のダミー待機(Sleep)をした後、`inMainThread`でコンソールに結果を出す実装で動作確認した。

### 動作確認結果

- クリック直後に`execute() returned immediately`がコンソールに出る(メインスレッドがブロックされていないことを確認)
- 約1.5秒後に`back on main thread. Dispatch OK.`が出る(ワーカースレッド→メインスレッドの復帰を確認)
- **この待機中も音楽再生は止まらない**ことを実機で確認

これでGitHub API通信を実装する際の非同期化の型が固まった。ワーカースレッドでHTTP通信・JSON解析・バージョン比較を行い、結果だけを`fb2k::inMainThread`でUIへ渡す、という構成で進める。

### 次にやること

- 残りのSDK検証タスク(Portable Mode保存先、HelpメニューへのMain Menu Command登録方法の正式確認、SDK/helpersのHTTP機能有無)に順次着手
- Component Scannerで取得した情報を、Repository Mapping(ユーザー登録)と紐付けるためのデータ構造を検討

---

## 2026-08-06(続き3) SDK検証タスク#4: HTTP通信(http_client)の確認

WinHTTPを直接使う必要はなく、SDK標準の`http_client` / `http_request` / `file`(`read_string_raw`)だけでHTTP通信が完結することを確認した。

- `http_client::get()` → `create_request("GET")` → `request->run(url, abort)`で`file::ptr`が返る
- `file::read_string_raw(pfc::string_base&, abort_callback&)`でレスポンス本文をそのまま文字列として読み出せる(ストリーム終端まで読む仕様)
- `abort_callback`には`fb2k::mainAborter()`を使用(アプリ終了時に連動して中断される)

検証用に「Check HTTP Client (Debug)」コマンドを追加。ワーカースレッド上で`https://api.github.com/zen`にGETリクエストを送り、結果をメインスレッドでコンソール出力する実装で動作確認した。

### 動作確認結果

実機で成功。GitHubのZen APIからのレスポンス("Design for failure.")がコンソールに正しく表示されることを確認した。これでGitHub Releases APIへの本番アクセスに進める土台が揃った。

### 次にやること

- 実際のGitHub Releases API(`https://api.github.com/repos/{owner}/{repo}/releases/latest`)を叩いて、JSONレスポンスからバージョン情報を取り出す検証に進む(JSON解析ライブラリの選定が必要)
- 残りのSDK検証タスク(Portable Mode保存先)を後回しでよいか判断し、実装を優先する

---

## 2026-08-06(続き4) SDK検証タスク#5: GitHub Releases APIからのバージョン取得

JSON解析には`nlohmann/json`(v3.11.3, MIT License, 単一ヘッダー)を採用。`third_party/nlohmann/json.hpp`として配置し、`$(ProjectDir)`が既にインクルードパスに含まれているため`.vcxproj`の追加変更は不要だった。

検証用に「Check GitHub Release (Debug)」コマンドを追加。`https://api.github.com/repos/{owner}/{repo}/releases/latest`にGETし、レスポンスJSONから`tag_name` / `html_url` / `prerelease` / `draft`を取り出してコンソール出力する実装で動作確認した(検証対象は`microsoft/vscode`を仮に使用。foo_albumtrainはまだprivateなため)。

### 動作確認結果

実機で成功。`tag_name: 1.132.0`、`html_url`、`prerelease: false`、`draft: false`が正しく取得できることを確認した。

### ここまでで確認できたSDKの使い方(実装の型が固まった)

- **コンポーネント検出**: `componentversion::enumerate()`(DLL名・表示名・バージョン。GUIDは取れない → DLL名を主識別子とする方針で確定済み)
- **非同期処理**: `fb2k::inCpuWorkerThread()`でワーカースレッドへ、`fb2k::inMainThread()`でメインスレッドへ復帰
- **HTTP通信**: `http_client::get()` → `create_request("GET")` → `run(url, abort)` → `file::read_string_raw()`
- **JSON解析**: `nlohmann::json::parse()`

Phase 1の最小構成(コンポーネント一覧取得→手動でGitHub URL登録→手動確認→結果表示)に必要な要素技術は出揃った。

### 次にやること

- ここまでの検証用コード(Help メニューのDebugコマンド群)を土台に、実際のPhase 1機能への組み替えを開始する
- 最初の一歩として、ユーザーがコンポーネントとGitHub RepositoryのURLを紐付けて保存する部分(Repository Mapping + JSON保存)を実装する

---

## 2026-08-06(続き5) Phase 1手動確認フローの統合

`update_check.cpp`を新設し、これまでバラバラに検証していた要素(コンポーネント検出・非同期処理・HTTP通信・JSON解析)を1つの手動確認フローに統合した。Helpメニューに「Check for Component Updates」として追加。

流れ:
1. `componentversion::enumerate()`でコンポーネント一覧取得(メインスレッド)
2. Repository Mapping(**まだUI/JSON保存が無いので、現時点ではソースコード内にハードコードしたテストデータ**)と突き合わせ、マッチしたものだけ処理
3. マッチした分だけワーカースレッドでGitHub Releases API(`/releases/latest`)に問い合わせ
4. バージョン比較(数字とドットのみの文字列同士を比較。それ以外は「比較不能」として誤判定を避ける。DP-0009/DP-0010に対応)
5. 結果をまとめて1回のログ出力で表示(DP-0025「1回の確認で1つの結果セット」に対応)

### つまずいた点: DLL名とリポジトリ名は別物

Repository Mappingのテストデータで`{ "foo_albumtrain.dll", ... }`のように登録したが、`componentversion::get_file_name()`は**拡張子(.dll)を含まない**形式で返すことが判明(実機ログで確認)。`stripDllExtension` / `normalizeDllName`ヘルパーを追加し、拡張子の有無どちらで書いても一致するように吸収した。

さらに、GitHub側の設定でも問題があった。`foo_albumtrain`(DLL名)に対して、リポジトリ名を同じ`foo_albumtrain`だと決め打ちしていたが、実際のfoo_albumtrainのリポジトリ名は`Album-Train`だった(READMEフォルダ名は`Album Train`、DLL名は`foo_albumtrain`、リポジトリ名は`Album-Train`と、3つとも表記が違う)。**DLL名とリポジトリ名が一致するとは限らない**ことを実例で確認できた。これはDesign Principles上も想定済みだったが(だからこそRepository Mappingという仕組みを分けている)、実例に触れたのは今回が初めて。

また、`/releases/latest`はリポジトリが存在してもprivateの場合や、GitHubの「Releases」機能で正式にリリースを1つも公開していない場合にも同じ404(`Object not found`)を返すため、ログだけでは原因の切り分けが難しいと分かった。原因調査時はリポジトリのトップページ・`/releases`ページを直接ブラウザで開いて確認するのが確実。

### 動作確認結果

Repository Mappingを`{ "foo_albumtrain", "p2ashiura", "Album-Train" }`に修正した上で実行し、成功した。

```
Album Train [foo_albumtrain]
  Installed: 4.2.0
  Latest:    v4.2.0  -> Up to date
```

Component Update Checker自身(まだGitHub Release未作成)は想定通りエラーとなり、エラーハンドリング側も併せて確認できた。

これでPhase 1のコア機能(検出→紐付け→取得→比較→表示)が実データで一通り動くことを確認できた。

### 次にやること

- Repository Mapping(ユーザー登録+JSON保存)を、ソースコード内ハードコードから正式な仕組みに置き換える
  - JSONファイルへの保存・読み込み(nlohmann/jsonが既に使えるので流用可能)
  - Portable Mode時の保存先の確認(未着手のSDK検証タスク)
  - ユーザーがRepository URLを入力するUI(Phase 1では簡易ダイアログで十分)
- バージョン比較のprerelease/suffix対応(現状は数字+ドットのみ対応。`v4.2.0-beta`のような表記は「比較不能」になる)

---

## 2026-08-06(続き6) Repository Mappingの永続化(cfg_string)

`repository_mapping.h` / `repository_mapping.cpp`を新設。SDK検証タスク(Portable Mode対応)を兼ねて、`core_api.h`(`get_profile_path`等)と`cfg_var.h`(`cfg_string`)を確認した結果、**自前でファイルI/Oをせず`cfg_string`にJSON文字列を丸ごと保存する方式**を採用した。

理由:
- `cfg_var`系はfoobar2000本体が書き込みのアトミック性・文字コードを面倒みてくれる
- Portable Mode / 通常Modeの保存先の違いを自動的に吸収してくれる
- 自前で`get_profile_path()` + `ofstream`を書くより、Windows特有のパス処理(日本語ユーザー名等)のバグを避けられる

`update_check.cpp`のハードコードされていた`k_repositoryMappingTestData`を撤去し、`loadRepositoryMapping()` / `findRepositoryMapping()`を呼ぶ形に置き換えた。また、まだ登録用UIが無いため、動作確認用に「Repository Mapping: Add Test Entry (Debug)」「Repository Mapping: Show Current (Debug)」の2つをHelpメニューに追加した。

### つまずいた点: BOMの二重付与

複数ファイルをまたいで編集する際、既にBOM付きだったファイルに対してもう一度BOM付与処理をしてしまい、BOMが二重になって1行目の`#include`が壊れる大量のコンパイルエラーが発生した(Claude側の作業ミス)。全ファイルのBOMを一度剥がしてから1回だけ付け直す形で解消。今後は「既にBOM付きかどうか」を確認してから処理するよう運用を改める。

### 動作確認結果

実機で成功。以下をすべて確認した。

- 「Add Test Entry」でcfg_stringへの保存
- 「Show Current」で保存内容の読み込み・表示
- 「Check for Component Updates」が、ハードコードを外した後もcfg_string経由で正しく`Up to date`を表示
- **foobar2000を再起動した後も、「Show Current」で内容が残っていることを確認**(永続化が実際に機能している証拠)

これでPhase 1の主要な要素(検出・登録の永続化・GitHub確認・比較・表示)が「再起動しても内容が残る」形で一通り揃った。

### 次にやること

- 登録用のUI(簡易ダイアログでDLL名・owner・repoを入力できるようにする)
- バージョン比較のprerelease/suffix対応(現状は数字+ドットのみ対応)
- Debug用コマンド群の扱いを整理する(最終的にはHelpメニューから外し、開発時のみ有効にするなどの方針を検討)

---

## 2026-08-06(続き7) Repository Mapping登録用ダイアログ

`repository_mapping_ui.cpp`を新設。foo_albumtrainの設定ダイアログと同じ方式(`.rc`ファイルを使わず、`DialogBoxIndirectParam` + 実行時の`CreateWindowEx`でコントロールを組み立てる)を踏襲した。Helpメニューに「Manage Component Repositories...」として追加。

構成:
- Component: ドロップダウン(`componentversion::enumerate()`から自動生成。DLL名の手打ちミスを防ぐ)
- GitHub Owner / GitHub Repo: Edit
- Save: `upsertRepositoryMappingEntry()`で即座に保存
- 登録済み一覧: ListBox
- Remove Selected: 選択中のエントリを削除(`removeRepositoryMappingEntry()`を新設)
- Close: 閉じるのみ(OK/Cancelの区別は持たせず、Saveした時点で確定する単純な方式)

### つまずいた点: ウィンドウサイズの計算

クライアント領域のピクセルサイズをそのまま`SetWindowPos`に渡していたため、タイトルバー・枠線の分だけ実際の表示領域が狭くなり、右端(Closeボタン)や一部ラベルが欠けて表示された。foo_albumtrainの実装を確認したところ、`AdjustWindowRectEx`で外枠込みのウィンドウサイズへ変換してから`SetWindowPos`する、という手順を踏んでいたため、同じ方式に修正して解消(あわせて親ウィンドウのモニター中央に表示するようにした)。ラベル幅も少し狭かったため拡張した。

### 動作確認結果

実機で成功。ドロップダウンからの選択・Owner/Repo入力・Save・一覧表示・Removeまで一通り動作を確認した。

これでPhase 1の主要フロー(検出・登録UI・永続化・GitHub確認・比較・表示)がすべて揃った。

### 次にやること

- バージョン比較のprerelease/suffix対応(現状は数字+ドットのみ対応)
- Debug用コマンド群(Check Component Updates (Debug)、Check Thread Dispatch (Debug)、Check HTTP Client (Debug)、Check GitHub Release (Debug)、Repository Mapping: Add Test Entry / Show Current)の整理。役目を終えたものから順にHelpメニューから外していく
- 将来的にPreferences → Toolsへの設定画面統合(あしぅら案)を見据えた設計の見直し

---

## 2026-08-06(続き8) Debugコマンド群の整理

SDK検証のために積み上げてきた探索的なコマンド群を整理した。実機能(`Check for Component Updates`と`Manage Component Repositories...`)に置き換わっているものはすべて削除した。

- `component_scanner.cpp`: 中身が全部Debugコマンド(Check Component Updates (Debug)、Check Thread Dispatch (Debug)、Check HTTP Client (Debug)、Check GitHub Release (Debug))だったため、ファイルごと削除。`.vcxproj`からも参照を除去
- `repository_mapping.cpp`: 末尾のDebugコマンド部分(Add Test Entry / Show Current)を削除。load/save/find/upsert/removeのコアロジックはそのまま維持

これでHelpメニューは「Check for Component Updates」「Manage Component Repositories...」の2つに整理された。

### 次にやること

- バージョン比較のprerelease/suffix対応(現状は数字+ドットのみ対応)
- 自動確認(起動後の遅延実行、Phase 1のスコープに入っているがまだ未着手)
- 将来的にPreferences → Toolsへの設定画面統合(あしぅら案)を見据えた設計の見直し

---

## 2026-08-06(続き9) 自動確認機能の実装

Phase 1の最後の主要機能として自動確認を実装した。これで最初に構想した機能がひととおり形になった。

### リファクタリング: update_check.h/cppの共通化

自動確認は手動確認(`Check for Component Updates`)と全く同じ「検出→紐付け→GitHub確認→比較」ロジックを使うため、先に`update_check.cpp`から`update_check.h`を切り出し、以下を公開した。

- `GetInstalledComponents()`: コンポーネント一覧取得(メインスレッド専用)
- `RunUpdateCheck()`: Repository Mapping突き合わせ+GitHub問い合わせ+比較(ワーカースレッド専用)
- `HasUpdateAvailable()`: 結果セットに更新ありが含まれるか
- `PrintUpdateCheckResults()`: 結果セットのconsole出力

### 自動確認の実装(automatic_check.cpp)

`initquit`(`SDK/initquit.h`)と`FB2K_RUN_ON_INIT`相当の`on_init()`で、foobar2000起動完了のタイミングをフック。設計:

- 起動完了後、**20秒待ってから**確認を開始(Playback First: 起動直後の負荷集中を避ける)。Sleepはワーカースレッドで行うためメインスレッドは妨げない
- 前回確認からの経過日数が設定値(既定7日)未満ならスキップ。cfg_int(`g_cfgAutoCheckLastRun`)にUNIXエポック秒で記録
- 確認を試みた時点で成功・失敗問わず最終確認日時を更新(通信不調時に毎起動でリトライが集中するのを防ぐ)
- **Non-Intrusive**: 更新が1件も無ければconsoleにも何も出さない。見つかった場合のみ要約を1行出す(Popup Notification等の専用UIはPhase 2以降)

### つまずいた点: cfg_var_legacyには`.set()`が無い

`cfg_int`(`cfg_var_legacy::cfg_int_t`)には`.set()`メソッドが存在せず、代入演算子(`operator=`)のみで値を設定する仕様だった(`cfg_var_modern`側は両方使えるが、ビルドされたSDKでは`FOOBAR2000_TARGET_VERSION < 81`によりlegacy版が使われていた)。`g_cfgAutoCheckLastRun.set(...)`を`g_cfgAutoCheckLastRun = ...`に修正して解消。あわせて、legacy版の`cfg_int`が32bit整数(`t_int32`)であることも判明し、UNIXエポック秒を保存する上で2038年問題の制約があることをコメントで明記した(Phase 1の時間軸では実害無し)。

### 動作確認用: Force Automatic Check (Debug)

7日間隔だと通常の動作確認が難しいため、間隔判定を無視して即座に自動確認と同じ処理を走らせる「Force Automatic Check (Debug)」をHelpメニューに追加した(トラブルシューティング用途として今後も残す)。

「更新あり」の経路を確認するため、一時的にComponent Update Checker自身(インストール済み0.1.0)を`microsoft/vscode`(バージョンが大きい実在のpublicリポジトリ)に紐付けてテストした。

### 動作確認結果

実機で成功。

```
Automatic Check Debug: forcing a check now (interval ignored)...
Component Update Checker: updates available for Component Update Checker. Use "Check for Component Updates" (Help menu) for details.
```

「更新あり」を検知して静かな通知が出ることを確認した。テスト用の`microsoft/vscode`への紐付けは確認後に削除する。

これでPhase 1として最初に構想した主要機能(検出・登録UI・永続化・手動確認・自動確認・バージョン比較・結果表示)が一通り実装され、動作確認も取れた。

### 次にやること

- バージョン比較のprerelease/suffix対応(現状は数字+ドットのみ対応)
- 自動確認の設定(有効/無効、間隔日数)を変更できるUI(現状はコード上の既定値固定)
- 将来的にPreferences → Toolsへの設定画面統合(あしぅら案)を見据えた設計の見直し
- Debugコマンド(Force Automatic Check)の扱いを、正式なUIができた際にどうするか検討

---

## 2026-08-06(続き10) 自動確認の設定ダイアログ

`automatic_check.h`を新設し、`automatic_check.cpp`内のcfg変数(有効/無効・間隔日数)を`GetAutomaticCheckEnabled()` / `SetAutomaticCheckEnabled()` / `GetAutomaticCheckIntervalDays()` / `SetAutomaticCheckIntervalDays()`として公開した(cfg変数自体は無名namespaceの外・`static`付きのファイルスコープに配置)。

`automatic_check_settings_ui.cpp`を新設。`repository_mapping_ui.cpp`と同じ方式(`.rc`不要、`DialogBoxIndirectParam` + 実行時`CreateWindowEx`、`AdjustWindowRectEx`でウィンドウサイズ確定、親ウィンドウのモニター中央に表示)でダイアログを実装した。Helpメニューに「Automatic Check Settings...」として追加。

構成:
- チェックボックス: Automatically check for updates
- Edit(数値のみ、`ES_NUMBER`): Check interval (days)。1未満は`SetAutomaticCheckIntervalDays()`側でも下限バリデーション
- Save: 即座に保存、「Saved.」メッセージ表示後、保存後の値を読み直して画面に反映
- Close: 閉じるのみ

### 動作確認結果

実機で成功。有効/無効の切り替え、間隔日数の変更・保存、ダイアログの再オープンでの値保持、**foobar2000再起動後も値が保持されている**ことを確認した。cfg_string(Repository Mapping)だけでなく、cfg_bool/cfg_intでも同様に永続化が機能することが実証された。

これで自動確認は「有効/無効」「間隔」をユーザー自身が変更できる状態になり、Phase 1の主要機能はすべて設定変更・永続化まで含めて完成した。

### 次にやること

- バージョン比較のprerelease/suffix対応(現状は数字+ドットのみ対応)
- 将来的にPreferences → Toolsへの設定画面統合(あしぅら案)。現状Helpメニューに散らばっている「Check for Component Updates」「Manage Component Repositories...」「Automatic Check Settings...」「Force Automatic Check (Debug)」を1つの設定ページにまとめる方向で検討
- Debugコマンド(Force Automatic Check)の扱いを、Preferences統合時にどうするか検討

---

## 2026-08-06(続き11) Preferences → Toolsへの統合

あしぅらさん最初の構想通り、Preferences → Tools配下に「Component Update Checker」ページを新設し、Helpメニューに散らばっていた設定系コマンドを集約した。

### 構成の変更

- `repository_mapping_ui.cpp`: `ShowRepositoryMappingDialog()`を無名namespaceの外に出し`repository_mapping_ui.h`で公開。単独のHelpメニューコマンド(「Manage Component Repositories...」)は撤去
- `automatic_check_settings_ui.cpp`: 撤去(Preferencesページに直接埋め込んだため不要に)
- `preferences_page.cpp`(新設): `preferences_page_v4`を実装。チェックボックス(自動確認の有効/無効)、間隔入力、「Manage Repositories...」ボタン、「Check for Updates Now」ボタンを1ページに集約
- Helpメニューに残るのは「Check for Component Updates」と「Force Automatic Check (Debug)」の2つ(クイックアクセス用として維持)

### つまずいた点: 独自WNDCLASSでの実装は画面が真っ白になった

`preferences_page_instance::instantiate()`は、これまでのポップアップダイアログ(`DialogBoxIndirectParam`、`WS_POPUP`)とは異なり、親ウィンドウに埋め込まれる子ウィンドウを返す必要がある。最初、独自の`WNDCLASS`を`RegisterClass`で登録し`CreateWindowEx`で子ウィンドウを作る方式で実装したが、Preferencesページを開いてもコントロールが一切表示されない(枠線すら見えない)問題が発生した。

原因の完全な特定はしていないが、これまで2回(Repository Mappingダイアログ、旧Automatic Check Settingsダイアログ)実績のある「`WS_CHILD`スタイルのダイアログテンプレート + `CreateDialogIndirectParam`」方式に置き換えたところ解消した。独自WNDCLASSの登録・生ウィンドウ作成は避け、ダイアログテンプレート方式に統一する方が安全という教訓が得られた。

技術的なポイント:
- `DS_CONTROL`スタイル(他のダイアログに埋め込まれる部品であることを示す)を使用
- `WM_INITDIALOG`の`lParam`に`instance_impl*`(`this`)を渡し、`SetWindowLongPtr(hDlg, DWLP_USER, ...)`で保持。以降`WM_COMMAND`等で`GetWindowLongPtr(hDlg, DWLP_USER)`から取り出す
- `get_state()`は、コントロールの現在値とbaseline(最後にApply/読み込みした値)を比較して`preferences_state::changed`を返す。これによりPreferencesウィンドウのApplyボタンの有効/無効が自動的に連動する
- `apply()`で実際に保存し、baselineを更新

### 動作確認結果

実機で成功。コントロール表示、チェックボックス/間隔変更によるApplyボタンの連動、Apply後の保存、Preferences再オープン時の値保持、「Manage Repositories...」「Check for Updates Now」ボタンの動作、すべて確認した。

これでPhase 1の集大成として、設定がPreferences → Toolsの1ページに集約された状態が完成した。

### 次にやること

- バージョン比較のprerelease/suffix対応(現状は数字+ドットのみ対応)
- Force Automatic Check (Debug)をPreferencesページのボタンとしても持たせるか検討(現状Helpメニューのみ)
- リリースに向けた最終確認(README更新、バージョン表記の整理等)
