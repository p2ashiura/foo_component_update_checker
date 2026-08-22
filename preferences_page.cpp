#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"
#include "SDK-2025-03-07/foobar2000/SDK/coreDarkMode.h"
#include "automatic_check.h"
#include "repository_mapping_ui.h"
#include "update_check.h"
#include "result_window.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

#include <vector>
#include <string>

// ------------------------------------------------------------------
// Preferences -> Tools -> Component Update Checker
//
// これまでHelpメニューに散らばっていた以下を1ページに集約する:
//   - Automatic Check Settings...(有効/無効・間隔) -> このページに直接埋め込み
//   - Manage Sources...                             -> ボタンから同じダイアログを開く
//   - Check for Component Updates                  -> 「Check Now」ボタン
//
// repository_mapping_ui.cpp等と同じ、WS_CHILDスタイルのダイアログテンプレート
// (CreateDialogIndirectParam)を使う方式。独自WNDCLASSを登録する方式は
// 初回実装時に画面が真っ白になる問題が出たため、実績のあるこちらに統一した。
// ------------------------------------------------------------------

namespace {

const int IDC_PP_ENABLED_CHECK = 4001;
const int IDC_PP_INTERVAL_EDIT = 4002;
const int IDC_PP_MANAGE_REPOS_BTN = 4003;
const int IDC_PP_CHECK_NOW_BTN = 4004;
const int IDC_PP_NOTIFY_UPDATES_ONLY_RADIO = 4005;
const int IDC_PP_NOTIFY_UPDATES_AND_ERRORS_RADIO = 4006;
const int IDC_PP_NOTIFY_ALWAYS_RADIO = 4007;
const int IDC_PP_OPEN_REGISTRY_BTN = 4008;

// {38FF764D-0212-4E61-8A6B-B3D7C5DE8F9D}
const GUID guid_preferences_page_component_update_checker =
{ 0x38ff764d, 0x0212, 0x4e61, { 0x8a, 0x6b, 0xb3, 0xd7, 0xc5, 0xde, 0x8f, 0x9d } };

// README「Known-Component Registry」節と表記を揃えたURL。
const char* const kRegistryUrl = "https://github.com/p2ashiura/foo_component_update_checker-registry";

// DS_SHELLFONTの自動フォント解決に頼らず、Windowsのダイアログ標準フォント
// (メッセージボックス等と同じ、通常Segoe UI 9pt相当)を自前で作成して使う。
// 一度作成したフォントはプロセス終了まで保持してよい(GDIリソースとして軽微)。
HFONT GetShellUIFont() {
    static HFONT s_font = NULL;
    if (!s_font) {
        NONCLIENTMETRICS ncm = { sizeof(ncm) };
        SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        s_font = CreateFontIndirect(&ncm.lfMessageFont);
    }
    return s_font;
}

int GetDialogFontLineHeight(HWND hDlg) {
    HFONT hFont = GetShellUIFont();
    if (!hFont) return 16;

    HDC hdc = GetDC(hDlg);
    HFONT hOld = (HFONT)SelectObject(hdc, hFont);

    TEXTMETRIC tm = {};
    GetTextMetrics(hdc, &tm);

    SelectObject(hdc, hOld);
    ReleaseDC(hDlg, hdc);

    int h = tm.tmHeight + tm.tmExternalLeading;
    if (h < 14) h = 14;
    return h;
}

// DS_SETFONT(DS_SHELLFONTに含まれる)は、テンプレートに直接定義された
// コントロールにはフォントを自動適用するが、cdit=0で後からCreateWindowExで
// 動的に作ったコントロールには伝わらない。作り終えた後にまとめて適用する。
BOOL CALLBACK SetChildFontEnumProc(HWND hwnd, LPARAM lParam) {
    SendMessage(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

void ApplyDialogFontToChildren(HWND hDlg) {
    HFONT hFont = GetShellUIFont();
    if (!hFont) return;
    SendMessage(hDlg, WM_SETFONT, (WPARAM)hFont, FALSE);
    EnumChildWindows(hDlg, SetChildFontEnumProc, (LPARAM)hFont);
}

class instance_impl : public preferences_page_instance {
public:
    instance_impl(HWND parent, preferences_page_callback::ptr callback)
        : m_callback(callback)
    {
        m_baselineEnabled = GetAutomaticCheckEnabled();
        m_baselineIntervalDays = GetAutomaticCheckIntervalDays();
        m_baselineNotifyLevel = GetAutomaticCheckNotificationLevel();

        m_wnd = CreatePageDialog(parent);

        // ダイアログ本体とその子コントロール(チェックボックス・Edit・ボタン等)に
        // ダークモードのテーマを適用する。createAuto()はfoobar2000側の
        // ダークモード設定変更に自動追従する(コールバック登録済み)。
        m_darkMode.AddDialogWithControls(m_wnd);
    }

    t_uint32 get_state() override {
        // dark_mode_supportedを含めることで、Preferencesダイアログ全体の
        // 配色をこのページの内容と一貫させてもらえる(SDK 2.0以降)。
        t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (IsDirty()) state |= preferences_state::changed;
        return state;
    }

    fb2k::hwnd_t get_wnd() override {
        return m_wnd;
    }

    void apply() override {
        SetAutomaticCheckEnabled(ReadEnabledFromControl());
        SetAutomaticCheckIntervalDays(ReadIntervalFromControl());
        SetAutomaticCheckNotificationLevel(ReadNotifyLevelFromControl());

        m_baselineEnabled = GetAutomaticCheckEnabled();
        m_baselineIntervalDays = GetAutomaticCheckIntervalDays();
        m_baselineNotifyLevel = GetAutomaticCheckNotificationLevel();

        LoadValuesIntoControls();
        NotifyStateChanged();
    }

    void reset() override {
        CheckDlgButton(m_wnd, IDC_PP_ENABLED_CHECK, BST_CHECKED);
        SetWindowTextA(GetDlgItem(m_wnd, IDC_PP_INTERVAL_EDIT), "7");
        CheckRadioButton(m_wnd, IDC_PP_NOTIFY_UPDATES_ONLY_RADIO, IDC_PP_NOTIFY_ALWAYS_RADIO, IDC_PP_NOTIFY_UPDATES_ONLY_RADIO);
        NotifyStateChanged();
    }

    // ダイアログプロシージャから呼ばれる処理(WM_COMMAND経由)
    void OnCommand(WPARAM wp) {
        int id = LOWORD(wp);

        if (id == IDC_PP_ENABLED_CHECK) {
            if (HIWORD(wp) == BN_CLICKED) NotifyStateChanged();
            return;
        }

        if (id == IDC_PP_INTERVAL_EDIT) {
            if (HIWORD(wp) == EN_CHANGE) NotifyStateChanged();
            return;
        }

        if (id == IDC_PP_NOTIFY_UPDATES_ONLY_RADIO || id == IDC_PP_NOTIFY_UPDATES_AND_ERRORS_RADIO || id == IDC_PP_NOTIFY_ALWAYS_RADIO) {
            if (HIWORD(wp) == BN_CLICKED) NotifyStateChanged();
            return;
        }

        if (id == IDC_PP_MANAGE_REPOS_BTN) {
            ShowRepositoryMappingDialog(m_wnd);
            return;
        }

        if (id == IDC_PP_OPEN_REGISTRY_BTN) {
            ShellExecuteA(m_wnd, "open", kRegistryUrl, NULL, NULL, SW_SHOWNORMAL);
            return;
        }

        if (id == IDC_PP_CHECK_NOW_BTN) {
            std::vector<InstalledComponentInfo> installed = GetInstalledComponents();

            fb2k::inCpuWorkerThread([installed] {
                abort_callback& abort = fb2k::mainAborter();
                std::vector<CheckResult> results = RunUpdateCheck(installed, abort);

                fb2k::inMainThread([results] {
                    // アプリ終了処理の途中でこのコールバックが呼ばれた場合、
                    // 新しくウィンドウを作らずに打ち切る。
                    if (fb2k::mainAborter().is_aborting()) return;

                    ShowUpdateResultWindow(
                        core_api::get_main_window(),
                        results,
                        "No installed components matched a registered source.\n"
                        "Use \"Manage Sources...\" to register one."
                    );
                });
            });
            return;
        }
    }

    void BuildControls(HWND hDlg) {
        m_wnd = hDlg;

        int lineH = GetDialogFontLineHeight(hDlg);
        int rowH = lineH + 8;
        int editH = lineH + 10;

        const int MARGIN = 10;
        int y = MARGIN;

        CreateWindowEx(0, _T("BUTTON"), _T("Automatically check for updates"),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            MARGIN, y, 300, editH, hDlg, (HMENU)(INT_PTR)IDC_PP_ENABLED_CHECK,
            NULL, NULL);

        y += rowH + 6;

        const int LABEL_W = 180;
        CreateWindowEx(0, _T("STATIC"), _T("Check interval (days):"),
            WS_CHILD | WS_VISIBLE,
            MARGIN, y + 2, LABEL_W, editH, hDlg, NULL, NULL, NULL);

        CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), NULL,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
            MARGIN + LABEL_W, y, 60, editH, hDlg, (HMENU)(INT_PTR)IDC_PP_INTERVAL_EDIT,
            NULL, NULL);
        // ES_NUMBERで数字以外(マイナス記号・小数点)は入力できないが、
        // 桁数自体も2桁(最大30日)までに制限しておく。
        SendMessage(GetDlgItem(hDlg, IDC_PP_INTERVAL_EDIT), EM_SETLIMITTEXT, 2, 0);

        y += rowH + 14;

        // ---- 通知レベル ----
        CreateWindowEx(0, _T("STATIC"), _T("When automatic check runs, show a popup for:"),
            WS_CHILD | WS_VISIBLE,
            MARGIN, y, 380, editH, hDlg, NULL, NULL, NULL);

        y += rowH;

        CreateWindowEx(0, _T("BUTTON"), _T("Updates only (default)"),
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            MARGIN, y, 360, editH, hDlg, (HMENU)(INT_PTR)IDC_PP_NOTIFY_UPDATES_ONLY_RADIO,
            NULL, NULL);

        y += rowH;

        CreateWindowEx(0, _T("BUTTON"), _T("Updates and errors (network/API failures, etc.)"),
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            MARGIN, y, 360, editH, hDlg, (HMENU)(INT_PTR)IDC_PP_NOTIFY_UPDATES_AND_ERRORS_RADIO,
            NULL, NULL);

        y += rowH;

        CreateWindowEx(0, _T("BUTTON"), _T("Always (even if everything is up to date)"),
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            MARGIN, y, 360, editH, hDlg, (HMENU)(INT_PTR)IDC_PP_NOTIFY_ALWAYS_RADIO,
            NULL, NULL);

        y += rowH + 14;

        const int BTN_W = 170;
        CreateWindowEx(0, _T("BUTTON"), _T("Manage Sources..."),
            WS_CHILD | WS_VISIBLE,
            MARGIN, y, BTN_W, editH, hDlg, (HMENU)(INT_PTR)IDC_PP_MANAGE_REPOS_BTN,
            NULL, NULL);

        CreateWindowEx(0, _T("BUTTON"), _T("Check for Updates Now"),
            WS_CHILD | WS_VISIBLE,
            MARGIN + BTN_W + 10, y, BTN_W, editH, hDlg, (HMENU)(INT_PTR)IDC_PP_CHECK_NOW_BTN,
            NULL, NULL);

        y += rowH + 10;

        CreateWindowEx(0, _T("BUTTON"), _T("Open Known-Component Registry..."),
            WS_CHILD | WS_VISIBLE,
            MARGIN, y, 280, editH, hDlg, (HMENU)(INT_PTR)IDC_PP_OPEN_REGISTRY_BTN,
            NULL, NULL);

        y += rowH + MARGIN;

        // WS_CHILDのダイアログには標準的にタイトルバー・枠が無いため、
        // AdjustWindowRectEx無しでクライアントサイズ = ウィンドウサイズとしてよい。
        SetWindowPos(hDlg, NULL, 0, 0, 420, y, SWP_NOMOVE | SWP_NOZORDER);

        ApplyDialogFontToChildren(hDlg);

        LoadValuesIntoControls();
    }

private:
    HWND m_wnd = NULL;
    fb2k::CCoreDarkModeHooks m_darkMode; // createAuto(): foobar2000のダークモード設定に自動追従
    preferences_page_callback::ptr m_callback;
    bool m_baselineEnabled = true;
    int m_baselineIntervalDays = 7;
    AutoCheckNotificationLevel m_baselineNotifyLevel = AutoCheckNotificationLevel::UpdatesOnly;

    bool ReadEnabledFromControl() {
        return IsDlgButtonChecked(m_wnd, IDC_PP_ENABLED_CHECK) == BST_CHECKED;
    }

    int ReadIntervalFromControl() {
        char buf[16] = {};
        GetWindowTextA(GetDlgItem(m_wnd, IDC_PP_INTERVAL_EDIT), buf, sizeof(buf));
        int days = atoi(buf);
        if (days < 1) days = 1;
        if (days > 30) days = 30;
        return days;
    }

    AutoCheckNotificationLevel ReadNotifyLevelFromControl() {
        if (IsDlgButtonChecked(m_wnd, IDC_PP_NOTIFY_ALWAYS_RADIO) == BST_CHECKED) {
            return AutoCheckNotificationLevel::Always;
        }
        if (IsDlgButtonChecked(m_wnd, IDC_PP_NOTIFY_UPDATES_AND_ERRORS_RADIO) == BST_CHECKED) {
            return AutoCheckNotificationLevel::UpdatesAndErrors;
        }
        return AutoCheckNotificationLevel::UpdatesOnly;
    }

    bool IsDirty() {
        if (!m_wnd) return false;
        return ReadEnabledFromControl() != m_baselineEnabled
            || ReadIntervalFromControl() != m_baselineIntervalDays
            || ReadNotifyLevelFromControl() != m_baselineNotifyLevel;
    }

    void NotifyStateChanged() {
        if (m_callback.is_valid()) m_callback->on_state_changed();
    }

    void LoadValuesIntoControls() {
        CheckDlgButton(m_wnd, IDC_PP_ENABLED_CHECK, GetAutomaticCheckEnabled() ? BST_CHECKED : BST_UNCHECKED);

        char buf[16];
        _itoa_s(GetAutomaticCheckIntervalDays(), buf, sizeof(buf), 10);
        SetWindowTextA(GetDlgItem(m_wnd, IDC_PP_INTERVAL_EDIT), buf);

        int checkedId = IDC_PP_NOTIFY_UPDATES_ONLY_RADIO;
        switch (GetAutomaticCheckNotificationLevel()) {
        case AutoCheckNotificationLevel::UpdatesAndErrors: checkedId = IDC_PP_NOTIFY_UPDATES_AND_ERRORS_RADIO; break;
        case AutoCheckNotificationLevel::Always: checkedId = IDC_PP_NOTIFY_ALWAYS_RADIO; break;
        default: checkedId = IDC_PP_NOTIFY_UPDATES_ONLY_RADIO; break;
        }
        CheckRadioButton(m_wnd, IDC_PP_NOTIFY_UPDATES_ONLY_RADIO, IDC_PP_NOTIFY_ALWAYS_RADIO, checkedId);
    }

    HWND CreatePageDialog(HWND parent);
};

LRESULT CALLBACK PreferencesPageDialogProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        instance_impl* self = (instance_impl*)lp;
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)self);
        if (self) self->BuildControls(hDlg);
        return TRUE;
    }

    case WM_COMMAND: {
        instance_impl* self = (instance_impl*)GetWindowLongPtr(hDlg, DWLP_USER);
        if (self) self->OnCommand(wp);
        return TRUE;
    }
    }

    return FALSE;
}

