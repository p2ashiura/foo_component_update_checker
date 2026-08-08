#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"
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
// 更新確認結果ウィンドウ
//
// 他のダイアログと同じ方式(.rc不要、DialogBoxIndirectParam + 実行時の
// CreateWindowEx)を踏襲している。ListView(report view)で結果一覧を表示し、
// 行のダブルクリックまたは「Open Release Page」ボタンでリリースページを開く。
// ------------------------------------------------------------------

namespace {

const int IDC_RW_LIST = 5001;
const int IDC_RW_OPEN_BTN = 5002;

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

std::string StatusToDisplayString(CheckResult const& r) {
    switch (r.status) {
    case UpdateStatus::UpdateAvailable: return "Update available";
    case UpdateStatus::UpToDate: return "Up to date";
    case UpdateStatus::ComparisonUnsupported: return "Unable to compare";
    case UpdateStatus::Error: return "Error: " + r.errorMessage;
    }
    return "";
}

struct ResultWindowContext {
    std::vector<CheckResult> results;
};

void PopulateListView(HWND hList, std::vector<CheckResult> const& results) {
    ListView_DeleteAllItems(hList);

    for (size_t i = 0; i < results.size(); ++i) {
        CheckResult const& r = results[i];

        LVITEMA item = {};
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        item.iSubItem = 0;
        item.pszText = (LPSTR)r.displayName.c_str();
        SendMessageA(hList, LVM_INSERTITEMA, 0, (LPARAM)&item);

        LVITEMA sub = {};
        sub.mask = LVIF_TEXT;
        sub.iItem = (int)i;

        sub.iSubItem = 1;
        sub.pszText = (LPSTR)r.installedVersion.c_str();
        SendMessageA(hList, LVM_SETITEMTEXTA, (WPARAM)i, (LPARAM)&sub);

        std::string latest = r.latestVersion.empty() ? "-" : r.latestVersion;
        sub.iSubItem = 2;
        sub.pszText = (LPSTR)latest.c_str();
        SendMessageA(hList, LVM_SETITEMTEXTA, (WPARAM)i, (LPARAM)&sub);

        std::string statusText = StatusToDisplayString(r);
        sub.iSubItem = 3;
        sub.pszText = (LPSTR)statusText.c_str();
        SendMessageA(hList, LVM_SETITEMTEXTA, (WPARAM)i, (LPARAM)&sub);
    }
}

void OpenSelectedRelease(HWND hDlg, HWND hList, std::vector<CheckResult> const* results) {
    int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
    if (sel < 0 || (size_t)sel >= results->size()) {
        MessageBox(hDlg, _T("Please select an item first."), _T("Component Update Checker"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    CheckResult const& r = (*results)[sel];

    // 「押せる = 見る価値がある」という一貫したルールにするため、
    // Up to date / Error のときはリリースページへの導線を出さない
    // (念のため見たい場合は Manage Repositories... から手動で開ける)。
    if (r.status == UpdateStatus::UpToDate) {
        MessageBox(hDlg, _T("This component is already up to date."), _T("Component Update Checker"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (r.status == UpdateStatus::Error || r.releaseUrl.empty()) {
        MessageBox(hDlg, _T("No release page is available for this item."), _T("Component Update Checker"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    ShellExecuteA(hDlg, "open", r.releaseUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

LRESULT CALLBACK ResultWindowDialogProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        ResultWindowContext* ctx = (ResultWindowContext*)lp;
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)ctx);

        int lineH = GetDialogFontLineHeight(hDlg);
        int editH = lineH + 10;

        const int MARGIN = 10;
        const int CLIENT_W = 560;
        const int LIST_H = 220;

        int y = MARGIN;

        HWND hList = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
            MARGIN, y, CLIENT_W - MARGIN * 2, LIST_H, hDlg, (HMENU)(INT_PTR)IDC_RW_LIST,
            NULL, NULL);

        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT);

        LVCOLUMNA col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH;

        col.cx = 190; col.pszText = (LPSTR)"Component";
        SendMessageA(hList, LVM_INSERTCOLUMNA, 0, (LPARAM)&col);
        col.cx = 90; col.pszText = (LPSTR)"Installed";
        SendMessageA(hList, LVM_INSERTCOLUMNA, 1, (LPARAM)&col);
        col.cx = 90; col.pszText = (LPSTR)"Latest";
        SendMessageA(hList, LVM_INSERTCOLUMNA, 2, (LPARAM)&col);
        col.cx = 140; col.pszText = (LPSTR)"Status";
        SendMessageA(hList, LVM_INSERTCOLUMNA, 3, (LPARAM)&col);

        PopulateListView(hList, ctx->results);

        y += LIST_H + 10;

        const int BTN_W = 150;
        CreateWindowEx(0, _T("BUTTON"), _T("Open Release Page"),
            WS_CHILD | WS_VISIBLE,
            MARGIN, y, BTN_W, editH, hDlg, (HMENU)(INT_PTR)IDC_RW_OPEN_BTN,
            NULL, NULL);

        const int CLOSE_BTN_W = 80;
        CreateWindowEx(0, _T("BUTTON"), _T("Close"),
            WS_CHILD | WS_VISIBLE,
            CLIENT_W - MARGIN - CLOSE_BTN_W, y, CLOSE_BTN_W, editH, hDlg, (HMENU)(INT_PTR)IDOK,
            NULL, NULL);

        y += editH + MARGIN;

        // ダイアログのサイズを実ピクセル単位で確定させる(他のダイアログと同じ手順)。
        RECT rc = { 0, 0, CLIENT_W, y };
        AdjustWindowRectEx(&rc,
            (DWORD)GetWindowLongPtr(hDlg, GWL_STYLE),
            FALSE,
            (DWORD)GetWindowLongPtr(hDlg, GWL_EXSTYLE));

        int winW = rc.right - rc.left;
        int winH = rc.bottom - rc.top;

        HWND ownerWnd = GetParent(hDlg);
        RECT rcOwner;
        HMONITOR hMon = NULL;
        if (ownerWnd && GetWindowRect(ownerWnd, &rcOwner)) {
            hMon = MonitorFromRect(&rcOwner, MONITOR_DEFAULTTONEAREST);
        } else {
            POINT ptZero = { 0, 0 };
            hMon = MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);
        }

        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMon, &mi);
        int posX = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - winW) / 2;
        int posY = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - winH) / 2;

        SetWindowPos(hDlg, NULL, posX, posY, winW, winH, SWP_NOZORDER);

        return TRUE;
    }

    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->idFrom == IDC_RW_LIST && nm->code == NM_DBLCLK) {
            ResultWindowContext* ctx = (ResultWindowContext*)GetWindowLongPtr(hDlg, DWLP_USER);
            HWND hList = GetDlgItem(hDlg, IDC_RW_LIST);
            if (ctx) OpenSelectedRelease(hDlg, hList, &ctx->results);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        if (LOWORD(wp) == IDC_RW_OPEN_BTN) {
            ResultWindowContext* ctx = (ResultWindowContext*)GetWindowLongPtr(hDlg, DWLP_USER);
            HWND hList = GetDlgItem(hDlg, IDC_RW_LIST);
            if (ctx) OpenSelectedRelease(hDlg, hList, &ctx->results);
            return TRUE;
        }

        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wp));
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }

    return FALSE;
}

} // namespace

void ShowUpdateResultWindow(HWND parent, std::vector<CheckResult> const& results, const char* emptyMessage) {
    if (results.empty()) {
        MessageBoxA(parent, emptyMessage, "Component Update Checker", MB_OK);
        return;
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    ResultWindowContext ctx;
    ctx.results = results;

    struct {
        DLGTEMPLATE dlg;
        WORD menu = 0;
        WORD wclass = 0;
        WCHAR title[32] = L"Component Update Checker";
    } dlgData = {};

    dlgData.dlg.style = DS_MODALFRAME | DS_SHELLFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dlgData.dlg.dwExtendedStyle = 0;
    dlgData.dlg.cdit = 0;
    dlgData.dlg.x = 0;
    dlgData.dlg.y = 0;
    dlgData.dlg.cx = 100;
    dlgData.dlg.cy = 100;

    DialogBoxIndirectParam(
        NULL, (LPCDLGTEMPLATE)&dlgData, parent,
        (DLGPROC)ResultWindowDialogProc, (LPARAM)&ctx);
}
