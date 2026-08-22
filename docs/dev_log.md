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

---

## 2026-08-06(続き12) バージョン比較のprerelease対応

`compareVersions()`をSemVer 2.0.0の優先順位ルールに準拠する形に拡張した。

対応形式: `MAJOR.MINOR.PATCH[.MORE...][-prerelease][+build]`(先頭の`v`/`V`は許容)。

- ビルドメタデータ(`+`以降)は優先順位に影響しないため無視
- prerelease同士は識別子ごとに比較。数字のみの識別子は数値比較、数字のみは英数字混在より必ず小さい扱い、それ以外はASCII順。すべて一致すれば識別子数が少ない方が「古い」
- 同じコアバージョンで、正式リリース(prereleaseなし)は同コアのどのprereleaseよりも常に優先順位が高い(`1.0.0-beta` < `1.0.0`)
- この形式に当てはまらないものは、引き続き「比較不能」として誤判定を避ける(DP-0009踏襲)

### 動作確認: 実際のGitHub APIレスポンスを使った検証

GitHubの`/releases/latest`はprerelease扱いのリリースを除外して返す仕様のため、実在のリポジトリから直接prereleaseタグを取得してテストすることはできない。そこで、**インストール済み側(Component Update Checker自身)のバージョン表記を一時的にprerelease風にし、実際の最新版のコアバージョンに合わせる**方法で検証した。

具体的には、`microsoft/vscode`の実際の最新版(`1.132.0`)を事前に確認した上で、`dllmain.cpp`の`DECLARE_COMPONENT_VERSION`を一時的に`"1.132.0-beta.1"`に変更してビルド・実行し、検証後に`"0.1.0"`へ戻した。

結果:

```
Component Update Checker [foo_component_update_checker]
  Installed: 1.132.0-beta.1
  Latest:    1.132.0  -> Update available
  Release:   https://github.com/microsoft/vscode/releases/tag/1.132.0
```

同一コアバージョンでprereleaseと正式版を正しく判定できることを、実データで確認できた。

### 次にやること

- Force Automatic Check (Debug)をPreferencesページのボタンとしても持たせるか検討(現状Helpメニューのみ)
- リリースに向けた最終確認(README更新、バージョン表記の整理等)

---

## 2026-08-07 Remote Registry(既知コンポーネントDB)の実装

Phase 2の主要機能として、開発側が管理する「DLL名 <-> GitHub Repository」対応表(Remote Registry)を実装した。ユーザーが個別に登録しなくても、主要なコンポーネントは自動的に更新確認できるようにする仕組み(Project Charter Phase 2、Design Principles DP-0018 Hybrid Registry Modelで構想していたもの)。

### 設計

- **サーバーは使わない**。GitHub上の静的JSON1つ(`known_components.json`)を`raw.githubusercontent.com`経由で匿名取得するだけ。GitHub APIのレート制限を消費しない
- **優先順位**: ユーザーが**Manage Repositories...**で明示的に登録した内容が常に最優先。そこに無いコンポーネントのみRemote Registryを参照する(DP-0018)
- **キャッシュ**: 取得した生JSONをcfg_stringにそのままキャッシュ。24時間に1回のみ再取得を試みる(Bounded Work)。成功・失敗にかかわらず試行時刻は記録し、通信不調時のリトライ集中を防ぐ(automatic_check.cppと同じ方針)
- **スキーマ**: `schema_version`と、各エントリに`source`フィールド(現状`"github"`のみ対応)を持たせ、将来GitLabや個人サイト等への対応を見据えた拡張性を確保(DP-0030 Schema Evolution)
- Fail Open: 取得失敗・JSON破損時は、直近のキャッシュ(なければ空)にフォールバックし、確認処理自体は止めない

新設ファイル: `remote_registry.h` / `remote_registry.cpp`。`update_check.cpp`の`RunUpdateCheck()`に、ユーザー登録 → Remote Registryの順で参照する優先順位ロジックを組み込んだ。

### リポジトリ構成の変更: レジストリ専用リポジトリへの分離

当初`foo_component_update_checker`リポジトリ内に`registry/known_components.json`を置く想定だったが、`raw.githubusercontent.com`は匿名アクセスのため**リポジトリがpublicである必要がある**ことが判明(動作確認時にnot foundで発覚)。コード本体をpublicにする心の準備がまだだったため、急遽方針変更し、**`foo_component_update_checker-registry`という別のpublicリポジトリ**を新設し、そちらに`known_components.json`を切り出した。コード本体(`foo_component_update_checker`)は引き続きprivateのまま運用できる。

これは実は最初から検討していた「将来的に別リポジトリへ移行する」選択肢そのもので、参照先URLを`remote_registry.cpp`内の定数1箇所にまとめておいたことで、実際に移行コストは定数の書き換えだけで済んだ。

### 動作確認用: Force Refresh Remote Registry (Debug)

24時間の再取得間隔があると動作確認がしづらいため、間隔を無視して即座に再取得・一覧表示するDebugコマンドをHelpメニューに追加した(トラブルシューティング用途として今後も残す)。

### 動作確認結果

実機で成功。

```
Remote Registry Debug: forcing refresh now (interval ignored)...
Remote Registry Debug: 1 entrie(s) loaded:
  foo_albumtrain [github] -> p2ashiura/Album-Train
```

続けて、Manage Repositoriesにfoo_albumtrainの個別登録が無い状態で「Check for Updates Now」を実行し、Remote Registry経由での紐付けだけで正しく動作することを確認した。

```
Album Train [foo_albumtrain]
  Installed: 4.2.0
  Latest:    v4.2.0  -> Up to date
```

これで「ユーザーが何も登録しなくても、主要コンポーネントは自動的に更新確認される」というPhase 2の中核機能が実現した。

### 次にやること

- Remote Registryへのエントリ追加(現状`foo_albumtrain`のみ。他の主要コンポーネントも今後追加していく)
- Manage Repositories...ダイアログで、Remote Registry由来の情報(どのコンポーネントが既にカバーされているか)を表示するUX改善の検討
- Force Automatic Check (Debug) / Force Refresh Remote Registry (Debug)をPreferencesページのボタンとしても持たせるか検討(現状Helpメニューのみ)

---

## 2026-08-07(続き) Shared Registryへの投稿ボタン(PR誘導方式)

ユーザーがRemote Registry(`foo_component_update_checker-registry`)へ新しいコンポーネント情報を提案できるボタンを、**Manage Repositories...**ダイアログに追加した。

### 設計判断: OAuth/APIトークンは使わない

GitHub APIで直接PRを作成する方式も検討したが、認証情報(トークン)をコンポーネント側で扱う必要が生じ、Design PrinciplesのPrivacy by Minimization/Secure Navigationの観点でリスクが高い。代わりに、**GitHubの「ファイル編集→自動フォーク→PR作成」という標準のWeb UIフロー**に乗せる方式にした。

流れ:
1. ダイアログで選択中のコンポーネント+入力済みowner/repoから、`known_components.json`の`"components"`配列にそのまま貼り付けられるJSONスニペットを組み立てる
2. クリップボードにコピー(`CF_UNICODETEXT`、UTF-8→UTF-16変換して格納)
3. `https://github.com/<owner>/<repo>/edit/main/known_components.json`を`ShellExecuteA`で開く。GitHub側がログイン状態を見て、書き込み権限が無ければ自動でフォーク→編集モードにしてくれる
4. ユーザーがペーストして「Propose changes」を押せばPRが作成される(この部分はGitHubの標準機能に任せている)

### リファクタリング: registryリポジトリの場所を一元管理

`remote_registry.h`に`k_registryRepoOwner` / `k_registryRepoName` / `k_registryFilePath`を定数として公開し、`remote_registry.cpp`(取得元URL)と`repository_mapping_ui.cpp`(PR誘導ボタンの編集URL)の両方がここを参照するように統一した。将来レジストリの場所を変える際は、この3定数を書き換えるだけでよい。

### 動作確認結果

実機で成功。ボタンクリックでJSONスニペットがクリップボードにコピーされ、GitHubの編集画面が開き、正しくペーストできることを確認した。

```
    {
      "dll": "foo_component_update_checker",
      "source": "github",
      "owner": "microsoft",
      "repo": "vscode"
    },
```

これで、ユーザーがRepository Mapping登録のついでに、開発側のDBへも簡単に貢献できる導線ができた。

### 次にやること

- Remote Registryへのエントリ追加(現状`foo_albumtrain`のみ)
- 提案されたPRのレビュー運用(誰が承認するか、悪意ある投稿をどう弾くか)は今後の課題。現状は誰でもPRを送れる状態で、マージするかどうかは手動判断に委ねられる
- Manage Repositories...ダイアログで、Remote Registry由来の情報を表示するUX改善の検討

---

## 2026-08-07(続き2) 結果表示をコンソールからポップアップウィンドウに変更

これまでconsole::printで出していた更新確認結果を、ListView形式のポップアップウィンドウ(`result_window.h` / `result_window.cpp`)に置き換えた。

