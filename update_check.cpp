#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"
#include "third_party/nlohmann/json.hpp"
#include "repository_mapping.h"
#include "update_check.h"

#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <sstream>

// ------------------------------------------------------------------
// Phase 1: 更新確認のコアロジック(手動確認・自動確認で共用)
//
// 流れ:
//   1. componentversion::enumerate() で導入済みコンポーネントを取得(メインスレッド)
//   2. Repository Mapping(cfg_stringに永続化。repository_mapping.cpp参照)と突き合わせ
//   3. マッチしたものだけワーカースレッドでGitHub Releases APIへ問い合わせ
//   4. バージョン比較(数値のみ対応。それ以外は「比較不能」として誤判定を避ける)
//   5. 結果をまとめて1回のログ出力で表示
//
// Repository Mappingへの登録は「Manage Component Repositories...」から行う。
// ------------------------------------------------------------------

namespace {

// DP-0009 / DP-0010: 誤判定するくらいなら「比較不能」を返す。
//
// 対応する形式(SemVer準拠。先頭のv/Vは許容):
//   MAJOR.MINOR.PATCH[.MORE...][-prerelease][+build]
// 例: "4.2.0", "v1.5.2", "1.0.0-beta", "2.0.0-rc.1", "1.2.3+20240101"
//
// - ビルドメタデータ(+以降)は優先順位に影響しないため無視する(SemVer仕様通り)
// - prerelease同士の比較はSemVer 2.0.0の優先順位ルールに従う:
//     * ドット区切りの識別子ごとに比較する
//     * 数字のみの識別子は数値として比較する
//     * 数字のみの識別子は、英数字混在の識別子より必ず「小さい」とみなす
//     * それ以外(英字を含む)は文字コード順(ASCII)で比較する
//     * すべて一致した場合、識別子の数が少ない方を「小さい」とみなす
// - この形式に当てはまらないバージョン文字列は「比較不能」として扱う
struct ParsedVersion {
    std::vector<long long> core;
    std::vector<std::string> prerelease; // 空 = 正式リリース(prereleaseなし)
    bool hasPrerelease = false;
};

bool isValidPrereleaseIdentifier(std::string const& id) {
    if (id.empty()) return false;
    for (char c : id) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-')) return false;
    }
    return true;
}

