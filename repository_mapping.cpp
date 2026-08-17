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

} // namespace

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
