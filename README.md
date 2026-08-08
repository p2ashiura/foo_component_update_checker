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

Phase 1 and the core of Phase 2 (remote registry) are complete (private
repo). Detection, registration, manual/automatic check, comparison,
notification, and a shared known-components database are all implemented
and working. Not yet released publicly.

Phase 1、およびPhase 2の中核(Remote Registry)まで完成(非公開リポジトリ)。
検出・登録・手動/自動確認・比較・通知、そして共有の既知コンポーネントDBまで
実装済みで動作する。まだ一般公開はしていない。

## Features / 機能

**English**

- Detect installed components (DLL name, display name, version) via the
  foobar2000 SDK
- Register a GitHub repository per component from **Preferences → Tools →
  Component Update Checker → Manage Repositories...** — or rely on the
  shared [known-components registry](#remote-registry--既知コンポーネントdb)
  for components that are already listed there
- Fetch latest release info from GitHub Releases and compare versions
  (SemVer-aware, including pre-releases such as `1.0.0-beta`); shows
  "unable to compare" instead of guessing when version formats don't match
- Manual check ("Check for Component Updates" in the Help menu, or "Check
  for Updates Now" in Preferences)
- Automatic check on startup (delayed, quiet unless updates are found,
  configurable interval)
- All settings live in **Preferences → Tools → Component Update Checker**

**日本語**

- foobar2000 SDK経由で導入済みコンポーネントを検出(DLL名・表示名・バージョン)
- **Preferences → Tools → Component Update Checker → Manage Repositories...**
  からコンポーネントごとにGitHub Repositoryを登録。もしくは、既に
  [共有の既知コンポーネントDB](#remote-registry--既知コンポーネントdb)に
  載っているコンポーネントなら、登録不要でそのまま確認できる
- GitHub Releasesから最新リリース情報を取得しバージョン比較(SemVer準拠、
  `1.0.0-beta`のようなprerelease表記にも対応)。表記が読み取れない場合は
  誤判定せず「比較不能」と表示
- 手動確認(Helpメニューの「Check for Component Updates」、または
  Preferencesの「Check for Updates Now」)
- 起動時の自動確認(遅延実行、更新がある場合のみ通知、間隔は設定可能)
- 設定は**Preferences → Tools → Component Update Checker**に集約

### Explicitly out of scope / 対象外

**English**

- Auto-download, auto-install, auto-replace of component DLLs
- Verifying the safety/quality of any component
- Sending library contents, playback history, or playlists anywhere
- Component identity is based on DLL name (not GUID); the foobar2000 SDK's
  `componentversion` interface does not expose a reliable per-component GUID

**日本語**

- コンポーネントDLLの自動ダウンロード・自動インストール・自動置換
- コンポーネントの安全性・品質の保証
- 音楽ライブラリ・再生履歴・プレイリスト等の外部送信
- コンポーネントの識別はDLL名ベース(GUIDではない)。foobar2000 SDKの
  `componentversion`インターフェースからは、信頼できるコンポーネント単位の
  GUIDが取得できないため

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

**日本語**

ローカルに登録されていないコンポーネントは、
[foo_component_update_checker-registry](https://github.com/p2ashiura/foo_component_update_checker-registry)
で管理されている共有の静的JSONファイルを参照する(`raw.githubusercontent.com`から
匿名取得。APIキーやサーバーは不要)。**Manage Repositories...**でローカルに
登録した内容は、常にこの共有DBより優先される。キャッシュは最大24時間に1回まで
更新を試み、失敗時は直近の取得済みキャッシュへ静かにフォールバックする。

## Requirements / 動作環境

- foobar2000 v2.x, 64-bit
- Windows 11

## Building / ビルド方法

**English**

1. Clone this repository.
2. Place the `SDK-2025-03-07` (foobar2000 SDK + Columns UI SDK + pfc/shared)
   and `WTL10_01_Release` folders alongside `foo_component_update_checker.vcxproj`.
   These are not committed to this repository — obtain them separately.
3. Open `foo_component_update_checker.slnx` in Visual Studio 2022 and build
   the `Debug|x64` or `Release|x64` configuration.

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
   `Debug|x64`または`Release|x64`構成でビルドする。

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
3. For anything not covered, click **Manage Repositories...** to associate
   an installed component with its GitHub repository (owner/repo).
4. Adjust **Automatically check for updates** and **Check interval (days)**
   as needed.

**日本語**

1. **Preferences → Tools → Component Update Checker**を開く
2. **Check for Updates Now**をクリックする —
   [共有DB](#remote-registry--既知コンポーネントdb)に既に載っている
   コンポーネントは、登録不要でそのまま確認される
3. カバーされていないコンポーネントは、**Manage Repositories...**から
   GitHubリポジトリ(owner/repo)を紐付ける
4. 必要に応じて**Automatically check for updates**と**Check interval
   (days)**を調整する

## Docs / 関連文書

See [`docs/dev_log.md`](docs/dev_log.md) for development history, design
decisions, and rationale behind them.

開発の経緯・設計判断とその理由については[`docs/dev_log.md`](docs/dev_log.md)を参照。

## License / ライセンス

TBD (this repository is currently private).
未定(現在は非公開リポジトリ)。
