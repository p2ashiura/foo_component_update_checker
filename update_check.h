#pragma once

#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"

#include <vector>
#include <string>

// Update Checkのコアロジック。手動確認(mainmenu_commands_check_for_updates)と
// 自動確認(automatic_check.cpp)の両方から共用する。

struct InstalledComponentInfo {
    std::string fileName;       // コンポーネントはDLL名。パッケージはパッケージのid(GUID)。
    std::string displayName;
    std::string installedVersion;
    // SMP/JSplitterのパッケージなど、自身が更新元のURLを持つ導入物用。
    // 空でなければ、Repository MappingやRemote Registryを経由せず、
    // このURLをそのまま取得元として使う(通常のコンポーネントでは空)。
    std::string sourceUrl;
};

enum class UpdateStatus {
    UpdateAvailable,
    UpToDate,
    ComparisonUnsupported,
    Error,
};

struct CheckResult {
    std::string dllName;
    std::string displayName;
    std::string installedVersion;
    std::string latestVersion;
    std::string releaseUrl;
    UpdateStatus status = UpdateStatus::Error;
    std::string errorMessage;
};

// 導入済みコンポーネント一覧を取得する。あわせて、SMP/JSplitterのパッケージのうち
// package.jsonに"source"(更新元URL)を持つものも一覧に含める(sourceUrlに格納)。
// componentversion::enumerate() を呼ぶため、メインスレッドで実行すること。
std::vector<InstalledComponentInfo> GetInstalledComponents();

// Repository Mappingに登録されているコンポーネントだけを対象に、
// GitHub Releases APIへの問い合わせとバージョン比較を行う。
// ネットワークI/Oを含むため、ワーカースレッドで実行すること。
std::vector<CheckResult> RunUpdateCheck(
    std::vector<InstalledComponentInfo> const& installed,
    abort_callback& abort
);

// resultsの中に「更新あり」が1件でもあるかを返す。
bool HasUpdateAvailable(std::vector<CheckResult> const& results);

// resultsの中にエラー(通信失敗・解析失敗・Remote Registry取得失敗等)が
// 1件でもあるかを返す。自動確認の通知レベル判定に使う。
bool HasAnyError(std::vector<CheckResult> const& results);

// 結果セットを1回のconsole::printでまとめて表示する(DP-0025)。
// emptyMessage: 該当コンポーネントが1件も無かった場合に表示するメッセージ。
void PrintUpdateCheckResults(std::vector<CheckResult> const& results, const char* emptyMessage);
