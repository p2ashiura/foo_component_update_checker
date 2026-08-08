#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"
#include "automatic_check.h"
#include "repository_mapping_ui.h"
#include "update_check.h"
#include "result_window.h"

#include <windows.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#include <vector>
#include <string>

// ------------------------------------------------------------------
// Preferences -> Tools -> Component Update Checker
//
// これまでHelpメニューに散らばっていた以下を1ページに集約する:
//   - Automatic Check Settings...(有効/無効・間隔) -> このページに直接埋め込み
//   - Manage Component Repositories...             -> ボタンから同じダイアログを開く
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

// {38FF764D-0212-4E61-8A6B-B3D7C5DE8F9D}
const GUID guid_preferences_page_component_update_checker =
{ 0x38ff764d, 0x0212, 0x4e61, { 0x8a, 0x6b, 0xb3, 0xd7, 0xc5, 0xde, 0x8f, 0x9d } };

int GetDialogFontLineHeight(HWND hDlg) {
    HFONT hFont = (HFONT)SendMessage(hDlg, WM_GETFONT, 0, 0);
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

class instance_impl : public preferences_page_instance {
public:
    instance_impl(HWND parent, preferences_page_callback::ptr callback)
        : m_callback(callback)
    {
        m_baselineEnabled = GetAutomaticCheckEnabled();
        m_baselineIntervalDays = GetAutomaticCheckIntervalDays();

        m_wnd = CreatePageDialog(parent);
    }

    t_uint32 get_state() override {
        t_uint32 state = preferences_state::resettable;
        if (IsDirty()) state |= preferences_state::changed;
        return state;
    }

    fb2k::hwnd_t get_wnd() override {
        return m_wnd;
    }

    void apply() override {
        SetAutomaticCheckEnabled(ReadEnabledFromControl());
        SetAutomaticCheckIntervalDays(ReadIntervalFromControl());

        m_baselineEnabled = GetAutomaticCheckEnabled();
        m_baselineIntervalDays = GetAutomaticCheckIntervalDays();

        LoadValuesIntoControls();
        NotifyStateChanged();
    }

    void reset() override {
        CheckDlgButton(m_wnd, IDC_PP_ENABLED_CHECK, BST_CHECKED);
        SetWindowTextA(GetDlgItem(m_wnd, IDC_PP_INTERVAL_EDIT), "7");
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

        if (id == IDC_PP_MANAGE_REPOS_BTN) {
            ShowRepositoryMappingDialog(m_wnd);
            return;
        }

        if (id == IDC_PP_CHECK_NOW_BTN) {
            std::vector<InstalledComponentInfo> installed = GetInstalledComponents();

            fb2k::inCpuWorkerThread([installed] {
                abort_callback& abort = fb2k::mainAborter();
                std::vector<CheckResult> results = RunUpdateCheck(installed, abort);

                fb2k::inMainThread([results] {
                    ShowUpdateResultWindow(
                        core_api::get_main_window(),
                        results,
                        "No installed components matched a registered repository.\n"
                        "Use \"Manage Repositories...\" to register one."
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

        y += rowH + 14;

        const int BTN_W = 170;
        CreateWindowEx(0, _T("BUTTON"), _T("Manage Repositories..."),
            WS_CHILD | WS_VISIBLE,
            MARGIN, y, BTN_W, editH, hDlg, (HMENU)(INT_PTR)IDC_PP_MANAGE_REPOS_BTN,
            NULL, NULL);

        CreateWindowEx(0, _T("BUTTON"), _T("Check for Updates Now"),
            WS_CHILD | WS_VISIBLE,
            MARGIN + BTN_W + 10, y, BTN_W, editH, hDlg, (HMENU)(INT_PTR)IDC_PP_CHECK_NOW_BTN,
            NULL, NULL);

        y += rowH + MARGIN;

        // WS_CHILDのダイアログには標準的にタイトルバー・枠が無いため、
        // AdjustWindowRectEx無しでクライアントサイズ = ウィンドウサイズとしてよい。
        SetWindowPos(hDlg, NULL, 0, 0, 420, y, SWP_NOMOVE | SWP_NOZORDER);

        LoadValuesIntoControls();
    }

private:
    HWND m_wnd = NULL;
    preferences_page_callback::ptr m_callback;
    bool m_baselineEnabled = true;
    int m_baselineIntervalDays = 7;

    bool ReadEnabledFromControl() {
        return IsDlgButtonChecked(m_wnd, IDC_PP_ENABLED_CHECK) == BST_CHECKED;
    }

    int ReadIntervalFromControl() {
        char buf[16] = {};
        GetWindowTextA(GetDlgItem(m_wnd, IDC_PP_INTERVAL_EDIT), buf, sizeof(buf));
        int days = atoi(buf);
        if (days < 1) days = 1;
        return days;
    }

    bool IsDirty() {
        if (!m_wnd) return false;
        return ReadEnabledFromControl() != m_baselineEnabled
            || ReadIntervalFromControl() != m_baselineIntervalDays;
    }

    void NotifyStateChanged() {
        if (m_callback.is_valid()) m_callback->on_state_changed();
    }

    void LoadValuesIntoControls() {
        CheckDlgButton(m_wnd, IDC_PP_ENABLED_CHECK, GetAutomaticCheckEnabled() ? BST_CHECKED : BST_UNCHECKED);

        char buf[16];
        _itoa_s(GetAutomaticCheckIntervalDays(), buf, sizeof(buf), 10);
        SetWindowTextA(GetDlgItem(m_wnd, IDC_PP_INTERVAL_EDIT), buf);
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