### UI設計

- ListView(report view)でComponent / Installed / Latest / Statusの4列を表示
- 行をダブルクリック、または「Open Release Page」ボタンでリリースページをブラウザで開く
- 該当コンポーネントが0件の場合は、ウィンドウの代わりに簡易MessageBoxを表示

適用箇所:
- 「Check for Component Updates」(Helpメニュー)
- Preferencesの「Check for Updates Now」ボタン
- 自動確認(更新が見つかった場合のみ。DP-0002 Non-Intrusiveは維持し、見つからなければ何も表示しない)
- Force Automatic Check (Debug)も同じウィンドウに統一(更新なしの場合のみコンソールにDebug専用メッセージを出す)

### つまずいた点: ListViewのA/Wマクロ

プロジェクトの文字コード設定がUnicodeのため、`ListView_InsertItem` / `ListView_SetItemText` / `ListView_InsertColumn`などの汎用マクロがワイド文字版(`LVITEMW`等)を要求し、UTF-8/ANSI文字列(`std::string`/`char*`)を渡すとコンパイルエラーになった。`ListView_InsertItemA`のような明示的なA版マクロも、このSDKのヘッダーには定義が無く未解決エラーになった。最終的に、マクロに頼らず`SendMessageA` + `LVM_INSERTITEMA` / `LVM_SETITEMTEXTA` / `LVM_INSERTCOLUMNA`メッセージ定数を直接使う形に書き換えて解消した。

### 設計判断: 「Up to date」ではリリースページを開けないようにする

当初は取得できたリリースURLがあれば常に開けるようにしていたが、「Up to dateなのにボタンが押せると、まだ何かすべきという誤解を招く」との指摘を受け、方針変更。**「押せる = 見る価値がある」という一貫したルール**にし、Up to date / Errorのときはクリックしても「This component is already up to date.」等のメッセージのみ表示し、リリースページは開かないようにした(Update available / Unable to compareのみ開ける)。

### 動作確認結果

実機で成功。Update availableの行はリリースページが正しく開き、Up to dateの行では意図通り開かず案内メッセージが出ることを確認した。

### 次にやること

- Remote Registryへのエントリ追加(現状`foo_albumtrain`のみ)
- 該当コンポーネントが0件のケース(MessageBox表示)の動作確認(まだ未検証)
- 提案されたPRのレビュー運用の検討
- Manage Repositories...ダイアログで、Remote Registry由来の情報を表示するUX改善の検討

---

## 2026-08-07(続き3) Repository Mappingの入力方法をURL貼り付け方式に変更

**Manage Repositories...**の入力欄を、「GitHub Owner」「GitHub Repo」の2欄から**「Repository URL」1欄**に統合した。owner/repoを手動で分けて入力させるより直感的で、かつ将来GitHub以外のサイトに対応を広げる際も「ユーザーが持っている情報(URL)をそのまま渡せる」形になるため。

`TryParseGitHubUrl()`を追加し、以下のような表記ゆれを吸収してowner/repoを取り出す。

- `https://github.com/owner/repo`
- `github.com/owner/repo`(スキーム無し)
- `https://github.com/owner/repo.git`
- 末尾に`/releases`等が付いていても可(クエリ・フラグメントも除去)

`github.com`と解釈できない入力は誤登録を避けるため保存せず、明示的にエラーを出す(DP-0009踏襲)。保存されるデータ形式(owner/repo文字列)自体は変更していないため、cfg_stringの保存内容やRemote Registryとの互換性への影響は無い。「Suggest for Shared Registry...」ボタンも同じ解析ロジックを使うよう統一した。

### 動作確認結果

実機で成功。URL貼り付け→保存→一覧に正しくowner/repoが分解されて表示されること、無関係なURLでは正しくエラーになることを確認した。

### 次にやること

- 設定項目の拡充、UI整理(あしぅらさんの構想: 公開前の最終チェックリストとして「設定項目追加」「UI整理」「Repository Mapping改善(今回実施)」「DB拡充」「自動確認ポップアップの動作確認」の5項目が挙がっている)
- Remote Registryへのエントリ追加(現状`foo_albumtrain`のみ)
- 自動確認で実際にポップアップが出るかの実地確認(7日待つか、Force Automatic Check (Debug)で代替確認は済んでいるが、本物の自動確認フローでの確認はまだ)

---

## 2026-08-07(続き4) 自動確認の通知レベル設定 + エラー原因の分類

公開前チェックリストの1つ目「設定項目の追加」として、自動確認の通知レベルを設定可能にした。

### これまで見えなかった情報の洗い出し

自動確認は「更新あり」以外は完全に無音だったため、以下が見えなくなっていた:
- ネットワークエラー(接続不可、タイムアウト等)
- GitHub API側のエラー(404/403等)
- JSON解析エラー
- 「比較不能」の結果
- 該当コンポーネントが0件のケース
- Remote Registry(共有DB)自体の取得失敗

### 実装

`automatic_check.h`に`AutoCheckNotificationLevel`(UpdatesOnly / UpdatesAndErrors / Always)を追加し、cfg_int(既定値0=UpdatesOnly)で永続化。Preferencesページにラジオボタン3択(「When automatic check runs, show a popup for:」)を追加し、既存のcheckbox/interval設定と同じbaseline管理(Apply連動)に組み込んだ。

`runAutomaticCheck()`の判定ロジック:
```
shouldShow = (level == Always)
          || HasUpdateAvailable(results)
          || (level == UpdatesAndErrors && HasAnyError(results))
```

`Force Automatic Check (Debug)`も同じ判定ロジックを再現するよう統一した。

### エラーの原因分類

`update_check.cpp`の`RunUpdateCheck()`で、これまで1つのtry/catchで囲んでいたHTTP取得とJSON解析を分離し、原因別のメッセージを付与:
- `Network/API error: ...`(接続失敗、GitHub側のエラー含む)
- `Invalid response: ...`(レスポンスの解析失敗)

### Remote Registry取得失敗の可視化

`remote_registry.h`に`RemoteRegistryFetchStatus`(attempted/succeeded/errorMessage)を追加し、`GetRemoteRegistryEntries()`が取得を試みたか・成功したかを呼び出し元に伝えられるようにした。`RunUpdateCheck()`は、Remote Registry取得に失敗した場合、`"Shared Registry (known_components.json)"`という合成エントリを結果セットに追加する。これにより、通知レベル判定にも自然に載るほか、**手動確認でもこれまで見えなかったRemote Registryの取得失敗が見えるようになった**(副産物的な改善)。

### 動作確認結果

実機で成功。ラジオボタンの切り替え・Apply連動・保存・再読み込みまで確認。存在しないowner/repoをRepository Mappingに登録してエラーケースを意図的に再現し、結果ウィンドウに`Error: Network/API error: Object not found`のように、原因が分かる形で表示されることを確認した。

### 次にやること

- 公開前チェックリスト残り: UI整理、DB拡充、自動確認ポップアップの実地確認(7日待つか、Debugコマンドでの代替確認は済んでいる)

---

## 2026-08-07(続き5) UI整理: メニュー名変更・Debugコマンド削除・ダークモード対応

公開前チェックリストの2つ目「UI整理」に着手した。

### Helpメニュー項目名の変更

foobar2000標準機能に「Check for updated components」という項目が既にあり、こちらの「Check for Component Updates」と紛らわしいと判明。**「Check Third-Party Component Updates」**に変更し、説明文にも「標準のComponent Repositoryではカバーされないサードパーティー製コンポーネント向け」であることを明記した。

### Debugコマンドの削除

公開前提として、「Force Automatic Check (Debug)」(automatic_check.cpp)と「Force Refresh Remote Registry (Debug)」(remote_registry.cpp)を削除した。動作確認は十分済んでいるため実害なし。Helpメニューは「Check Third-Party Component Updates」の1項目のみになった。

### ダークモード対応

`SDK-2025-03-07/foobar2000/SDK/coreDarkMode.h`の`fb2k::CCoreDarkModeHooks`(`helpers/DarkMode.h`のlibPPUI直リンク版ではなく、foo_ui_std側の実装を呼び出す軽量版。Minimal Dependencies方針に合致)を採用。

- **Preferencesページ**: `createAuto()`でメンバー変数として保持し、コンストラクタで`AddDialogWithControls(m_wnd)`。`get_state()`に`preferences_state::dark_mode_supported`を追加。foobar2000側の設定変更に自動追従することを確認
- **Manage Repositories...**(`repository_mapping_ui.cpp`): `ShowRepositoryMappingDialog()`内にローカル変数として`CCoreDarkModeHooks`を確保し、`DialogBoxIndirectParam`の`lParam`経由で`WM_INITDIALOG`に渡し、そこで`AddDialogWithControls(hDlg)`
- **結果ウィンドウ**(`result_window.cpp`): 既存の`ResultWindowContext`構造体(`lParam`経由で渡している、ダイアログ表示中ずっとスタック上に生存)にメンバーとして追加