bool isNumericIdentifier(std::string const& id) {
    if (id.empty()) return false;
    for (char c : id) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

bool tryParseVersion(std::string raw, ParsedVersion& out) {
    std::string s = raw;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    if (s.empty()) return false;

    // ビルドメタデータ(+以降)は優先順位に無関係なので切り落とす
    size_t plusPos = s.find('+');
    if (plusPos != std::string::npos) {
        s = s.substr(0, plusPos);
    }
    if (s.empty()) return false;

    // prerelease(-以降)を切り出す
    std::string corePart = s;
    std::string prereleasePart;
    bool hasPrerelease = false;

    size_t dashPos = s.find('-');
    if (dashPos != std::string::npos) {
        corePart = s.substr(0, dashPos);
        prereleasePart = s.substr(dashPos + 1);
        hasPrerelease = true;
    }

    // コア部分(数字とドットのみ)を解析
    if (corePart.empty()) return false;
    for (char c : corePart) {
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.')) return false;
    }

    std::vector<long long> core;
    {
        std::stringstream ss(corePart);
        std::string token;
        while (std::getline(ss, token, '.')) {
            if (token.empty()) return false;
            try {
                core.push_back(std::stoll(token));
            } catch (...) {
                return false;
            }
        }
    }
    if (core.empty()) return false;

    // prerelease部分(あれば)を解析
    std::vector<std::string> prerelease;
    if (hasPrerelease) {
        if (prereleasePart.empty()) return false; // "1.2.3-" のような不完全な形式は不可

        std::stringstream ss(prereleasePart);
        std::string token;
        while (std::getline(ss, token, '.')) {
            if (!isValidPrereleaseIdentifier(token)) return false;
            prerelease.push_back(token);
        }
        if (prerelease.empty()) return false;
    }

    out.core = std::move(core);
    out.prerelease = std::move(prerelease);
    out.hasPrerelease = hasPrerelease;
    return true;
}

// SemVer 2.0.0の優先順位ルールでprerelease識別子リストを比較する。
// 戻り値: a<b なら負、a>b なら正、等しければ0。
int comparePrereleaseIdentifiers(std::vector<std::string> const& a, std::vector<std::string> const& b) {
    size_t n = (std::min)(a.size(), b.size());

    for (size_t i = 0; i < n; ++i) {
        std::string const& ida = a[i];
        std::string const& idb = b[i];

        bool numA = isNumericIdentifier(ida);
        bool numB = isNumericIdentifier(idb);

        if (numA && numB) {
            long long va, vb;
            try {
                va = std::stoll(ida);
                vb = std::stoll(idb);
            } catch (...) {
                // 極端に長い数字列などで変換に失敗した場合は文字列比較にフォールバック
                if (ida != idb) return ida < idb ? -1 : 1;
                continue;
            }
            if (va != vb) return va < vb ? -1 : 1;
            continue;
        }

        if (numA != numB) {
            // 数字のみの識別子は、英数字混在の識別子より必ず小さい(SemVer仕様)
            return numA ? -1 : 1;
        }

        if (ida != idb) return ida < idb ? -1 : 1;
    }

    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return 0;
}

UpdateStatus compareVersions(std::string const& installedRaw, std::string const& latestRaw) {
    ParsedVersion a, b;
    if (!tryParseVersion(installedRaw, a) || !tryParseVersion(latestRaw, b)) {
        return UpdateStatus::ComparisonUnsupported;
    }

    // 1. コア部分(Major.Minor.Patch...)を比較
    size_t n = (std::max)(a.core.size(), b.core.size());
    for (size_t i = 0; i < n; ++i) {
        long long av = i < a.core.size() ? a.core[i] : 0;
        long long bv = i < b.core.size() ? b.core[i] : 0;
        if (av < bv) return UpdateStatus::UpdateAvailable;
        if (av > bv) return UpdateStatus::UpToDate;
    }

    // 2. コアが同じ場合、prereleaseの有無・内容で比較する(SemVer仕様)
    //    正式リリースは、同じコアのどのprereleaseよりも優先順位が高い
    if (!a.hasPrerelease && !b.hasPrerelease) return UpdateStatus::UpToDate;
    if (a.hasPrerelease && !b.hasPrerelease) return UpdateStatus::UpdateAvailable; // 導入版がprerelease、最新は正式版
    if (!a.hasPrerelease && b.hasPrerelease) return UpdateStatus::UpToDate;        // 導入版が既に正式版、最新はprerelease

    int cmp = comparePrereleaseIdentifiers(a.prerelease, b.prerelease);
    if (cmp < 0) return UpdateStatus::UpdateAvailable;
    return UpdateStatus::UpToDate;
}

} // namespace

std::vector<InstalledComponentInfo> GetInstalledComponents() {
    std::vector<InstalledComponentInfo> installed;

    for (auto ptr : componentversion::enumerate()) {
        pfc::string8 fileName, name, version;
        ptr->get_file_name(fileName);
        ptr->get_component_name(name);
        ptr->get_component_version(version);

        InstalledComponentInfo info;
        info.fileName = fileName.c_str();
        info.displayName = name.c_str();
        info.installedVersion = version.c_str();
        installed.push_back(std::move(info));
    }

    return installed;
}

std::vector<CheckResult> RunUpdateCheck(
    std::vector<InstalledComponentInfo> const& installed,
    abort_callback& abort
) {
    std::vector<CheckResult> results;

    // Repository Mappingはcfg_stringに保存されているが、cfg_varはメインスレッド
    // 前提のAPIではないため、ワーカースレッドで読み込んでも問題ない。
    std::vector<RepositoryMappingEntry> mappingEntries = loadRepositoryMapping();

    for (auto const& comp : installed) {
        RepositoryMappingEntry mapping;
        if (!findRepositoryMapping(mappingEntries, comp.fileName, mapping)) {
            continue; // Phase 1: 未登録は静かにスキップ
        }

        CheckResult r;
        r.dllName = comp.fileName;
        r.displayName = comp.displayName;
        r.installedVersion = comp.installedVersion;

        try {
            pfc::string8 url = "https://api.github.com/repos/";
            url << mapping.owner.c_str() << "/" << mapping.repo.c_str() << "/releases/latest";

            http_client::ptr client = http_client::get();
            http_request::ptr request = client->create_request("GET");
            request->add_header("User-Agent", "foo_component_update_checker/0.1.0");
            request->add_header("Accept", "application/vnd.github+json");

            file::ptr response = request->run(url, abort);

            pfc::string8 body;
            response->read_string_raw(body, abort);

            nlohmann::json parsed = nlohmann::json::parse(body.c_str());

            r.latestVersion = parsed.value("tag_name", "");
            r.releaseUrl = parsed.value("html_url", "");
            r.status = compareVersions(r.installedVersion, r.latestVersion);
        } catch (std::exception const& e) {
            r.status = UpdateStatus::Error;
            r.errorMessage = e.what();
        }

        results.push_back(std::move(r));
    }

    return results;
}