HWND instance_impl::CreatePageDialog(HWND parent) {
    struct {
        DLGTEMPLATE dlg;
        WORD menu = 0;
        WORD wclass = 0;
        WCHAR title[2] = L"";
        WORD pointsize = 8;
        WCHAR typeface[15] = L"MS Shell Dlg 2";
    } dlgData = {};

    // WS_CHILD: Preferencesページに埋め込まれる子ウィンドウとして作る
    // (これまでのポップアップダイアログはWS_POPUP|WS_CAPTIONだった点が違う)。
    // DS_CONTROLは「他のダイアログに埋め込まれる部品」であることを示す標準スタイル。
    dlgData.dlg.style = WS_CHILD | DS_SHELLFONT | DS_CONTROL;
    dlgData.dlg.dwExtendedStyle = 0;
    dlgData.dlg.cdit = 0;
    dlgData.dlg.x = 0;
    dlgData.dlg.y = 0;
    dlgData.dlg.cx = 100;
    dlgData.dlg.cy = 100;

    return CreateDialogIndirectParam(
        NULL, (LPCDLGTEMPLATE)&dlgData, parent,
        (DLGPROC)PreferencesPageDialogProc, (LPARAM)this);
}

class preferences_page_impl : public preferences_page_v4 {
public:
    const char* get_name() override {
        return "Component Update Checker";
    }

    GUID get_guid() override {
        return guid_preferences_page_component_update_checker;
    }

    GUID get_parent_guid() override {
        return preferences_page::guid_tools;
    }

    preferences_page_instance::ptr instantiate(fb2k::hwnd_t parent, preferences_page_callback::ptr callback) override {
        return new service_impl_t<instance_impl>((HWND)parent, callback);
    }
};

static preferences_page_factory_t<preferences_page_impl> g_preferences_page_factory;

} // namespace