### つまずいた点: ListViewの文字が見えなくなる

結果ウィンドウに`AddDialogWithControls()`を適用したところ、**ダークモード・ライトモード両方で**ListViewの行の文字が見えなくなった。当初「ダークモードでは背景が暗いのに文字色が黒のまま」という仮説で`ListView_SetTextColor`等を明示的に設定したが改善せず、むしろライトモードまで巻き込んで悪化した。

最終的に、**ListView自体にはダークモードフックを適用せず**(`AddDialog(hDlg)` + ボタン2つへの`AddCtrlAuto()`のみに限定)、ListViewの色は`ListView_SetBkColor` / `ListView_SetTextBkColor` / `ListView_SetTextColor`で完全に手動管理する形に変更したところ解消した。ダークモードフック側のListView用カスタム描画処理と、手動の色指定が競合していたと推測される(詳細な原因はlibPPUI内部の実装に依存するため未確認)。教訓として、**ListViewを含むダイアログでは、フックを`AddDialogWithControls()`で一括適用せず、ListViewだけ手動色管理に分離する**方が安全そうだとわかった。

### 動作確認結果

実機で成功。Preferencesページ・Manage Repositories...ダイアログはダークモードに正しく追従。結果ウィンドウもボタン・ダイアログ背景はテーマに追従しつつ、ListViewの文字がライト/ダーク両方のモードで正しく見えることを確認した。

### 次にやること

- 公開前チェックリスト残り: DB拡充、自動確認ポップアップの実地確認(7日待つか、Debugコマンドでの代替確認は済んでいる。ただしDebugコマンドは削除済みなので、実地確認が必要な場合は一時的に復活させる)
- UI整理の続き(他に気になる点があれば都度対応)

---

## 2026-08-07(続き6) UI整理: フォントサイズ・ヘッダー配色・ステータス文言

### フォントサイズの修正

Preferencesページ・Manage Repositories...・結果ウィンドウの3ダイアログで、文字がfoobar2000標準のPreferencesページより大きく表示される問題があった。

**誤った仮説(1回目)**: `DS_SHELLFONT`スタイルを指定しているのにフォント情報(pointsize/typeface)をDLGTEMPLATEに含めていないのが原因、と考え追加したが効果なし(そもそも配列サイズのミスでビルドも一度失敗)。

**真因**: `DS_SETFONT`(`DS_SHELLFONT`に含まれる)は、**テンプレートに直接定義されたコントロール**にはフォントを自動適用するが、これらのダイアログは`cdit=0`で全コントロールを`WM_INITDIALOG`内の`CreateWindowEx`で動的に作っているため、そもそもテンプレート側のフォント自動適用の対象外だった。

**誤った仮説(2回目)**: `WM_GETFONT`でダイアログから取得したフォントを、`EnumChildWindows`で全コントロールに`WM_SETFONT`適用すれば直ると考えたが、結果ウィンドウのListViewの文字が(それまで小さく見えていたのに)逆に大きくなってしまった。ダイアログ自身が`WM_GETFONT`で返すフォント自体が、期待していた小さいシェルフォントではなかったと判明。

**最終解決**: `DS_SHELLFONT`の自動解決に頼るのをやめ、`SystemParametersInfo(SPI_GETNONCLIENTMETRICS)`で`lfMessageFont`(Windowsのダイアログ標準フォント、通常Segoe UI 9pt相当)を自前取得し、`CreateFontIndirect`で生成したフォントを、ダイアログ自身と全子コントロールへ明示的に`WM_SETFONT`で適用する方式に変更。3ファイルとも同じパターンを踏襲。これで解決した。

### 結果ウィンドウ: ListViewヘッダーのダークモード対応

ダークモード時、ListViewの行の色は手動設定(既存)で正しく暗くなっていたが、**列見出し(ヘッダー行)だけ背景が白いまま**残っていた。ヘッダーはListView本体とは別の子ウィンドウ(`SysHeader32`)であるため、行の色設定の対象外だったことが原因。

- `SetWindowTheme(hHeader, L"DarkMode_ItemsView", NULL)`で背景色は解決
- 文字色は追従せず黒いまま残った。ダイアログの`WM_NOTIFY`で`NM_CUSTOMDRAW`を処理しようとしたが効果なし
- 原因: ヘッダーの`NM_CUSTOMDRAW`通知の送信先は**ヘッダーの親であるListView自身**であり、そこから先(ダイアログ)へは伝播しないため、ダイアログ側でいくら`WM_NOTIFY`を処理しても届いていなかった
- 解決: `SetWindowSubclass`で**ListView自体をサブクラス化**し、ListViewが受け取った時点で直接`NM_CUSTOMDRAW`を処理し、`SetTextColor`で文字色を明示指定する方式に変更。これで解決した

### ステータス文言の変更

「Up to date」と「Update available」が見た目(先頭2単語)で紛らわしいとの指摘を受け、「Up to date」を**「Current」**に変更した。列見出しの「Latest」(バージョン番号)との混同を避ける意図もある。

### 動作確認結果

実機で全て成功。3ダイアログのフォントサイズがfoobar2000標準と揃い、結果ウィンドウのヘッダーもダークモードで背景・文字色ともに正しく表示され、ステータス文言も視認性が改善したことを確認した。

### 次にやること

- 公開前チェックリスト残り: DB拡充、自動確認ポップアップの実地確認
- UI整理はこれでひとまず一区切り。他に気になる点があれば都度対応

---

## 2026-08-07(続き7) データベース拡充・PR受付・v1.0.0リリース

### Remote Registryの拡充

実在するGitHub Releasesページを確認しながら5件を追加(`foo_spider_monkey_panel`, `foo_uie_webview`, `foo_vis_spectrum_analyzer`, `foo_midi`、および一度追加した`foo_jscript_panel3`)。`foo_jscript_panel3`(jscript-panel/release)は追加後にリリースページ自体が消失していることが判明し、開発が放棄されたと判断。JSONの`components`配列ではなく別の`disabled`配列に移動した(パーサーは`components`しか読まないため実質的な「コメントアウト」として機能する)。理由も含めて記録し、READMEにも「Disabled / 無効化済み」節を設けた。

registryリポジトリのREADMEに、登録済みコンポーネント一覧の表と、Contributing(PR歓迎、Suggest for Shared Registry...ボタンとの連携)を追記した。

### 自動確認の実地確認(公開前チェックリスト最後の項目)

これまでの「Force Automatic Check (Debug)」は本物の起動フロー(`on_init()` → 20秒待機)を経由していなかったため、一時的に「Reset Automatic Check Last-Run (TEMP Debug)」を追加し、最終確認日時をリセットしてfoobar2000を実際に再起動し、20秒待って自動的にポップアップが出ることを確認した。確認後、このDebugコマンドは削除した。

### クラッシュレポートの調査と防御コードの追加

自動確認の実地確認中、`ucrtbased.dll`(Debug版CRT)内での「Illegal operation」クラッシュが1件発生。`Call path: main_thread_callback::callback_run`とあり、タイミング的にアプリ終了処理中にワーカースレッドからのコールバックがディスパッチされたことが引き金と推測された。Debug CRT特有の自己診断(Release版では発生しない可能性が高い)である一方、念のため防御コードを追加: 手動確認・Preferencesの「Check for Updates Now」・自動確認の3箇所すべての`fb2k::inMainThread`コールバック内に`if (fb2k::mainAborter().is_aborting()) return;`を追加し、アプリ終了中は新規ウィンドウを作らず静かに打ち切るようにした。これを機にRelease構成でのビルド・動作確認も実施し、問題ないことを確認した。

### 入力値のバリデーション強化

Preferencesの「Check interval (days)」について、極端な値や小数の入力を防ぐため、入力欄自体を`EM_SETLIMITTEXT`で2桁までに制限し、読み取り・保存の両方で1〜30日の範囲にクランプするようにした。

### v1.0.0リリース準備

- `dllmain.cpp`のバージョンを`1.0.0`に更新(User-Agent文字列も統一)
- READMEを公開版として全面更新(Features/Usageの最新化、Network Usageセクションの新設、動作環境にWindows 10を追加)
- 両リポジトリ(`foo_component_update_checker`、`foo_component_update_checker-registry`)にMITライセンスの`LICENSE`ファイルを追加
- コンポーネント側READMEに「Network Usage」節(通信先・送信しない情報の明記)、registryリポジトリのREADMEに「透明性(自動収集ではなくレビュー済みPRのみ)」「PRレビューの限界」を追記

### 次にやること

- v1.0.0としてコミット・プッシュ
- Reddit投稿(告知 + コンポーネント情報の募集)の文面作成

---

## 2026-08-08 GitLab / Codeberg対応

v1.0.0公開・Reddit投稿を経て、コメントで寄せられた情報の中にGitHub以外(marc2k3.github.io、SourceForge等)で配布されているコンポーネントの実例があったため、対応サイトの拡大に着手した。まずはAPIが構造化されている**GitLab**と**Codeberg**(Gitea API)から対応した。

