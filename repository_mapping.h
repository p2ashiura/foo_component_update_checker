#pragma once

#include <string>
#include <vector>

// Repository Mapping: どのコンポーネント(DLL名)をどのGitHub Repositoryと
// 紐付けるか、を管理するモジュール。
//
// 永続化にはfoobar2000 SDKのcfg_string(cfg_var)を使う。
// - Portable Mode / 通常Modeの保存先の違いをfoobar2000側が自動で吸収してくれる
// - 書き込みのアトミック性・文字コードの扱いを自前実装する必要がない
// 中身はJSON配列の文字列として1つのcfg_stringにまとめて保存する。

struct RepositoryMappingEntry {
    std::string dllName;  // 拡張子(.dll)の有無は問わない。比較時に正規化する。
    std::string source;   // "github" / "gitlab" / "codeberg" / "marc2k3" / "sourceforge"。
                           // 未設定時は"github"扱い(既存保存データとの後方互換のため)。
    std::string owner;    // owner/repoモデル(github/gitlab/codeberg)用。
                           // marc2k3では未使用(空文字)。
    std::string repo;     // owner/repoモデル(github/gitlab/codeberg)用。
                           // marc2k3では未使用(空文字)。
    std::string url;      // urlモデル(marc2k3)用。コンポーネント個別ページのURLを
                           // そのまま保持する。github/gitlab/codebergでは未使用(空文字)。
};

// 保存されている全エントリを読み込む。
// 壊れている・未初期化の場合は空のvectorを返す(DP-0006 Fail Open)。
std::vector<RepositoryMappingEntry> loadRepositoryMapping();

// 全エントリを保存する(既存の内容は置き換え)。
void saveRepositoryMapping(std::vector<RepositoryMappingEntry> const& entries);

// dllNameで検索する(拡張子の有無・大文字小文字は無視)。
// 見つかった場合はtrueを返しoutに格納する。
bool findRepositoryMapping(
    std::vector<RepositoryMappingEntry> const& entries,
    std::string const& dllName,
    RepositoryMappingEntry& out
);

// 指定したdllNameのエントリを追加(無ければ)または更新(あれば)して保存する。
// Phase 1ではまだ登録用UIが無いため、暫定的にこの関数をDebugコマンドから呼んで
// 動作確認する。将来的には設定ダイアログから呼ばれる想定。
void upsertRepositoryMappingEntry(RepositoryMappingEntry const& entry);

// 指定したdllNameのエントリを削除して保存する(見つからなければ何もしない)。
void removeRepositoryMappingEntry(std::string const& dllName);
