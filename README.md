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

Early development (Phase 1, private repo). Not yet functional as a component.

初期開発中(Phase 1、非公開リポジトリ)。まだコンポーネントとして機能しない。

## Scope (Phase 1) / 対象範囲(Phase 1)

- Detect installed components (DLL name, GUID, display name, version)
  導入済みコンポーネントの検出(DLL名・GUID・表示名・バージョン)
- User-registered GitHub repository per component
  コンポーネントごとにユーザーがGitHub Repositoryを手動登録
- Fetch latest release info from GitHub Releases
  GitHub Releasesから最新リリース情報を取得
- Compare installed vs. latest version; show "unable to compare" when unsure
  バージョン比較。判定できない場合は「比較不能」として表示
- Manual check (Help menu) and automatic check (delayed, on startup)
  手動確認(Helpメニュー)と自動確認(起動後に遅延実行)
- Open the release page in the default browser
  リリースページを既定ブラウザで開く

### Explicitly out of scope / 対象外

- Auto-download, auto-install, auto-replace of component DLLs
  コンポーネントDLLの自動ダウンロード・自動インストール・自動置換
- Verifying the safety/quality of any component
  コンポーネントの安全性・品質の保証
- Sending library contents, playback history, or playlists anywhere
  音楽ライブラリ・再生履歴・プレイリスト等の外部送信

## Requirements / 動作環境

- foobar2000 v2.x, 64-bit
- Windows 11

## Building / ビルド方法

1. Clone this repository.
   このリポジトリをclone
2. Place the `SDK-2025-03-07` (foobar2000 SDK + Columns UI SDK + pfc/shared)
   and `WTL10_01_Release` folders alongside `foo_component_update_checker.vcxproj`.
   These are not committed to this repository — obtain them separately.
   `SDK-2025-03-07`(foobar2000 SDK / Columns UI SDK / pfc / shared)と
   `WTL10_01_Release`フォルダを`foo_component_update_checker.vcxproj`と同じ階層に配置する。
   これらはリポジトリにはコミットしていないため、別途入手すること。
3. Open `foo_component_update_checker.slnx` in Visual Studio 2022 and build
   the `Debug|x64` or `Release|x64` configuration.
   Visual Studio 2022で`foo_component_update_checker.slnx`を開き、
   `Debug|x64`または`Release|x64`構成でビルドする。

The solution builds the SDK helper libraries (`pfc`, `foobar2000_SDK`,
`foobar2000_component_client`, `shared`, `columns_ui_sdk`) as build
dependencies before linking the main component.

ソリューションは、本体をリンクする前にSDK側のヘルパーライブラリ
(`pfc`, `foobar2000_SDK`, `foobar2000_component_client`, `shared`, `columns_ui_sdk`)
をビルド依存関係としてビルドする。

## Docs / 関連文書

See [`docs/dev_log.md`](docs/dev_log.md) for development history, design
decisions, and rationale behind them.

開発の経緯・設計判断とその理由については[`docs/dev_log.md`](docs/dev_log.md)を参照。

## License / ライセンス

TBD (this repository is currently private).
未定(現在は非公開リポジトリ)。