### 実装

- `RepositoryMappingEntry`(`repository_mapping.h`)に`source`フィールドを追加。既存の保存データ(v1.0.0以前、sourceフィールド無し)は読み込み時に`"github"`として扱う後方互換を維持
- `repository_mapping_ui.cpp`の`TryParseGitHubUrl`を`TryParseRepositoryUrl`に拡張し、`github.com` / `gitlab.com` / `codeberg.org`のいずれかを自動判定
- `update_check.cpp`に`FetchGitHubLatestRelease` / `FetchGitLabLatestRelease` / `FetchCodebergLatestRelease`を実装し、`mapping.source`に応じて分岐
  - GitLab: `GET /api/v4/projects/{owner}%2F{repo}/releases/permalink/latest`。レスポンスにリリースページの直接URLが含まれないため、`https://gitlab.com/{owner}/{repo}/-/releases/{tag_name}`という形で自前組み立て
  - Codeberg(Gitea API): `GET /api/v1/repos/{owner}/{repo}/releases?limit=1&draft=false&pre-release=false`(配列で返るため先頭要素を使用)
- `remote_registry.cpp`の`findRemoteRegistryEntry`が対応済みsourceとして`gitlab`/`codeberg`も受け付けるよう更新

### 動作確認: GitLab側の403エラーの原因調査

最初にテストした際、登録したGitLabリポジトリで403 Forbiddenが発生。調査の結果、**GitLab側でプロジェクトごとに「Releases機能」自体を無効化できる設定があり、無効化されていると公開リポジトリでも(可視性やトークン権限に関わらず)常に403を返す**という既知の仕様が原因と判明(URLの組み立てミスではなかった)。確実にReleases機能が有効な実例として`inkscape/inkscape`(GitLabへ移行済み、115件のリリースあり)を使って再検証した。

### 動作確認結果

実機で両方とも成功。

- **Codeberg**(`Codeberg/pages-server`): `Update available`と正しく表示され、フル機能(取得・比較・表示)が動作することを確認
- **GitLab**(`inkscape/inkscape`): `Unable to compare`と表示されたが、これは取得自体は成功しており、Inkscapeのタグ命名規則(`INKSCAPE_1_4`)がSemVer形式に沿っていないための正常な挙動(DP-0009: 誤判定するくらいなら比較不能と表示する、の意図通り)。自前組み立てしたリリースページURLも、実際のURLと一致することを確認できた

これで3サイト(GitHub / GitLab / Codeberg)への対応が実データで検証できた。

### 次にやること

- READMEをGitLab/Codeberg対応に合わせて更新
- registryリポジトリ(known_components.json)にも、GitLab/Codeberg由来のエントリを追加していく
- 他のホスティングサイト(SourceForge、個人サイト等)への対応は、構造化されたAPIが無いため難易度が上がる。優先度は下げつつ検討

---

## 2026-08-08(続き) v1.1.0リリース: バージョン更新・Excelマクロ対応・README更新

### バージョン更新

`dllmain.cpp`を`1.1.0`に更新(GitLab/Codeberg対応を含むマイナーバージョンアップ)。User-Agent文字列(`remote_registry.cpp`、`update_check.cpp`)も統一した。

### Excelマクロ(registry_manager.xlsm)の複数サイト対応

以前作成したDB管理用Excelマクロが、GitHub専用の`ParseGitHubUrl`のままだったため、コンポーネント本体側の`TryParseRepositoryUrl`と同じロジックで`ParseRepositoryUrl`(VBA)に拡張した。

- `github.com` / `gitlab.com` / `codeberg.org`を自動判定し、`source`フィールドに反映
- Status列に判定結果(例: `OK (gitlab)`)を表示するようにし、目視確認しやすくした
- README用テーブル生成も、`owner/repo (source)`という形式でリンクを組み立てるよう変更
- テンプレート(`registry_manager_template.xlsx`)の例示行を3サイト分に増やし、Instructionsシートにも対応サイトを明記

ユーザーの環境で実際にマクロを動かして動作確認済み。

### README更新

本体・registry両方のREADMEを、GitHub限定の表現からGitHub/GitLab/Codeberg対応に更新した。

- 本体側: Status、Features、Usage、Network Usage(接続先に`gitlab.com`/`codeberg.org`を追加)を更新
- registry側: 冒頭説明・Schemaの説明を汎用化(`source`は3値のいずれか、という説明に変更)。登録済みコンポーネント一覧表は、Excelマクロの出力形式(`owner/repo (source)`)にそのまま合わせ、将来はマクロの出力をコピペするだけで表を更新できるようにした

### 次にやること

- v1.1.0としてコミット・プッシュ(本体・registry両リポジトリ)
- registryへのGitLab/Codeberg由来エントリの追加(現状registryの中身は全てGitHub)
- 他のホスティングサイト(SourceForge、個人サイト等)への対応は、構造化されたAPIが無いため難易度が上がる。優先度は下げつつ検討

---

## 2026-08-16 marc2k3.github.io対応

「他のホスティングサイトへの対応」の1つ目として、コミュニティで最も有名な個人配布サイトである`marc2k3.github.io`(marc2k3氏、Cover Utils等の作者)への対応に着手した。

### 方針検討

GitHub/GitLab/Codebergと違い、構造化されたReleases APIを持たない個人サイトなので、先に設計方針を決めた。

- 実サイト(`https://marc2k3.github.io/component/cover-utils/`)を確認したところ、Zensicalという静的サイトジェネレータ製で、`class="md-button md-button--primary"`のDownloadボタンにファイル名(バージョン番号入り)が埋め込まれている構造だった
- 「このサイト専用のパーサーをまず作る」か「ユーザーが正規表現を手動登録する汎用機構にする」かで検討し、実例1件(marc2k3)に特化したパーサーを先に作る方針にした。将来他の個人サイトが出てきたら都度追加する

### データモデル拡張: urlモデルの追加

`RepositoryMappingEntry`(`repository_mapping.h`)・`RemoteRegistryEntry`(`remote_registry.h`)の両方に`url`フィールドを追加。`source`によって必須フィールドが変わる設計にした。

- `github` / `gitlab` / `codeberg`: 従来通り`owner` + `repo`が必須(owner/repoモデル)
- `marc2k3`: `url`(コンポーネント個別ページのURL)が必須、`owner`/`repo`は空文字(urlモデル)

`FetchMarc2k3LatestRelease`(`update_check.cpp`)は、登録ページを取得し、Downloadボタンのhrefからファイル名末尾の`-<version>.fb2k-component`を正規表現で抜き出す実装。`releaseUrl`は専用のリリースページが無いため、登録ページURLをそのまま使う。

### つまずいた点(1): バリデーションの見落とし

`repository_mapping.cpp`の`loadRepositoryMapping()`は「`owner`と`repo`が両方揃っていないと無効なエントリとして捨てる」というowner/repoモデル専用のバリデーションのままだった。urlモデルのエントリは`owner`/`repo`が空なので、**保存はできるのに次回読み込み時に静かに消える**という見つけにくいバグになっていた。`source`に応じて必須フィールドを出し分ける形(`isValidEntry`)に直して解消した。

### つまずいた点(2): 生文字列リテラルの自己衝突

`FetchMarc2k3LatestRelease`の正規表現`R"(class="md-button md-button--primary"\s+href="([^"]+)")"`が、正規表現の中身自体に`)"`という並び(`([^"]+)"`の部分)を含んでいたため、C++の生文字列リテラルがそこで早期終端し、`error C2001: 文字列リテラル内の改行`という構文エラーになった。カスタム区切り子(`R"RX( ... )RX"`)に変更して解決。以降、`)"`を含みうる正規表現には全てカスタム区切り子を使う方針にした。

### つまずいた点(3): 文字コード(BOM)の問題

新規追加したファイル(`repository_mapping.h`/`.cpp`、`remote_registry.h`/`.cpp`)にUTF-8 BOMを付け忘れていた。プロジェクトの既存ファイルは全てBOM付きUTF-8で統一されている(2026-08-06のC4819警告の教訓)が、今回はその前提を見落とした。BOM無しだとMSVCがシステムのコードページ(932=Shift-JIS)で解釈し、日本語コメント中のマルチバイト文字が偶然`\`や`"`と誤認識されて大量の構文エラーが連鎖する、という同じ種類の問題を再び踏んだ形になった。今後、新規ファイル作成時はBOM付与を最初のチェック項目にする。

### 動作確認結果

実機で成功。`https://marc2k3.github.io/component/cover-utils/`を登録・保存し、一覧表示・最新バージョン(`1.7`)の取得・比較・リリースページへのリンク遷移まで確認。同サイトの別コンポーネント(Last.fm Playcount Sync等)でも問題なく動作し、サイト内で汎用的に使えることを確認した。

### 次にやること

- SourceForgeへの対応を検討

---

## 2026-08-16(続き) SourceForge対応

`sacddecoder`(Super Audio CD Decoder、複数のfoobar2000コンポーネントを配布)プロジェクトを題材に、SourceForge対応を実装した。

