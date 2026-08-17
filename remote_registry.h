#pragma once

#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"

#include <string>
#include <vector>

// Remote Registry: 開発側が管理する「主要コンポーネントのDLL名 <-> GitHub
// リポジトリ」対応表を、GitHub上の静的JSONファイルから取得して参照する。
//
// - サーバーは使わず、raw.githubusercontent.com上の静的ファイルを読むだけ
//   (GitHub APIのレート制限を消費しない)
// - ユーザーがManage Repositories...で明示的に登録した内容が常に最優先。
//   Remote Registryは、そこに無いコンポーネントを補完する位置づけ(DP-0018)
// - オフライン時・取得失敗時は、直近のキャッシュ(なければ空)にフォールバックし、
//   確認処理自体は止めない(Fail Open)
//
// "source"フィールドは将来GitHub以外(GitLab、個人サイト等)にも対応できるよう
// 持たせてある。現時点では "github" / "gitlab" / "codeberg" / "marc2k3" /
// "sourceforge" / "hyv" に対応。

// Registryリポジトリの場所。移行時はここだけ書き換えればよい。
// remote_registry.cpp(取得元URLの組み立て)と、登録ダイアログのPR誘導ボタン
// (repository_mapping_ui.cpp)の両方から参照する。
inline const char* const k_registryRepoOwner = "p2ashiura";
inline const char* const k_registryRepoName = "foo_component_update_checker-registry";
inline const char* const k_registryFilePath = "known_components.json";

struct RemoteRegistryEntry {
    std::string dllName;
    std::string source; // "github" / "gitlab" / "codeberg" / "marc2k3" / "sourceforge" / "hyv"
    std::string owner;  // owner/repoモデル用。marc2k3では未使用(空文字)
    std::string repo;   // owner/repoモデル用。marc2k3では未使用(空文字)
    std::string url;    // urlモデル(marc2k3)用。他sourceでは未使用(空文字)
};

// GetRemoteRegistryEntries()呼び出し時に、実際に取得を試みたか・成功したかを
// 呼び出し元へ伝えるための構造体。24時間の間隔内で再取得しなかった場合は
// attempted=falseのままになる(それ自体はエラーではない)。
struct RemoteRegistryFetchStatus {
    bool attempted = false;
    bool succeeded = false;
    std::string errorMessage;
};

// Remote Registryのエントリ一覧を取得する。
// 必要に応じてGitHub上の最新版を取得しに行く(ネットワークI/Oを含むため、
// ワーカースレッドで呼ぶこと)。取得できない場合は直近のキャッシュを返す。
// outStatusを渡すと、取得を試みたか・成功したかが分かる(通知の要否判定に使う)。
std::vector<RemoteRegistryEntry> GetRemoteRegistryEntries(abort_callback& abort, RemoteRegistryFetchStatus* outStatus = nullptr);

// dllNameで検索する(拡張子の有無・大文字小文字は無視)。
// 対応済みのsource("github"/"gitlab"/"codeberg"/"marc2k3"/"sourceforge"/"hyv")の
// エントリのみ現時点では利用可能とみなす。
bool findRemoteRegistryEntry(
    std::vector<RemoteRegistryEntry> const& entries,
    std::string const& dllName,
    RemoteRegistryEntry& out
);
