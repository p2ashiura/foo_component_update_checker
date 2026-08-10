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

// {F7C3A9E2-6D1B-4F8A-9C2E-3B5D7A1F9C60}
const GUID guid_cfg_auto_check_notification_level =
{ 0xf7c3a9e2, 0x6d1b, 0x4f8a, { 0x9c, 0x2e, 0x3b, 0x5d, 0x7a, 0x1f, 0x9c, 0x60 } };
} // namespace

static cfg_bool g_cfgAutoCheckEnabled(guid_cfg_auto_check_enabled, true);
static cfg_int g_cfgAutoCheckIntervalDays(guid_cfg_auto_check_interval_days, 7);
static cfg_int g_cfgAutoCheckLastRun(guid_cfg_auto_check_last_run, 0); // UNIXエポック秒。0 = 未実施。
static cfg_int g_cfgAutoCheckNotificationLevel(guid_cfg_auto_check_notification_level, 0); // AutoCheckNotificationLevel::UpdatesOnly

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
    // Safe Defaults: 0以下・極端に大きい値は事故のもとなので1〜30日に収める。
    if (days < 1) days = 1;
    if (days > 30) days = 30;
    g_cfgAutoCheckIntervalDays = static_cast<t_int32>(days);
}

AutoCheckNotificationLevel GetAutomaticCheckNotificationLevel() {
    int32_t raw = static_cast<int32_t>(g_cfgAutoCheckNotificationLevel.get());
    switch (raw) {
    case 1: return AutoCheckNotificationLevel::UpdatesAndErrors;
    case 2: return AutoCheckNotificationLevel::Always;
    default: return AutoCheckNotificationLevel::UpdatesOnly;
    }
}

void SetAutomaticCheckNotificationLevel(AutoCheckNotificationLevel level) {
    g_cfgAutoCheckNotificationLevel = static_cast<t_int32>(level);
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
        // アプリ終了処理の途中でこのコールバックが呼ばれた場合、何もせず打ち切る。
        if (fb2k::mainAborter().is_aborting()) return;

        std::vector<InstalledComponentInfo> installed = GetInstalledComponents();

        fb2k::inCpuWorkerThread([installed] {
            abort_callback& abort = fb2k::mainAborter();
            std::vector<CheckResult> results = RunUpdateCheck(installed, abort);

            AutoCheckNotificationLevel level = GetAutomaticCheckNotificationLevel();
            bool shouldShow =
                (level == AutoCheckNotificationLevel::Always) ||
                HasUpdateAvailable(results) ||
                (level == AutoCheckNotificationLevel::UpdatesAndErrors && HasAnyError(results));

            // Non-Intrusive(既定): 通知レベルに応じて必要な場合のみ
            // ポップアップウィンドウで知らせる。
            if (shouldShow) {
                fb2k::inMainThread([results] {
                    // アプリ終了処理の途中でこのコールバックが呼ばれた場合、
                    // 新しくウィンドウを作らずに打ち切る(main_thread_callback経由の
                    // 処理が終了中にディスパッチされることによる不具合を避ける)。
                    if (fb2k::mainAborter().is_aborting()) return;

                    ShowUpdateResultWindow(
                        core_api::get_main_window(),
                        results,
                        "Automatic check: no installed components matched a registered repository."
                    );
                });
            }

            fb2k::inMainThread([] {
                if (fb2k::mainAborter().is_aborting()) return;
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

} // namespace