### 設計: marc2k3と違い汎用対応が可能

SourceForgeは各プロジェクトが共通して持つ**ファイル配布用RSSフィード**(`/projects/{project}/rss?path={path}`)を使えることが分かった。これはプラットフォーム自体の機能であり、marc2k3のような個人サイト固有の構造ではないため、**プロジェクトを問わず汎用的に対応できる**。データモデルもmarc2k3で追加済みの`url`フィールドをそのまま流用でき、スキーマ変更は不要だった(`source = "sourceforge"`、`url` = 登録したフォルダのファイル一覧ページURL)。

実データ(`sacddecoder`の`foo_input_sacd`フォルダ)を確認し、2点の設計上の注意点を発見した。

- 同一バージョンに対して複数ファイル(`foo_input_sacd-2.0.25.zip`と`foo_input_sacd-2.0.25_for_foobar-1.6.x.zip`のような互換ビルド用派生)が存在することがある
- ページ上の「Download Latest Version」ボタンは、実際に一番新しいファイルとは食い違う場合がある(メンテナーが意図的に安定版を指定していると思われる)ため信用できない。RSS(実際のアップロード日時順)を見る必要がある

### 実装

`FetchSourceForgeLatestRelease`(`update_check.cpp`)を追加。

1. 登録されたフォルダURLから正規表現でプロジェクト名とパスを抜き出し、RSS URLを組み立てる
2. RSSの`<item><title>`を新しい順(RSSの並び順)に走査し、`-([0-9]+(?:\.[0-9]+)*)\.(zip|rar|7z|fb2k-component)$`に**厳密に**マッチする最初のファイルを採用する

この正規表現1つで、readme.txt等の非パッケージファイル(拡張子不一致)と、`_for_foobar-1.6.x.zip`のような派生ビルド(バージョン部分が数字だけで終わらない)の両方を、特別な除外リストを持たずに自然にスキップできることを確認した(DP-0009: 誤判定するくらいなら比較不能/スキップ)。

`TryParseRepositoryUrl`(`repository_mapping_ui.cpp`)にも`sourceforge.net/projects/`判定を追加。marc2k3と同じurlモデルなので、一覧表示・Suggest for Shared Registryのsnippet生成もmarc2k3と共通の分岐(`source == "marc2k3" || source == "sourceforge"`)にまとめた。

### つまずいた点: isValidEntryへの追加漏れ(marc2k3対応と同じ種類のバグを再発)

`sourceforge`を実装した際、`repository_mapping.cpp`の`isValidEntry()`に`marc2k3`しか書いておらず、`sourceforge`を追加し忘れていた。結果、marc2k3対応時に直したのと全く同じ症状(**保存した直後の一覧再読み込みで、バリデーションに引っかかって静かに消える**)を自分で再現してしまった。urlモデルのsourceを新設するたびに、このチェック箇所へ確実に追加することを今後のチェックリストに入れる。

### 動作確認結果

実機で成功。`https://sourceforge.net/projects/sacddecoder/files/foo_input_sacd/`を登録し、一覧表示・最新バージョン(`2.0.25`。派生ビルドではなく正しい方)の取得・比較まで確認。同プロジェクトの別フォルダ(`sacd_metabase`等)でも別コンポーネントとして問題なく動作し、プロジェクトを問わない汎用実装になっていることを確認した。

これで対応サイトはGitHub / GitLab / Codeberg(owner/repoモデル、構造化API)、marc2k3(urlモデル、サイト固有パーサー)、SourceForge(urlモデル、汎用RSSパーサー)の5種類になった。

### 次にやること

- Excelマクロ(registry_manager.xlsm)をurlモデル対応に更新
- v1.2.0としてリリース

---

## 2026-08-16(続き2) Excelマクロのurlモデル対応・v1.2.0リリース

### Excelマクロ(registry_manager.xlsm)の更新

`ParseRepositoryUrl`(VBA)を、コンポーネント本体側の`TryParseRepositoryUrl`(marc2k3/sourceforge対応後の4引数版)と同じ判定順序・同じ正規化ロジックに揃えて拡張した。

- `IsUrlModelSource(source)`ヘルパーを追加し、`marc2k3`/`sourceforge`ならurlモデル、それ以外ならowner/repoモデルとして`BuildJsonArray`(JSON生成)・`BuildMarkdownTable`(README用テーブル生成)の両方で出し分ける
- urlモデルのMarkdownテーブル表示は、長いURLをそのまま出すと見づらいため`UrlPathForDisplay`で`https://`プレフィックスと末尾スラッシュを除いた短縮形にし、`source: 短縮URL`というラベルでリンクする形にした

ユーザーの環境で実際にマクロを動かし、JSON・Markdownともに問題なく生成されることを確認済み。

### v1.2.0リリース準備

- `dllmain.cpp`のバージョンを`1.2.0`に更新
- User-Agent文字列(`remote_registry.cpp`1箇所、`update_check.cpp`5箇所)を`1.2.0`に統一
- 本体・registry両方のREADMEを更新
  - 本体側: Status/Features/Network Usage/Usageに、marc2k3.github.io・SourceForgeを追加。「Explicitly out of scope」にサイト固有パーサー(marc2k3)の構造的な脆さについての注記を新設(サイト構造が変わった場合、誤判定ではなく明示的なエラーとして確認失敗を表示する、という既存のDP-0009方針をユーザー向けに明文化したもの)
  - registry側: Schemaの説明にurlモデル(`marc2k3`/`sourceforge`)のJSON例を追加。`source`によって必要なフィールドが変わることを明記
  - registry側README修正時、「Registered Componentsテーブルの日本語対訳」が本来の位置(English直後)ではなく別の節の後ろに紛れ込んでいた(前バージョンからの既存の位置ズレ)のを本来の位置に修正した

### 動作確認結果

ビルドして`Help`メニュー・コンポーネント一覧上で`1.2.0`と表示されることを確認。

### 次にやること

- v1.2.0としてコミット・プッシュ(本体・registry両リポジトリ)
- registryへのmarc2k3/sourceforge由来エントリの追加(現状registryの中身は全てGitHub)
- 他のホスティングサイトへの対応は、引き続き優先度を見ながら検討

---

## 2026-08-16(続き3) Remote Registryキャッシュの再発見・dllmainの説明文更新

### Remote Registryキャッシュを踏んだ

registryリポジトリにmarc2k3/SourceForge対応のエントリ(`known_components.json`)を実際にプッシュし、コンポーネント側で「Check for Updates Now」を実行しても、SourceForge/marc2k3で公開されているコンポーネントの情報が反映されない、という報告があった。

原因は`remote_registry.cpp`の`shouldRefetch()`(24時間キャッシュ)だった。開発中ずっと手動確認を繰り返していたため、プッシュ前の時点で一度でも確認を実行していれば、その時のキャッシュ(SourceForge対応前の中身)が24時間経過するまでそのまま使われ続ける。Repository Mappingでの手動登録での確認はこのキャッシュを経由しない(`findRepositoryMapping`はローカル保存データを毎回そのまま読む)ため気付きにくく、しかも以前のバージョンでも同じ現象に遭遇していたが「そのうち直った」で済ませてしまい、24時間キャッシュの存在自体を忘れていた、とのこと。

動作確認のため、`remote_registry.cpp`にTEMP Debugコマンド「Force Refresh Remote Registry (TEMP Debug)」を一時的に追加した。記録済みの最終取得時刻を0にリセットし、次回の`GetRemoteRegistryEntries()`で強制的に再取得させる仕組み。実行して読み込んだ全エントリ(DLL名・source)をコンソールに出力させ、キャッシュが原因であることを確認できた。確認後、このコマンドは削除した(2026-08-07にも一度同種のコマンドを削除しており、今回で2回目)。

**教訓**: Remote Registryのキャッシュ挙動(24時間に1回しか更新しない)は、開発中の動作確認と相性が悪く、繰り返し勘違いの原因になっている。次に同じ状況に遭遇したときのために、この経緯をdev logに残しておく。

### dllmainの説明文をAlbum Trainのスタイルに統一

`foo_albumtrain`側で採用している説明文の書式(機能の一文説明 → 空行 → 著作権表示 → ライセンス → リポジトリURL)に、`foo_component_update_checker`側の`DECLARE_COMPONENT_VERSION`も揃えた。

```
Checks installed third-party foobar2000 components for available updates and notifies you when one is found — it does not auto-download, auto-install, or auto-replace anything.

(C) p2ashiura
Released under the MIT License.
https://github.com/p2ashiura/foo_component_update_checker
```

これまでの「Does not auto-download or auto-install anything.」という2文構成から、auto-replaceも含めた一文にまとめ、READMEの「Explicitly out of scope」の書きぶりとも表現を揃えた。

### 動作確認結果

ビルドして、キャッシュを無視した強制再取得でSourceForge/marc2k3のエントリが正しく読み込まれることを確認。TEMP Debugコマンドの削除、dllmainの説明文(空行込み)の表示も確認済み。

