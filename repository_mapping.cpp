#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"
#include "third_party/nlohmann/json.hpp"
#include "repository_mapping.h"

#include <algorithm>
#include <cctype>

namespace {

// {05770E5C-08C2-44C5-B709-01D2B5BF4B61}
const GUID guid_cfg_repository_mapping =
{ 0x05770e5c, 0x08c2, 0x44c5, { 0xb7, 0x09, 0x01, 0xd2, 0xb5, 0xbf, 0x4b, 0x61 } };

// 初期値は空のJSON配列。
cfg_string g_cfgRepositoryMapping(guid_cfg_repository_mapping, "[]");

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// componentversion::get_file_name() は拡張子(.dll)を含まない形で返ってくることが
// 実機確認で分かっている。ユーザーがどちらの表記で登録しても一致するよう吸収する。
std::string stripDllExtension(std::string s) {
    const std::string ext = ".dll";
    if (s.size() >= ext.size()) {
        std::string tail = toLowerCopy(s.substr(s.size() - ext.size()));
        if (tail == ext) {
            return s.substr(0, s.size() - ext.size());
        }
    }
    return s;
}

std::string normalizeDllName(std::string s) {
    return toLowerCopy(stripDllExtension(s));
}

// sourceに応じて必須フィールドが異なる(owner/repoモデル vs urlモデル)ため、
// ここで出し分ける。DP-0009: 中途半端なエントリは誤判定の元になるので、
// 必須フィールドが欠けているものは読み込まずに捨てる。
bool isValidEntry(RepositoryMappingEntry const& entry) {
    if (entry.dllName.empty()) return false;

    if (entry.source == "marc2k3" || entry.source == "sourceforge" || entry.source == "hyv") {
        return !entry.url.empty();
    }

    // github / gitlab / codeberg(および後方互換の未設定時)は owner/repo モデル
    return !entry.owner.empty() && !entry.repo.empty();
}

std::string TrimWhitespace(std::string s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

} // namespace

// リポジトリURL(様々な表記ゆれ)から source を判定し、
//   - owner/repoモデル(github/gitlab/codeberg) の場合は outOwner/outRepo
//   - urlモデル(marc2k3) の場合は outUrl
// を埋める。対応例:
//   https://github.com/owner/repo
//   https://gitlab.com/owner/repo
//   https://codeberg.org/owner/repo
//   https://marc2k3.github.io/component/xxx/
//   (github/gitlab/codebergはhttp/https有無、末尾に/releases等が付いていても、
//    .gitサフィックスが付いていても可)
//
// 現時点でこれらのいずれとも解釈できない入力は、誤登録を避けるため
// 素直に失敗を返す(DP-0009: 誤判定するくらいなら明示的にエラーにする)。
// GitLabのグループ/サブグループ(owner部分に複数階層のパスを持つ場合)は
// 現時点では未対応。
bool TryParseRepositoryUrl(
    std::string url, std::string& outSource, std::string& outOwner,
    std::string& outRepo, std::string& outUrl
) {
    url = TrimWhitespace(url);
    if (url.empty()) return false;

    // marc2k3.github.io: owner/repoモデルではなく、コンポーネント個別ページの
    // URLをそのまま識別子として保持する特別扱い。
    if (url.find("marc2k3.github.io/") != std::string::npos) {
        std::string normalized = url;

        // クエリ文字列・フラグメントを切り落とす
        size_t cutPos = normalized.find_first_of("?#");
        if (cutPos != std::string::npos) normalized = normalized.substr(0, cutPos);

        // スキームが無ければ https:// を補う
        if (normalized.compare(0, 8, "https://") != 0 &&
            normalized.compare(0, 7, "http://") != 0) {
            normalized = "https://" + normalized;
        }

        outSource = "marc2k3";
        outOwner = "";
        outRepo = "";
        outUrl = normalized;
        return true;
    }

    // sourceforge.net: marc2k3と同様にurlモデル。ただしこちらはSourceForge
    // というプラットフォーム自体の共通機能(RSSフィード)を使うため、特定の
    // プロジェクトに限らず汎用的に対応できる。登録はプロジェクトのファイル
    // 一覧ページ(フォルダ)のURLで行う。
    if (url.find("sourceforge.net/projects/") != std::string::npos) {
        std::string normalized = url;

        size_t cutPos = normalized.find_first_of("?#");
        if (cutPos != std::string::npos) normalized = normalized.substr(0, cutPos);

        if (normalized.compare(0, 8, "https://") != 0 &&
            normalized.compare(0, 7, "http://") != 0) {
            normalized = "https://" + normalized;
        }

        // 末尾にスラッシュが無ければ揃えておく(フォルダページである前提のため)。
        if (normalized.back() != '/') normalized += "/";

        outSource = "sourceforge";
        outOwner = "";
        outRepo = "";
        outUrl = normalized;
        return true;
    }

    // foobar.hyv.fi: marc2k3と同様、このサイト専用のurlモデル(Case氏の
    // 個人配布サイト)。ただしクエリパラメータ(?view=<component>)自体が
    // コンポーネントの識別子なので、marc2k3/sourceforgeと違いクエリ文字列は
    // 切り落とさず残す(フラグメントのみ切り落とす)。
    if (url.find("foobar.hyv.fi/") != std::string::npos) {
        std::string normalized = url;

        size_t fragPos = normalized.find('#');
        if (fragPos != std::string::npos) normalized = normalized.substr(0, fragPos);

        if (normalized.compare(0, 8, "https://") != 0 &&
            normalized.compare(0, 7, "http://") != 0) {
            normalized = "https://" + normalized;
        }

        outSource = "hyv";
        outOwner = "";
        outRepo = "";
        outUrl = normalized;
        return true;
    }

    struct SiteMarker {
        const char* marker;
        const char* source;
    };
    static const SiteMarker sites[] = {
        { "github.com/", "github" },
        { "gitlab.com/", "gitlab" },
        { "codeberg.org/", "codeberg" },
    };

    const char* matchedSource = nullptr;
    size_t pos = std::string::npos;
    size_t markerLen = 0;

    for (auto const& site : sites) {
        size_t p = url.find(site.marker);
        if (p != std::string::npos) {
            matchedSource = site.source;
            pos = p;
            markerLen = std::string(site.marker).length();
            break;
        }
    }

    if (matchedSource == nullptr) return false;

    std::string rest = url.substr(pos + markerLen);

    // クエリ文字列・フラグメントを切り落とす
    size_t cutPos = rest.find_first_of("?#");
    if (cutPos != std::string::npos) rest = rest.substr(0, cutPos);

    // owner/repo/(残り、あれば無視) に分割
    size_t slashPos = rest.find('/');
    if (slashPos == std::string::npos) return false; // owner だけではリポジトリが特定できない

    std::string owner = rest.substr(0, slashPos);
    std::string repoRest = rest.substr(slashPos + 1);

    size_t secondSlashPos = repoRest.find('/');
    std::string repo = (secondSlashPos == std::string::npos) ? repoRest : repoRest.substr(0, secondSlashPos);

    // ".git" サフィックスを除去
    const std::string gitSuffix = ".git";
    if (repo.size() > gitSuffix.size() &&
        repo.compare(repo.size() - gitSuffix.size(), gitSuffix.size(), gitSuffix) == 0) {
        repo = repo.substr(0, repo.size() - gitSuffix.size());
    }

    if (owner.empty() || repo.empty()) return false;

    outSource = matchedSource;
    outOwner = owner;
    outRepo = repo;
    outUrl = ""; // owner/repoベースのサイトではurlは使わない
    return true;
}

std::vector<RepositoryMappingEntry> loadRepositoryMapping() {
    std::vector<RepositoryMappingEntry> result;

    try {
        pfc::string8 raw = g_cfgRepositoryMapping.get();
        nlohmann::json parsed = nlohmann::json::parse(raw.c_str());

        if (!parsed.is_array()) return result;

        for (auto const& item : parsed) {
            RepositoryMappingEntry entry;
            entry.dllName = item.value("dll", "");
            // 後方互換: v1.0.0以前に保存されたエントリにはsourceフィールドが無いため、
            // その場合は"github"として扱う。
            entry.source = item.value("source", "github");
            entry.owner = item.value("owner", "");
            entry.repo = item.value("repo", "");
            entry.url = item.value("url", "");

            if (isValidEntry(entry)) {
                result.push_back(std::move(entry));
            }
        }
    } catch (std::exception const&) {
        // DP-0006 Fail Open: 壊れていても空のマッピングとして扱い、
        // foobar2000本体やComponent Update Checkerの起動自体は継続する。
        result.clear();
    }

    return result;
}

void saveRepositoryMapping(std::vector<RepositoryMappingEntry> const& entries) {
    nlohmann::json arr = nlohmann::json::array();

    for (auto const& e : entries) {
        nlohmann::json item;
        item["dll"] = e.dllName;
        item["source"] = e.source.empty() ? "github" : e.source;
        item["owner"] = e.owner;
        item["repo"] = e.repo;
        item["url"] = e.url;
        arr.push_back(std::move(item));
    }

    g_cfgRepositoryMapping.set(arr.dump().c_str());
}

bool findRepositoryMapping(
    std::vector<RepositoryMappingEntry> const& entries,
    std::string const& dllName,
    RepositoryMappingEntry& out
) {
    std::string target = normalizeDllName(dllName);

    for (auto const& e : entries) {
        if (normalizeDllName(e.dllName) == target) {
            out = e;
            return true;
        }
    }

    return false;
}

void upsertRepositoryMappingEntry(RepositoryMappingEntry const& entry) {
    auto entries = loadRepositoryMapping();
    std::string target = normalizeDllName(entry.dllName);

    bool replaced = false;
    for (auto& e : entries) {
        if (normalizeDllName(e.dllName) == target) {
            e = entry;
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        entries.push_back(entry);
    }

    saveRepositoryMapping(entries);
}

void removeRepositoryMappingEntry(std::string const& dllName) {
    auto entries = loadRepositoryMapping();
    std::string target = normalizeDllName(dllName);

    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [&](RepositoryMappingEntry const& e) {
            return normalizeDllName(e.dllName) == target;
        }),
        entries.end()
    );

    saveRepositoryMapping(entries);
}