bool HasUpdateAvailable(std::vector<CheckResult> const& results) {
    for (auto const& r : results) {
        if (r.status == UpdateStatus::UpdateAvailable) return true;
    }
    return false;
}

void PrintUpdateCheckResults(std::vector<CheckResult> const& results, const char* emptyMessage) {
    if (results.empty()) {
        console::print(emptyMessage);
        return;
    }

    pfc::string8 msg = "Update Check: result set (";
    msg << static_cast<int>(results.size()) << " item(s))\n";

    for (auto const& r : results) {
        msg << "--------------------------------\n";
        msg << r.displayName.c_str() << " [" << r.dllName.c_str() << "]\n";
        msg << "  Installed: " << r.installedVersion.c_str() << "\n";

        switch (r.status) {
        case UpdateStatus::UpdateAvailable:
            msg << "  Latest:    " << r.latestVersion.c_str() << "  -> Update available\n";
            msg << "  Release:   " << r.releaseUrl.c_str() << "\n";
            break;
        case UpdateStatus::UpToDate:
            msg << "  Latest:    " << r.latestVersion.c_str() << "  -> Up to date\n";
            break;
        case UpdateStatus::ComparisonUnsupported:
            msg << "  Latest:    " << r.latestVersion.c_str() << "  -> Unable to compare versions\n";
            msg << "  Release:   " << r.releaseUrl.c_str() << "\n";
            break;
        case UpdateStatus::Error:
            msg << "  Error:     " << r.errorMessage.c_str() << "\n";
            break;
        }
    }

    console::print(msg);
}

namespace {

// {5E9A1C4D-2B6F-4E7A-8D3C-6F1A9B4E2D50}
const GUID guid_mainmenu_check_for_updates =
{ 0x5e9a1c4d, 0x2b6f, 0x4e7a, { 0x8d, 0x3c, 0x6f, 0x1a, 0x9b, 0x4e, 0x2d, 0x50 } };

class mainmenu_commands_check_for_updates : public mainmenu_commands {
public:
    t_uint32 get_command_count() override {
        return 1;
    }

    GUID get_command(t_uint32 p_index) override {
        return guid_mainmenu_check_for_updates;
    }

    void get_name(t_uint32 p_index, pfc::string_base& p_out) override {
        p_out = "Check for Component Updates";
    }

    bool get_description(t_uint32 p_index, pfc::string_base& p_out) override {
        p_out = "Checks installed components against their registered GitHub repositories for updates.";
        return true;
    }

    GUID get_parent() override {
        return mainmenu_groups::help;
    }

    void execute(t_uint32 p_index, service_ptr_t<service_base> p_callback) override {
        console::print("Update Check: scanning installed components...");

        std::vector<InstalledComponentInfo> installed = GetInstalledComponents();

        console::print("Update Check: querying registered repositories on worker thread...");

        fb2k::inCpuWorkerThread([installed] {
            abort_callback& abort = fb2k::mainAborter();
            std::vector<CheckResult> results = RunUpdateCheck(installed, abort);

            fb2k::inMainThread([results] {
                PrintUpdateCheckResults(
                    results,
                    "Update Check: no installed components matched a registered repository. "
                    "(Use \"Manage Component Repositories...\" to register one.)"
                );
            });
        });
    }
};

static service_factory_single_t<mainmenu_commands_check_for_updates> g_mainmenu_commands_check_for_updates_factory;

} // namespace