### 次にやること

- v1.2.0としてコミット・プッシュ(本体・registry両リポジトリ)
- registryへのmarc2k3/sourceforge由来エントリの追加は完了。githubのエントリと合わせて件数を随時更新していく
- 他のホスティングサイトへの対応は、引き続き優先度を見ながら検討

---

## 2026-08-16(続き4) foobar.hyv.fi対応

v1.2.0のコミット・プッシュ後、`https://foobar.hyv.fi/`(Case氏の個人配布サイト。HDCDデコーダー、Digital Audio output等を配布)への対応を追加した。

### marc2k3との違い

同じ「個人サイト特化パーサー」のurlモデルだが、2点で構造が異なっていた。

- **バージョンの取得元**: marc2k3はダウンロードリンクのファイル名にバージョンが埋め込まれていたが、hyv.fiはダウンロードリンク(`foo_hdcd.fb2k-component`)にバージョンが含まれておらず、代わりにページ内の情報テーブルに`<tr><td>Version:</td><td>1.22</td></tr>`という形で明示されている。そちらを直接正規表現で読み取る方式にした
- **URLの正規化**: hyv.fiは`?view=foo_hdcd`のようにクエリパラメータ自体がコンポーネントの識別子なので、marc2k3/SourceForgeのように`?`以降を切り捨てると壊れる。フラグメント(`#`)のみ切り落とし、クエリ文字列は残す形にした

実サイト2件(`foo_hdcd`、`foo_out_digital`)のHTMLソースを見せてもらい、`Version:`行の構造が完全に一致していることを確認してから実装した。

### 追加漏れ対策

これまで2回、新しいurlモデルのsourceを追加する際に「特定の1箇所への追加を忘れる」バグ(保存した瞬間に一覧から消える等)を踏んでいたため、今回は実装前に`grep`で`"marc2k3"`/`"sourceforge"`を参照している全箇所(9箇所)を洗い出してから、`"hyv"`を機械的に追加していく進め方に変えた。結果、今回はその種のバグは発生しなかった。

### 動作確認結果

実機で成功。`https://foobar.hyv.fi/?view=foo_hdcd`を登録し、一覧表示・最新バージョン(`1.22`)の取得・比較を確認。`foo_out_digital`(`0.3`)でも同様に動作することを確認した。

対応サイトはGitHub / GitLab / Codeberg(owner/repoモデル)、marc2k3(urlモデル、ファイル名からバージョン抽出)、SourceForge(urlモデル、RSSフィード)、hyv(urlモデル、ページ内テーブルから直接抽出)の6種類になった。

### 次にやること

- dev_logへの記録(このエントリ)は完了
- README(本体・registry両方)、Excelマクロへのhyv対応反映は次回
- v1.3.0としてのバージョン更新は次回

---

## 2026-08-16(続き5) 本体更新確認/Component Repository連携の検討・見送り

「本コンポーネントによる更新確認(手動・自動とも)が実行される前に、foobar2000本体の更新確認とデフォルトのComponent Repository更新確認を先に走らせ、そちらで更新が無ければ本コンポーネントのチェックを実行する」という機能案を検討した(設定から両者を個別にオン/オフできるようにしたい、という要望込み)。

### 調査結果: 技術的な壁

foobar2000本体の「Check for Updates」(アプリ本体)と、Preferences → Components側の「Component Repository」更新確認は、いずれもfoobar2000コア内部の閉じた機能であり、サードパーティ向けSDKには次の2点が公開されていないことが分かった。

1. これらをプログラムから起動する公開API(該当するmainmenu_commandsのGUID等)が見当たらない
2. 仮に起動できたとしても、「更新が見つかったかどうか」を読み取る手段が無い(UIダイアログとして結果を表示するだけの閉じた仕組みで、サードパーティへコールバックや戻り値を返す設計になっていない)

同領域の先行コンポーネント`foo_acfu`(Updates checker for foobar2000)も、公式サイトで「This component does not supersede standard foobar2000 way to check for updates but may complement it.」と明言しており、本体機能と連動するのではなく完全に独立した並行システムとして動かす設計を選んでいる。恐らく同じ制約に行き着いた結果と思われる。

### 判断: 見送り(Cを選択)

「本体側で更新が見つからなかったことを確認してから実行する」という条件付き直列実行は、結果を読み取る手段が無い以上、原理的に組めない。次の3案を提示した。

- 案A: 本体の「Check for Updates」を発火だけさせ、結果は待たずに本コンポーネントのチェックを続けて実行する
- 案B: チェック実行時に、本体・Component Repositoryの更新確認も行うよう案内するUIを添える(連携ではなく案内に留める)
- 案C: 見送り

案A/Bはいずれも「更新が無ければ実行する」という当初の要望の核心(結果に応じた条件分岐)を満たせないため、C(見送り)を選択した。

**教訓**: foobar2000コア内部の機能(本体更新確認、Component Repository)は、SDK経由で外部から観測・制御できない設計になっている。今後同種の「本体機能と連動させたい」という要望が出た場合、まず「結果を読み取れるか」を最初に確認すること。

### 次にやること

- README(本体・registry両方)、Excelマクロへのhyv対応反映
- v1.3.0としてのバージョン更新

---

## 2026-08-17 foobar.hyv.fi対応・v1.3.0リリース

### hydrogenaud.io board 33の検討・見送り

前回見送った「本体機能との連携」に続き、「更新情報の母数が最も大きそうなソース」として`https://hydrogenaudio.org/index.php/board,33.0.html`(3rd Party Pluginsフォーラム)への対応を検討した。

- `board=33`はSMF(Simple Machines Forum)ベースの掲示板で、コンポーネント1つにつきスレッド1本という構成。161ページ分のカテゴリがあり母数自体は大きい
- ただし、marc2k3/hyv.fiのような「サイト全体で統一されたテンプレート」が無く、スレッドごと・作者ごとにバージョン表記の書き方が全くバラバラ(構造化されたバージョン欄が存在しない)
- RSSフィードについても、第三者が「NewsBlurに登録しようとしたらRSSフィードとして認識できないというエラーが出た」と報告しているのを見つけ、機能していない/壊れている可能性がある
- 実際にスレッドを1つ取得してみたところ本文が空で返ってきており(セッション必須かBot対策の可能性)、ページ取得自体の実現性にも疑問が残った
- 登録モデルも「サイト単位で汎用対応」(SourceForge型)ではなく「スレッド単位の個別登録」(marc2k3型)にせざるを得ず、しかも作者の数だけパーサーの書き方が必要になるため、実装コストに対してリターンが小さいと判断し、見送った(Cを選択)

### foobar.hyv.fi対応

Case氏の個人配布サイト(`foo_hdcd`、`foo_out_digital`等)への対応を追加した。marc2k3と同じ「このサイト専用パーサー」のurlモデルだが、2点構造が異なっていた。

- **バージョンの取得元**: ダウンロードリンクのファイル名にバージョンが含まれておらず(`foo_hdcd.fb2k-component`)、代わりにページ内の情報テーブルに`<tr><td>Version:</td><td>1.22</td></tr>`という形で明示されているので、そちらを直接正規表現で読み取る方式にした
- **URLの正規化**: `?view=foo_hdcd`のようにクエリパラメータ自体がコンポーネントの識別子なので、marc2k3/SourceForgeと違い`?`以降を切り捨てず、フラグメント(`#`)のみ切り落とす

実サイト2件(`foo_hdcd`、`foo_out_digital`)のHTMLソースで`Version:`行の構造が完全に一致することを確認してから実装。前回・前々回の教訓を踏まえ、実装前に`marc2k3`/`sourceforge`を参照している全箇所(9箇所)を`grep`で洗い出してから`hyv`を機械的に追加する進め方を徹底し、追加漏れによるバグは今回発生しなかった。

実機で動作確認: `https://foobar.hyv.fi/?view=foo_hdcd`(`1.22`)、`foo_out_digital`(`0.3`)ともに登録・比較まで確認済み。

### Excelマクロ・README更新

- `registry_manager_macro.txt`: `hyv`対応を追加(既存9箇所+新規の判定ブロック)。あわせて、urlモデルのMarkdownテーブル表示を`source: パス`という形式から、owner/repoモデルと揃えた`パス (source)`という表示順に変更した
- registryリポジトリ側の`README.md`(→以後`registry_README.md`という名前で区別することにした。コンポーネント本体の`README.md`と出力時に名前が衝突していたため): Schema節に`hyv`を追加(5→6種類のsource)。表記統一に伴い、既存の`marc2k3:`/`sourceforge:`プレフィックス付きエントリ(24件)も新しい表記に一括修正。ついでに`foo_resume`の完全重複行を1件解消。表が129件まで増えて長くなってきたため、`<details><summary>`で折りたためるようにした
- コンポーネント本体の`README.md`: Status/Features/Explicitly out of scope/Network Usage/Usageの各節にfoobar.hyv.fiを追加。編集の過程で「Explicitly out of scope」の日本語部分に生じていた文末重複(コピー時のミス)も合わせて修正

