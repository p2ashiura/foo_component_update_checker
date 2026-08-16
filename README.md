# foo_component_update_checker

foobar2000 (64-bit) component that checks installed third-party components
for available updates and notifies the user. It does not auto-download,
auto-install, or auto-replace anything — it only tells you an update exists
and takes you to the release page.

foobar2000 (64bit版) 用コンポーネント。導入済みのサードパーティー製コンポーネントについて、
更新の有無をインターネット上で確認し、ユーザーへ通知する。DLLの自動ダウンロード・
自動インストール・自動置換は行わない。更新の存在を知らせ、公開ページへ案内するところまでを担当する。

---

## Status / 現在の状態

v1.2.0. GitHub, GitLab, and Codeberg Releases are supported as update
sources, along with two non-API sources: marc2k3.github.io (a
site-specific parser for that well-known personal component page) and
SourceForge (a generic RSS-feed-based parser that works for any
SourceForge project). Support for further sites may be added in the
future (the registry's `source` field is designed with this in mind).

v1.2.0。GitHub・GitLab・CodebergのReleasesに加えて、APIを持たない2つの
ソースにも対応した: marc2k3.github.io(有名なfoobar2000コンポーネント
配布サイトに特化したパーサー)と、SourceForge(どのSourceForgeプロジェクトにも
汎用的に使えるRSSフィードベースのパーサー)。さらなるサイトへの対応は将来
追加する可能性がある(Registryの`source`フィールドは、この拡張を見込んだ
設計になっている)。

## Features / 機能

**English**

- Detect installed components (DLL name, display name, version) via the
  foobar2000 SDK
- Register a repository per component by pasting its URL in
  **Preferences → Tools → Component Update Checker → Manage
  Repositories...** — the site is detected automatically from the URL.
  Supported sites:
  - GitHub, GitLab, Codeberg — Releases API, any repository
  - marc2k3.github.io — site-specific page parser (this one site only)
  - SourceForge — RSS feed, works for any project's files folder

  or rely on the shared
  [known-components registry](#remote-registry--既知コンポーネントdb) for
  components that are already listed there
- Fetch latest release info and compare versions (SemVer-aware, including
  pre-releases such as `1.0.0-beta`); shows "unable to compare" instead of
  guessing when version formats don't match
- Manual check ("Check Third-Party Component Updates" in the Help menu, or
  "Check for Updates Now" in Preferences), shown in a popup window with a
  clickable link to each release page
- Automatic check on startup (delayed ~20s, configurable interval of 1–30
  days). By default only pops up when an update is found; this can be
  changed in Preferences to also show communication errors, or to always
  show a result regardless
- Users can propose additions to the shared registry via a
  **Suggest for Shared Registry...** button, which opens a pre-filled pull
  request flow on GitHub — no account setup or API token needed
- Supports Windows dark mode, matching the rest of foobar2000's Preferences
  dialog
- All settings live in **Preferences → Tools → Component Update Checker**

**日本語**

- foobar2000 SDK経由で導入済みコンポーネントを検出(DLL名・表示名・バージョン)
- **Preferences → Tools → Component Update Checker → Manage
  Repositories...**で、リポジトリのURLを貼り付けるだけでコンポーネントごとに
  登録できる(サイトはURLから自動判定される)。対応サイト:
  - GitHub・GitLab・Codeberg — Releases API、任意のリポジトリに対応
  - marc2k3.github.io — このサイト専用のページパーサー(このサイト限定)
  - SourceForge — RSSフィードベース、どのプロジェクトのファイルフォルダにも対応

  もしくは、既に
  [共有の既知コンポーネントDB](#remote-registry--既知コンポーネントdb)に
  載っているコンポーネントなら、登録不要でそのまま確認できる
- 最新リリース情報を取得しバージョン比較(SemVer準拠、`1.0.0-beta`のような
  prerelease表記にも対応)。表記が読み取れない場合は誤判定せず「比較不能」と表示
- 手動確認(Helpメニューの「Check Third-Party Component Updates」、または
  Preferencesの「Check for Updates Now」)。結果はポップアップウィンドウに
  一覧表示され、各リリースページへのリンクをクリックで開ける
- 起動時の自動確認(約20秒遅延実行、間隔は1〜30日で設定可能)。既定では
  更新が見つかったときだけポップアップするが、Preferencesで通信エラーも
  表示する設定や、常に結果を表示する設定に変更できる
- **Suggest for Shared Registry...**ボタンから、共有DBへの追加をユーザー自身が
  提案できる(GitHub上でPull Requestの下書きが自動的に開かれる。アカウント
  設定やAPIトークンは不要)
- Windowsのダークモードに対応し、foobar2000本体のPreferencesダイアログと
  見た目が揃う
- 設定は**Preferences → Tools → Component Update Checker**に集約

### Explicitly out of scope / 対象外

**English**

- Auto-download, auto-install, auto-replace of component DLLs
- Verifying the safety/quality of any component
- Sending library contents, playback history, or playlists anywhere
- Component identity is based on DLL name (not GUID); the foobar2000 SDK's
  `componentversion` interface does not expose a reliable per-component GUID
- Site-specific page parsers (currently marc2k3.github.io) are inherently
  fragile — if that site's page structure changes, checking fails with an
  explicit error rather than a wrong result. SourceForge, by contrast, is
  supported generically via its RSS feed and is not tied to any one
  project

**日本語**

- コンポーネントDLLの自動ダウンロード・自動インストール・自動置換
- コンポーネントの安全性・品質の保証
- 音楽ライブラリ・再生履歴・プレイリスト等の外部送信
- コンポーネントの識別はDLL名ベース(GUIDではない)。foobar2000 SDKの
  `componentversion`インターフェースからは、信頼できるコンポーネント単位の
  GUIDが取得できないため
- サイト固有のページパーサー(現状marc2k3.github.io)は構造上壊れやすい —
  対象サイトのページ構造が変わった場合、誤った結果を返すのではなく明示的な
  エラーとして確認失敗を表示する。一方SourceForgeはRSSフィード経由の汎用対応
  であり、特定のプロジェクトに依存しない

## Remote Registry / 既知コンポーネントDB

**English**

Components not registered locally are looked up in a shared, static JSON
file maintained at
[foo_component_update_checker-registry](https://github.com/p2ashiura/foo_component_update_checker-registry)
(fetched anonymously from `raw.githubusercontent.com`, no API key or server
involved). Locally registered repositories (via **Manage Repositories...**)
always take priority over this shared registry. The cache refreshes at most
once every 24 hours; failures fall back silently to the last known-good
cache.

Contributions to the shared registry are welcome — see that repository's
README for details, or use the **Suggest for Shared Registry...** button
inside the component.

**日本語**

ローカルに登録されていないコンポーネントは、
[foo_component_update_checker-registry](https://github.com/p2ashiura/foo_component_update_checker-registry)
で管理されている共有の静的JSONファイルを参照する(`raw.githubusercontent.com`から
匿名取得。APIキーやサーバーは不要)。**Manage Repositories...**でローカルに
登録した内容は、常にこの共有DBより優先される。キャッシュは最大24時間に1回まで
更新を試み、失敗時は直近の取得済みキャッシュへ静かにフォールバックする。

共有DBへの投稿は歓迎している。詳細はそちらのリポジトリのREADMEを参照するか、
コンポーネント内の**Suggest for Shared Registry...**ボタンを使うこと。

## Network Usage / 通信について

**English**

This component connects to the internet on its own (to check for updates).
Specifically:

- `api.github.com` — to check installed components' GitHub Releases
- `gitlab.com` — to check installed components' GitLab Releases
- `codeberg.org` — to check installed components' Codeberg Releases
- `marc2k3.github.io` — to check installed components hosted on that site
- `sourceforge.net` — to check installed components' SourceForge project
  file feeds
- `raw.githubusercontent.com` — to fetch the shared
  [known-components registry](#remote-registry--既知コンポーネントdb)

No personal data, library contents, playback history, or file paths are
ever sent. Network usage is minimal (small periodic checks), but it does
happen over your own connection — this is disclosed here so there are no
surprises.

**日本語**

このコンポーネントは、更新確認のために自発的にインターネットへ接続する。
具体的には:

- `api.github.com` — 導入済みコンポーネントのGitHub Releasesを確認するため
- `gitlab.com` — 導入済みコンポーネントのGitLab Releasesを確認するため
- `codeberg.org` — 導入済みコンポーネントのCodeberg Releasesを確認するため
- `marc2k3.github.io` — 導入済みコンポーネントのうち、当該サイトで配布されて
  いるものを確認するため
- `sourceforge.net` — 導入済みコンポーネントのSourceForgeプロジェクトの
  ファイルフィードを確認するため
- `raw.githubusercontent.com` — 共有の
  [既知コンポーネントDB](#remote-registry--既知コンポーネントdb)を取得するため

個人情報・音楽ライブラリの内容・再生履歴・ファイルパス等は一切送信しない。
通信量はごくわずか(小さな定期確認のみ)だが、ユーザー自身の回線を使って
発生するため、事前にここで明示しておく。

## Requirements / 動作環境

- foobar2000 v2.x, 64-bit
- Windows 10/11

## Building / ビルド方法

**English**

1. Clone this repository.
2. Place the `SDK-2025-03-07` (foobar2000 SDK + Columns UI SDK + pfc/shared)
   and `WTL10_01_Release` folders alongside `foo_component_update_checker.vcxproj`.
   These are not committed to this repository — obtain them separately.
3. Open `foo_component_update_checker.slnx` in Visual Studio 2022 and build
   the `Release|x64` configuration (or `Debug|x64` for development).

The solution builds the SDK helper libraries (`pfc`, `foobar2000_SDK`,
`foobar2000_component_client`, `shared`, `columns_ui_sdk`) as build
dependencies before linking the main component.

This project also bundles [nlohmann/json](https://github.com/nlohmann/json)
(single header, `third_party/nlohmann/json.hpp`, MIT License) for parsing
GitHub API responses.

**日本語**

1. このリポジトリをclone
2. `SDK-2025-03-07`(foobar2000 SDK / Columns UI SDK / pfc / shared)と
   `WTL10_01_Release`フォルダを`foo_component_update_checker.vcxproj`と同じ階層に配置する。
   これらはリポジトリにはコミットしていないため、別途入手すること。
3. Visual Studio 2022で`foo_component_update_checker.slnx`を開き、
   `Release|x64`構成でビルドする(開発時は`Debug|x64`)。

ソリューションは、本体をリンクする前にSDK側のヘルパーライブラリ
(`pfc`, `foobar2000_SDK`, `foobar2000_component_client`, `shared`, `columns_ui_sdk`)
をビルド依存関係としてビルドする。

GitHub APIのレスポンス解析には[nlohmann/json](https://github.com/nlohmann/json)
(単一ヘッダー、`third_party/nlohmann/json.hpp`、MITライセンス)を同梱している。

## Usage / 使い方

**English**

1. Open **Preferences → Tools → Component Update Checker**.
2. Click **Check for Updates Now** — components already listed in the
   [shared registry](#remote-registry--既知コンポーネントdb) will be checked
   automatically, no registration needed.
3. For anything not covered, click **Manage Repositories...** and paste the
   component's repository URL to associate it. Examples:
   - `https://github.com/owner/repo`
   - `https://gitlab.com/owner/repo`
   - `https://codeberg.org/owner/repo`
   - `https://marc2k3.github.io/component/xxx/`
   - `https://sourceforge.net/projects/<project>/files/<folder>/`
4. Adjust **Automatically check for updates**, **Check interval (days)**,
   and the notification level as needed.

**日本語**

1. **Preferences → Tools → Component Update Checker**を開く
2. **Check for Updates Now**をクリックする —
   [共有DB](#remote-registry--既知コンポーネントdb)に既に載っている
   コンポーネントは、登録不要でそのまま確認される
3. カバーされていないコンポーネントは、**Manage Repositories...**から
   リポジトリのURLを貼り付けて紐付ける。例:
   - `https://github.com/owner/repo`
   - `https://gitlab.com/owner/repo`
   - `https://codeberg.org/owner/repo`
   - `https://marc2k3.github.io/component/xxx/`
   - `https://sourceforge.net/projects/<project>/files/<folder>/`
4. 必要に応じて**Automatically check for updates**・**Check interval
   (days)**・通知レベルを調整する

## Docs / 関連文書

See [`docs/dev_log.md`](docs/dev_log.md) for development history, design
decisions, and rationale behind them.

開発の経緯・設計判断とその理由については[`docs/dev_log.md`](docs/dev_log.md)を参照。

## License / ライセンス

MIT License. See [`LICENSE`](LICENSE) for the full text.

MITライセンス。全文は[`LICENSE`](LICENSE)を参照。
