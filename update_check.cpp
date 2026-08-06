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
// 数字とドットだけで構成されるバージョン文字列(先頭のv/Vは許容)のみ対応する。
bool tryNormalizeVersion(std::string raw, std::vector<long long>& outParts) {
    std::string s = raw;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    if (s.empty()) return false;

    for (char c : s) {
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.')) return false;
    }

    outParts.clear();
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, '.')) {
        if (token.empty()) return false;
        try {
            outParts.push_back(std::stoll(token));
        } catch (...) {
            return false;
        }
    }
    return !outParts.empty();
}

UpdateStatus compareVersions(std::string const& installedRaw, std::string const& latestRaw) {
    std::vector<long long> a, b;
    if (!tryNormalizeVersion(installedRaw, a) || !tryNormalizeVersion(latestRaw, b)) {
        return UpdateStatus::ComparisonUnsupported;
    }

    size_t n = (std::max)(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        long long av = i < a.size() ? a[i] : 0;
        long long bv = i < b.size() ? b[i] : 0;
        if (av < bv) return UpdateStatus::UpdateAvailable;
        if (av > bv) return UpdateStatus::UpToDate;
    }
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
