#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"
#include "update_check.h"
#include "automatic_check.h"
#include "result_window.h"

#include <ctime>
#include <vector>

// ------------------------------------------------------------------
// Phase 1: 自動確認
//
// - foobar2000起動完了後、一定時間待ってから確認を開始する(Playback First:
//   起動直後の負荷集中を避ける)
// - 前回確認からの経過日数が設定値未満なら何もしない(既定7日)
// - 更新確認自体はupdate_check.hのRunUpdateCheck()を手動確認と共用する
// - 自動確認は静か(Non-Intrusive): 更新が1件も無ければconsoleにも何も出さない。
//   更新が見つかった場合のみ、要約をconsoleへ出す
//   (Popup Notification等の専用UIはPhase 2以降の課題)
// - 確認を試みた時点(成功・失敗問わず)で最終確認日時を更新する。
//   これにより、通信が不調な環境で毎回の起動時にリトライが集中するのを防ぐ
//   (Bounded Work / Conservative Concurrency)
// ------------------------------------------------------------------

// cfg変数はこのファイルの外(automatic_check.h経由)からも参照されるため、
// 無名namespaceの外に置く。GUIDは引き続きこのファイル内に閉じてよい。
namespace {
// {E1A6C4B9-3F7D-4E2A-9C8B-5D1F6A4E7C90}
const GUID guid_cfg_auto_check_enabled =
{ 0xe1a6c4b9, 0x3f7d, 0x4e2a, { 0x9c, 0x8b, 0x5d, 0x1f, 0x6a, 0x4e, 0x7c, 0x90 } };

// {F2B7D5C0-4A8E-4F3B-AD9C-6E2A7B5F8D01}
const GUID guid_cfg_auto_check_interval_days =
{ 0xf2b7d5c0, 0x4a8e, 0x4f3b, { 0xad, 0x9c, 0x6e, 0x2a, 0x7b, 0x5f, 0x8d, 0x01 } };

// {A3C8E6D1-5B9F-4A4C-BE0D-7F3B8C6A9E12}
const GUID guid_cfg_auto_check_last_run =
{ 0xa3c8e6d1, 0x5b9f, 0x4a4c, { 0xbe, 0x0d, 0x7f, 0x3b, 0x8c, 0x6a, 0x9e, 0x12 } };
} // namespace

static cfg_bool g_cfgAutoCheckEnabled(guid_cfg_auto_check_enabled, true);
static cfg_int g_cfgAutoCheckIntervalDays(guid_cfg_auto_check_interval_days, 7);
static cfg_int g_cfgAutoCheckLastRun(guid_cfg_auto_check_last_run, 0); // UNIXエポック秒。0 = 未実施。

bool GetAutomaticCheckEnabled() {
    return g_cfgAutoCheckEnabled.get();
}

void SetAutomaticCheckEnabled(bool enabled) {
    g_cfgAutoCheckEnabled = enabled;
}

int GetAutomaticCheckIntervalDays() {
    return static_cast<int>(g_cfgAutoCheckIntervalDays.get());
}

void SetAutomaticCheckIntervalDays(int days) {
    if (days < 1) days = 1; // Safe Defaults: 0以下は事故のもとなので下限を設ける
    g_cfgAutoCheckIntervalDays = static_cast<t_int32>(days);
}