### v1.3.0リリース準備

- `dllmain.cpp`のバージョンを`1.3.0`に更新
- User-Agent文字列(`remote_registry.cpp`1箇所、`update_check.cpp`6箇所。`FetchHyvLatestRelease`分が増えたため前回の5箇所から1つ増加)を`1.3.0`に統一
- README(本体)のバージョン表記も`v1.3.0`に更新

ビルドして`1.3.0`表示を確認済み。

### 次にやること

- v1.3.0としてコミット・プッシュ(本体・registry両リポジトリ)
- registryへのhyv由来エントリの追加
- 他のホスティングサイトへの対応は、hydrogenaudioの一件も踏まえて優先度を見ながら検討

---

## 2026-08-17(続き) SDKリポジトリの共有化

複数のコンポーネント(`foo_albumtrain`、`foo_component_update_checker`)を並行開発するようになり、各リポジトリ直下に同じSDK(`SDK-2025-03-07`、`WTL10_01_Release`)を重複して置くのがフォルダサイズ的に厳しくなってきたため、SDKを1箇所に集約する方法を検討・実施した。

### 検討した案と選定理由

- `Directory.Build.props`をリポジトリの上位階層に置く案も検討したが、これはあしぅらさんの環境固有のフォルダ構成(複数リポジトリが同じ階層に並んでいる)に依存する設定になってしまい、**単体のリポジトリをcloneしただけの他の人の環境では成立しない**という問題があった。READMEのBuilding手順(「SDKフォルダをリポジトリ直下に置く」)を変えずに済ませたかったため、この案は不採用
- 代わりに**NTFSディレクトリジャンクション**(`mklink /J`)を採用。SDKの実体を`_shared_sdk`という共有フォルダに1つだけ置き、各リポジトリ直下の`SDK-2025-03-07`/`WTL10_01_Release`は、そこを指すジャンクションに置き換える方式。これなら`.vcxproj`・`.slnx`・`#include`パス、README手順のいずれも変更不要で、他の人の環境にも一切影響しない

### 実施

- `Album-Train`リポジトリは`foo_component_update_checker`と違い、リポジトリ直下ではなく`Album Train`という1階層下のサブフォルダにSDKが置かれている構造だった(`.slnx`を見て判明)。ジャンクションを張る場所はリポジトリごとに実際の配置を確認してから決める必要があった
- 両リポジトリともビルドが通り、GitHub Desktop上でも変更が検出されないことを確認(SDKフォルダは元々`.gitignore`で除外されているため、ジャンクション化してもGitからは完全に透過的)

### 動作確認結果

`foo_component_update_checker`・`Album Train`、両方ともRelease x64ビルドが通ることを確認。想定通り無風で移行できた。

### 次にやること

- 今後新しいコンポーネントを作る際も、clone後に`mklink /J`を1回叩くだけで共有SDKに乗せられる

---

## 2026-08-17(続き2) レジストリのリンクチェッカー追加・SourceForgeのCloudflare Bot判定を発見

### 課題認識

`known_components.json`の件数が増えてきた(現状129件)ことで、コード側の性能面はまだ全く問題にならない規模だが、**Pull Requestのレビューや、リポジトリが消えた/配布が止まったエントリの検知が人力のボトルネックになる**という話になった。対策として、登録済みURLを巡回してHTTPステータスコードを確認する「リンクチェッカー」をExcelマクロとして追加することにした。

### 実装

- 既存の`registry_manager_macro.txt`から`ParseRepositoryUrl`/`IsUrlModelSource`/`SiteDomain`の3関数を`Private`→`Public`に変更し、新モジュール`registry_link_checker_macro.txt`から再利用できるようにした(URL解析ロジックを2箇所に重複させないため)
- `WinHttp.WinHttpRequest`(Windows標準搭載)でGETリクエストを送り、ステータスコードに応じて`OK`/`DEAD`(404)/`WARN`(それ以外)/`ERROR`(接続失敗)に分類。Components/Disabled両シートのE列(判定)・F列(確認日時)に結果を書き込む
- リクエスト間隔を0.5秒空け、User-Agentも明示するなど、サーバーへの配慮は入れた
- **自動でdisabled化はしない**設計にした。404が返ってきても一時的な問題の可能性があるため、最終判断は引き続き人力(DP-0009と同じ考え方)

### SourceForgeがCloudflareのBot判定チャレンジで弾かれることが判明

実際に動かしたところ、SourceForgeのエントリが全て`403`になった。切り分けのため以下を順に試した。

1. チェック対象を「登録したフォルダページ」から、本体コンポーネントが実際に使っている「RSSのURL」(`FetchSourceForgeLatestRelease`と同じ導出ロジック)に変更 → 変化なし
2. `Accept`/`Accept-Language`ヘッダーを追加してブラウザらしく見せる → 変化なし
3. レスポンスヘッダー・本文冒頭をシートに書き出す診断ロジックを追加して実際の中身を確認 → `Server: cloudflare`、`Cf-Mitigated: challenge`、本文に`Just a moment...`という、**Cloudflareのインタラクティブ(JavaScript実行が必要)なBot判定チャレンジ**であることが判明

このチャレンジはJavaScriptを実際に実行して解答する仕組みのため、`WinHttpRequest`はもちろん`ServerXMLHTTP`のような別のHTTPクライアントに切り替えても、原理的に突破できない。ヘッダー調整でどうにかなる話ではなかった。

最終的に、SourceForgeのエントリは**HTTPリクエスト自体を送らずスキップ**し、「手動確認推奨」として扱う方針に変更した(無駄なリクエストを送り続けるのは相手のBot対策システムに対しても行儀が悪いため)。

### 教訓・今後のリスク

本体コンポーネントの`FetchSourceForgeLatestRelease`は過去の実機確認では正常に取得できていたが、**Cloudflareの判定はIPの評判やアクセスパターンによって変動する**ため、「今動いている」ことは「今後もずっと動く」ことを保証しない。将来、本体側の実際のユーザー環境でも同じチャレンジに引っかかり、SourceForge由来のコンポーネントの更新確認が失敗する可能性がある。

現状の実装(`FetchSourceForgeLatestRelease`)はHTTPステータスコードを確認せずに本文を正規表現でパースする作りなので、チャレンジページが返ってきてもクラッシュはせず「No release archive with a recognizable version number was found」という比較的自然なエラーになるはずだが、実際の原因(Bot判定)がユーザーに伝わらない点は改善の余地がある。優先度は高くないが、頭の片隅に置いておくこと。

### 次にやること

- リンクチェッカーの本格運用(定期的な実行)
- 余裕があれば、`FetchSourceForgeLatestRelease`のエラーメッセージをもう少し具体的にすることを検討(Cloudflareチャレンジと推測できる場合の専用メッセージ等)

---

## 2026-08-17(続き3) 32bit対応・v2.0.0リリース

動作確認できる32bit環境が整ったため、これまで64bit専用だった本コンポーネントを32bit(Win32)にも対応させた。

### ビルド設定の追加

構成マネージャーで既存の`x64`構成をコピーする形で`Win32`構成を追加。ソリューション内の全プロジェクト(`pfc`、`foobar2000_SDK`、`foobar2000_component_client`、`shared`、`columns_ui_sdk`、本体)に反映されることを確認。

事前の懸念(ウィンドウハンドル周りの32/64bit互換性)は、`repository_mapping_ui.cpp`が既に`INT_PTR`/`*Ptr`系のAPIで書かれていたため杞憂に終わり、**コード側は無変更でビルドが通った**。

### 動作確認結果

Win32・x64、両構成でビルドが通り、GitHub(構造化API)・marc2k3・SourceForge(いずれもurlモデル)で最新バージョンの取得まで確認できた。想定通り無風だった。

### 用語整理: Win32とx86の違い

作業の過程で「Win32構成でビルドしたものは、x86ビルドとして扱って良いか」という点を整理した。結論は**同じもの**: 「x86」はCPU命令セット(32bit)を指すハードウェア寄りの呼び方、「Win32」はVisual Studioがそのターゲットをビルドする際のプラットフォーム名(Win32 API時代からの名残)で、生成される機械語は同一。ソリューションに両方を追加する必要は無い。

### fb2k-componentへの32/64bit同梱

Hydrogenaudio Wikiの開発者向けドキュメントを確認し、1つの`.fb2k-component`(実体はzip)に両アーキテクチャを同梱する場合の構造を確認した。

```
foo_component_update_checker.fb2k-component
├── foo_component_update_checker.dll      ← 32bit版(レガシー/ルート)
└── x64/
    └── foo_component_update_checker.dll  ← 64bit版
```

64bit版のfoobar2000は展開時にルートのファイルをx64サブフォルダの中身で上書きして使う、という仕組み。梱包作業の自動化(バッチ化)は、各ビルドを実機で個別に動作確認してからパッケージ化する運用のため、ファイルの置き場所が都度変わり自動化に向かないとの判断で見送った。

