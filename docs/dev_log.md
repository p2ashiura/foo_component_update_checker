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