namespace {

// 起動直後の負荷集中を避けるための固定待機時間。
// Phase 1では設定不可の固定値とする(将来Expert Optionとして開放可能)。
const int k_startupDelaySeconds = 20;

bool shouldRunAutomaticCheckNow() {
    if (!g_cfgAutoCheckEnabled.get()) return false;

    // 注意: cfg_var_legacy版のcfg_intは32bit整数(t_int32)なので、
    // ここでのUNIXエポック秒は2038年問題の影響を受ける。
    // Phase 1の時間軸では実用上問題ないため、現時点では許容する。
    int64_t lastRun = static_cast<int64_t>(g_cfgAutoCheckLastRun.get());
    if (lastRun <= 0) return true; // 未実施なら実行する

    int64_t intervalSeconds = static_cast<int64_t>(g_cfgAutoCheckIntervalDays.get()) * 24 * 60 * 60;
    int64_t now = static_cast<int64_t>(std::time(nullptr));

    return (now - lastRun) >= intervalSeconds;
}

void markAutomaticCheckRanNow() {
    // cfg_var_legacy版のcfg_int_tには .set() が無く、代入演算子(operator=)のみ
    // 提供されている(cfg_var_modern版は両方使えるが、legacy互換のためこちらに統一する)。
    g_cfgAutoCheckLastRun = static_cast<t_int32>(std::time(nullptr));
}

void runAutomaticCheck() {
    if (!shouldRunAutomaticCheckNow()) return;

    // コンポーネント一覧の取得はSDK呼び出しのためメインスレッドで行う必要がある。
    // このタイミングでは既にfb2k::inCpuWorkerThread内(ワーカースレッド)なので、
    // 一度メインスレッドへ戻ってから取得し、その結果を再度ワーカースレッドへ渡す。
    fb2k::inMainThread([] {
        std::vector<InstalledComponentInfo> installed = GetInstalledComponents();

        fb2k::inCpuWorkerThread([installed] {
            abort_callback& abort = fb2k::mainAborter();
            std::vector<CheckResult> results = RunUpdateCheck(installed, abort);

            // Non-Intrusive: 通信失敗や比較不能はここでは通知しない。
            // 更新が実際に見つかった場合のみ、ポップアップウィンドウで知らせる。
            if (HasUpdateAvailable(results)) {
                fb2k::inMainThread([results] {
                    ShowUpdateResultWindow(core_api::get_main_window(), results, "");
                });
            }

            fb2k::inMainThread([] {
                markAutomaticCheckRanNow();
            });
        });
    });
}

class initquit_automatic_check : public initquit {
public:
    void on_init() override {
        // Playback First: 起動直後の負荷集中を避けるため、一定時間待ってから開始する。
        // Sleepはワーカースレッド上で行うため、メインスレッド(起動処理)は妨げない。
        fb2k::inCpuWorkerThread([] {
            Sleep(k_startupDelaySeconds * 1000);
            runAutomaticCheck();
        });
    }
};

static initquit_factory_t<initquit_automatic_check> g_initquit_automatic_check_factory;


// ------------------------------------------------------------------
// 動作確認・トラブルシューティング用: 7日間隔の判定を無視して
// 自動確認と同じ処理(検出→紐付け→GitHub確認→比較→静かな通知)を
// 即座に実行する。今後も「自動確認が本当に動いているか」の確認に使える。
// ------------------------------------------------------------------

// {B6D2E8F4-9A5C-4E1D-8B6F-3C7A9D2E5F80}
const GUID guid_mainmenu_force_automatic_check =
{ 0xb6d2e8f4, 0x9a5c, 0x4e1d, { 0x8b, 0x6f, 0x3c, 0x7a, 0x9d, 0x2e, 0x5f, 0x80 } };

class mainmenu_commands_force_automatic_check : public mainmenu_commands {
public:
    t_uint32 get_command_count() override {
        return 1;
    }

    GUID get_command(t_uint32 p_index) override {
        return guid_mainmenu_force_automatic_check;
    }

    void get_name(t_uint32 p_index, pfc::string_base& p_out) override {
        p_out = "Force Automatic Check (Debug)";
    }

    bool get_description(t_uint32 p_index, pfc::string_base& p_out) override {
        p_out = "Diagnostic: runs the automatic (silent) check immediately, ignoring the interval.";
        return true;
    }

    GUID get_parent() override {
        return mainmenu_groups::help;
    }

    void execute(t_uint32 p_index, service_ptr_t<service_base> p_callback) override {
        console::print("Automatic Check Debug: forcing a check now (interval ignored)...");

        std::vector<InstalledComponentInfo> installed = GetInstalledComponents();

        fb2k::inCpuWorkerThread([installed] {
            abort_callback& abort = fb2k::mainAborter();
            std::vector<CheckResult> results = RunUpdateCheck(installed, abort);

            fb2k::inMainThread([results] {
                if (HasUpdateAvailable(results)) {
                    ShowUpdateResultWindow(core_api::get_main_window(), results, "");
                } else {
                    console::print("Automatic Check Debug: no updates found (this would have been silent in a real automatic run).");
                }
                markAutomaticCheckRanNow();
            });
        });
    }
};

static service_factory_single_t<mainmenu_commands_force_automatic_check> g_mainmenu_commands_force_automatic_check_factory;

} // namespace