### VALIDATE_COMPONENT_FILENAMEはリネームすると読み込み失敗する

「ビルド出力のDLL名の末尾に32/64の区別を付けたい」という要望が出たが、調査の結果`VALIDATE_COMPONENT_FILENAME`はディスク上の実ファイル名を厳密にチェックする仕組みで、リネームすると**foobar2000への読み込み自体が失敗する**ことが分かった。

代わりに、他のfoobar2000コンポーネント(`foo_sid`)の実例を参考に、**DLLの名前は変えず、32bit版・64bit版それぞれ専用のポータブルfoobar2000フォルダに分けて配置する**という対処法を提示した。ただし、あしぅらさんの環境では既に「32bit版はポータブル版、64bit版はインストール版」という運用になっており、フォルダの違いで区別できているとのことで、追加対応は不要と判断した。

### 実機での最終確認

`.fb2k-component`化した上で、32bit版・64bit版それぞれのfoobar2000に実際に導入し、両方とも問題なく動作することを確認済み。

### README・バージョン更新

- Requirements節を`32-bit or 64-bit`に、Building節に`Release|Win32`/`Debug|Win32`対応を追記
- 冒頭の説明文から`(64-bit)`表記を削除(32bit対応後は不要なため)
- Status節を「今回のアップデート内容(32bit対応)」だけに絞った記載に変更(以前あった対応サイト一覧はFeatures/Network Usage/Usage側に既に詳しく載っているため、Statusから削除しても情報は失われない)
- バージョンを`v2.0.0`に更新。今回のあしぅらさんの言葉を借りると「機能的には大きなことはしていないが、ユーザーから見ればかなり大きな変更」という判断でメジャーバージョンを上げた
- `dllmain.cpp`のバージョン表記、User-Agent文字列(`remote_registry.cpp`1箇所、`update_check.cpp`9箇所)も`2.0.0`に統一

### 次にやること

- v2.0.0としてコミット・プッシュ(本体リポジトリ)
- Reddit投稿の準備

---

## 2026-08-17(続き4) v2.0.0のReddit告知・UI表記の整理(v2.0.1)

### Reddit告知

v2.0.0(32bit対応)のコミット・プッシュ後、これまでと同じ体裁でReddit投稿文を作成した。今回は特定のユーザー要望から生まれた機能ではなく、「32bit環境のユーザーやコンポーネントがまだ一定数残っている」という認識から着手したものである旨を明記した。

### UI表記の整理: 「Repository」という言葉が実態と合わなくなっていた

対応サイトが6種類(GitHub/GitLab/Codeberg=owner/repoモデル、marc2k3/SourceForge/hyv=urlモデル)まで増えたことで、UI・README上の「Repository」という言葉が実態と合わなくなってきているのではという指摘があった。実際に洗い出すと、確かにmarc2k3/hyvは「コンポーネント個別のページ」、SourceForgeは「ファイル一覧のフォルダ」であり、「リポジトリ」と呼ぶには無理があった。

「ダウンロードページ」という案も検討したが、GitHub/GitLab/Codebergで実際に貼るのはリポジトリのルートURL(ダウンロードページそのものではない)、SourceForgeもファイル一覧フォルダであり、これも完全には当てはまらないと判断。最終的に**「Page URL」/「公開ページのURL」**という、6サイト全てに共通して当てはまる表現に統一した。

### 変更範囲の切り分け: UI文字列のみ、内部識別子は変更しない

`RepositoryMappingEntry`、`repository_mapping.h`、`ShowRepositoryMappingDialog`等の内部C++識別子は、今回変更しないと決めた。理由:

- ユーザーには一切見えない、開発者だけが見る名前
- ファイル名・構造体名・関数名の変更は、`marc2k3`/`sourceforge`/`hyv`追加のたびに踏んできた「1箇所への追加漏れ」バグと同種のリスクを、プロジェクト全体に対して一度に負うことになる
- 内部の実装名とUI上の表示名が完全一致している必要は無い(実務でもよくあること)

この切り分けにより、変更はユーザーに見える文字列のみに限定できた。

### 実装

- `repository_mapping_ui.cpp`: ダイアログタイトル`Repository Mapping`→`Manage Component Sources`(文字数増加に伴い`title[24]`→`title[32]`に配列サイズも拡張)、入力欄ラベル`Repository URL:`→`Page URL:`、エラーメッセージ4箇所
- `preferences_page.cpp`: ボタン`Manage Repositories...`→`Manage Sources...`(ダイアログタイトルより短い、ボタン幅に収まる表現を選定)、「一致するエントリが無い」エラーメッセージ
- `update_check.cpp`: Helpメニュー版の同エラーメッセージも同様に修正
- README: Features/Remote Registry/Usageに残っていた`Manage Repositories...`/`リポジトリのURL`という古い表記を、実際のUIに合わせて`Manage Sources...`/`公開ページのURL`に更新。Status節にも今回の変更内容を追記

機能面の変更は無く表記の統一のみのため、パッチバージョンとして`v2.0.1`に更新(`dllmain.cpp`、User-Agent文字列、README)。

### 動作確認結果

ビルドして、ダイアログ・ボタン・エラーメッセージの表示、`2.0.1`のバージョン表示を確認済み。

### 次にやること

- v2.0.1としてコミット・プッシュ

---

## 2026-08-22 Reddit要望への対応・Preferencesにレジストリ導線追加(v2.1.0)

### きっかけ

Redditでの告知に対し、「レジストリの既知コンポーネント一覧を、リンク付きで見られるようにしてほしい」という機能要望が来た。単に「レジストリ側に表がある」と返信するだけでも足りるが、Preferencesからワンクリックで飛べるボタンを足す案もあるのではと考え、設計として検討した。

### 設計検討: どこまでを本体の責務にするか

要望には2つの側面があると整理した。

- レジストリの一覧をアプリ内に表示すること自体は、Mission(「更新情報を必要なときだけ知らせる補助機能」)からは外れる。導入済みかどうかに関係なく全件を検索・閲覧できるUIは「発見(discovery)」機能であり、これまで守ってきた「一度に一つずつ」「Phase単位で区切る」規律にも合わない。すでに`registry_README.md`側に人間が読める表があるため、同じ情報をアプリ内にも持つと二重メンテナンスになる
- 一方、「Preferencesにレジストリへのリンクボタンを1つ置く」ことは、既存の「リリースページをブラウザで開く」パターンの延長でしかなく、コストとリスクが全く違う

結論として、**一覧表示機能そのものは見送り、Preferencesに固定URLを開くだけのボタンを1つ追加する**という切り分けで進めることにした。

### Reddit返信

上記の結論に沿って、「レジストリ側に表がある。Preferencesからワンクリックで開けるようにもする予定」という趣旨の返信を英訳し、レジストリへのリンクを添えて投稿した。

### 実装: 「Open Known-Component Registry...」ボタン

`preferences_page.cpp`に、「Manage Sources...」「Check for Updates Now」の下に「Open Known-Component Registry...」ボタンを追加。クリック時は`ShellExecuteA`でレジストリリポジトリのURL(`https://github.com/p2ashiura/foo_component_update_checker-registry`)を開くだけの、状態も持たない最小実装。

- ボタン用ID(`IDC_PP_OPEN_REGISTRY_BTN`)を追加
- URLは`kRegistryUrl`定数としてGUID定義の近くに配置
- `<shellapi.h>`インクルードと`shell32.lib`リンクを追加

あしぅらさんがビルドし、ボタンからレジストリページが正しく開くことを実機で確認済み。

### README更新(本体側)

今回の機能追加に合わせ、本体`README.md`のStatus/Features/Usageの各節を更新。バージョン表記もv2.1.0に更新した(実際のソース側バージョン更新はあしぅらさんが実施し、ビルドして`2.1.0`表示・動作を確認済み)。

### レジストリ側README(`registry_README.md`)の整理

以下、まとめて対応した。

- 194件まで増えた`known_components.json`対応表の`<details><summary>`による折りたたみを廃止し、常時展開表示に変更(折りたたむ方針自体を見直した)
- 古いボタン名の残存(`Manage Repositories...`)を、v2.0.1で改名済みの`Manage Sources...`に修正。Contributing節に2箇所残っていた
- 今後、レジストリ側のREADMEはファイル名を`README-registry.md`とし、本体側の`README.md`と名前で区別する運用にすることにした

### 日本語文中の半角スペース整理

本体・レジストリ両方のREADMEについて、日本語の地の文中に英語的な書き癖(em dash前後の半角スペース、`— `)が紛れ込んでいた箇所を機械的に洗い出し、詰めた(本体側12箇所、レジストリ側3箇所)。あわせて`dev_log.md`側にも「Help メニュー」(他は「Helpメニュー」)という表記ゆれが1箇所見つかっている(今回は未対応)。

### 次にやること

- v2.1.0として本体・レジストリ両リポジトリをコミット・プッシュ
- `dev_log.md`内の表記ゆれ(「Help メニュー」)の修正要否を判断
