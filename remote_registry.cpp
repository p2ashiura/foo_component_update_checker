#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"
#include "third_party/nlohmann/json.hpp"
#include "remote_registry.h"

#include <algorithm>
#include <cctype>
#include <ctime>

namespace {

// 移行のしやすさのため、参照先はここ1箇所にまとめる(owner/repo/pathをheader経由で
// 他ファイル(登録ダイアログのPR誘導ボタン等)からも参照する)。
// データベース部分だけを独立したpublicリポジトリ(foo_component_update_checker-registry)
// に分離してある。コード本体(foo_component_update_checker)はprivateのままでよい。
const std::string k_remoteRegistryUrl =
    std::string("https://raw.githubusercontent.com/") + k_registryRepoOwner + "/" + k_registryRepoName + "/main/" + k_registryFilePath;

// 静的ファイルなので高頻度に取りに行く必要はない。既定24時間。
const int64_t k_refreshIntervalSeconds = 24 * 60 * 60;

// {A308A585-999F-4EA8-B104-B220D9465125}
const GUID guid_cfg_remote_registry_cache =
{ 0xa308a585, 0x999f, 0x4ea8, { 0xb1, 0x04, 0xb2, 0x20, 0xd9, 0x46, 0x51, 0x25 } };

// {BD0271B2-ADD6-4597-AC9D-9271C5A9969D}
const GUID guid_cfg_remote_registry_last_fetch =
{ 0xbd0271b2, 0xadd6, 0x4597, { 0xac, 0x9d, 0x92, 0x71, 0xc5, 0xa9, 0x96, 0x9d } };

// 取得できた生のJSONテキストをそのままキャッシュする(自前の構造体へ
// 変換してから保存するより単純で、スキーマ変更にも強い)。
static cfg_string g_cfgRemoteRegistryCache(guid_cfg_remote_registry_cache, "{\"schema_version\":1,\"components\":[]}");
static cfg_int g_cfgRemoteRegistryLastFetch(guid_cfg_remote_registry_last_fetch, 0);

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string stripDllExtension(std::string s) {
    const std::string ext = ".dll";
    if (s.size() >= ext.size()) {
        std::string tail = toLowerCopy(s.substr(s.size() - ext.size()));
        if (tail == ext) return s.substr(0, s.size() - ext.size());
    }
    return s;
}

std::string normalizeDllName(std::string s) {
    return toLowerCopy(stripDllExtension(s));
}

bool shouldRefetch() {
    int64_t lastFetch = static_cast<int64_t>(g_cfgRemoteRegistryLastFetch.get());
    if (lastFetch <= 0) return true;

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    return (now - lastFetch) >= k_refreshIntervalSeconds;
}

void markFetchAttemptedNow() {
    g_cfgRemoteRegistryLastFetch = static_cast<t_int32>(std::time(nullptr));
}

// 取得したJSONが最低限のスキーマ(schema_versionとcomponents配列を持つ)を
// 満たしているかだけ確認する。中身の詳細な検証はパース時に個別に行う。
bool looksLikeValidRegistryJson(nlohmann::json const& parsed) {
    return parsed.is_object() && parsed.contains("components") && parsed["components"].is_array();
}

void tryRefreshCache(abort_callback& abort, RemoteRegistryFetchStatus* outStatus) {
    if (!shouldRefetch()) return; // outStatus->attempted は false のまま(既定値)

    if (outStatus) outStatus->attempted = true;

    try {
        http_client::ptr client = http_client::get();
        http_request::ptr request = client->create_request("GET");
        request->add_header("User-Agent", "foo_component_update_checker/1.2.0");

        file::ptr response = request->run(k_remoteRegistryUrl.c_str(), abort);

        pfc::string8 body;
        response->read_string_raw(body, abort);

        nlohmann::json parsed = nlohmann::json::parse(body.c_str());
        if (looksLikeValidRegistryJson(parsed)) {
            g_cfgRemoteRegistryCache = body.c_str();
            if (outStatus) outStatus->succeeded = true;
        } else {
            // スキーマを満たさない場合は、古いキャッシュを保持したまま何もしない
            // (Fail Open: 破損データで既存の動作を壊さない)。
            if (outStatus) {
                outStatus->succeeded = false;
                outStatus->errorMessage = "Invalid response: the registry file does not match the expected format.";
            }
        }
    } catch (std::exception const& e) {
        // 通信失敗・JSON解析失敗のいずれも、古いキャッシュへフォールバックする。
        // ここで例外を外へ伝播させないが、原因は呼び出し元に伝える。
        if (outStatus) {
            outStatus->succeeded = false;
            outStatus->errorMessage = std::string("Network/API error: ") + e.what();
        }
    }

    // 成功・失敗にかかわらず試行した時刻は記録する(Bounded Work: 通信不調時に
    // 毎回リトライが集中するのを防ぐ。automatic_check.cppと同じ方針)。
    markFetchAttemptedNow();
}

} // namespace

std::vector<RemoteRegistryEntry> GetRemoteRegistryEntries(abort_callback& abort, RemoteRegistryFetchStatus* outStatus) {
    if (outStatus) *outStatus = RemoteRegistryFetchStatus{};

    tryRefreshCache(abort, outStatus);

    std::vector<RemoteRegistryEntry> result;

    try {
        pfc::string8 raw = g_cfgRemoteRegistryCache.get();
        nlohmann::json parsed = nlohmann::json::parse(raw.c_str());

        if (!looksLikeValidRegistryJson(parsed)) return result;

        for (auto const& item : parsed["components"]) {
            RemoteRegistryEntry entry;
            entry.dllName = item.value("dll", "");
            entry.source = item.value("source", "");
            entry.owner = item.value("owner", "");
            entry.repo = item.value("repo", "");
            entry.url = item.value("url", "");

            if (!entry.dllName.empty() && !entry.source.empty()) {
                result.push_back(std::move(entry));
            }
        }
    } catch (std::exception const& e) {
        // DP-0006 Fail Open: キャッシュ自体が壊れていても空のリストとして扱う。
        // 直前のfetchが成功していたのにここでパースが壊れるのは通常起きないはずだが、
        // 念のためこちらもエラーとして伝える。
        result.clear();
        if (outStatus && (!outStatus->attempted || outStatus->succeeded)) {
            outStatus->succeeded = false;
            outStatus->errorMessage = std::string("Invalid response: cached registry data could not be parsed (") + e.what() + ").";
        }
    }

    return result;
}

bool findRemoteRegistryEntry(
    std::vector<RemoteRegistryEntry> const& entries,
    std::string const& dllName,
    RemoteRegistryEntry& out
) {
    std::string target = normalizeDllName(dllName);

    for (auto const& e : entries) {
        // 対応済みのsource("github"/"gitlab"/"codeberg"/"marc2k3"/"sourceforge")
        // のみ利用可能とみなす。未対応のsource(将来追加予定のサイトが登録された
        // 場合等)は静かにスキップする(Fail Open)。
        if (e.source != "github" && e.source != "gitlab" && e.source != "codeberg" && e.source != "marc2k3" && e.source != "sourceforge") continue;
        if (normalizeDllName(e.dllName) == target) {
            out = e;
            return true;
        }
    }

    return false;
}
