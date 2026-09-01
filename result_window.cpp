#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"
#include "SDK-2025-03-07/foobar2000/SDK/coreDarkMode.h"
#include "update_check.h"
#include "result_window.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")

#include <vector>
#include <string>
#include <algorithm>

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
const int IDC_RW_STATUS_LABEL = 5003;
const int IDC_RW_PROGRESS = 5004;
const int IDC_RW_EMPTY_LABEL = 5005;

// 手動確認・自動確認のどちらから開いたかに関わらず、結果ウィンドウは同時に
// 1つまでとする。開いている間、そのHWNDをここに保持する
// (メインスレッドからしか触らないため、排他制御は不要)。
HWND g_resultWindowHwnd = NULL;

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

std::string StatusToDisplayString(CheckResult const& r) {
    switch (r.status) {
    case UpdateStatus::UpdateAvailable: return "Update available";
    case UpdateStatus::UpToDate: return "Current";
    case UpdateStatus::ComparisonUnsupported: return "Unable to compare";
    case UpdateStatus::Error: return "Error: " + r.errorMessage;
    }
    return "";
}

// 「操作が必要な行を上に」という考え方で並び順を決める:
// 更新あり(一番見てほしい) > エラー(要確認) > 比較不能(要確認) > 最新(見る必要が薄い)。
// 同じ優先度内の順序は変えない(std::stable_sort)。
int StatusSortRank(UpdateStatus status) {
    switch (status) {
    case UpdateStatus::UpdateAvailable: return 0;
    case UpdateStatus::Error: return 1;
    case UpdateStatus::ComparisonUnsupported: return 2;
    case UpdateStatus::UpToDate: return 3;
    }
    return 4;
}

void SortResultsByStatus(std::vector<CheckResult>& results) {
    std::stable_sort(results.begin(), results.end(),
        [](CheckResult const& a, CheckResult const& b) {
            return StatusSortRank(a.status) < StatusSortRank(b.status);
        });
}

struct ResultWindowContext {
    std::vector<InstalledComponentInfo> installed;
    std::vector<CheckResult> results;
    bool checking = true;
    std::string emptyMessage;
    fb2k::CCoreDarkModeHooks darkMode;
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

// ListView自体をサブクラス化し、ヘッダー(SysHeader32、ListViewの子ウィンドウ)から
// 上がってくるNM_CUSTOMDRAWをここで直接処理する。ヘッダーの通知先はその親である
// ListView自身であり、ダイアログのWM_NOTIFYまでは届かないため、この位置で
// 処理する必要がある。dwRefDataには、生成時に決めたダーク/ライトの真偽値
// (bool)をそのままDWORD_PTRとして渡している(ダイアログ表示中は変化しないため)。
LRESULT CALLBACK ListViewHeaderColorSubclassProc(
    HWND hList, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR uIdSubclass, DWORD_PTR dwRefData
) {
    if (msg == WM_NOTIFY) {
        LPNMHDR nm = (LPNMHDR)lp;
        if (nm->code == NM_CUSTOMDRAW) {
            HWND hHeader = ListView_GetHeader(hList);
            if (nm->hwndFrom == hHeader) {
                LPNMCUSTOMDRAW cd = (LPNMCUSTOMDRAW)lp;
                bool dark = (dwRefData != 0);

                if (cd->dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;
                }
                if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                    SetTextColor(cd->hdc, dark ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOWTEXT));
                    return CDRF_DODEFAULT;
                }
            }
        }
    }

    return DefSubclassProc(hList, msg, wp, lp);
}

LRESULT CALLBACK ResultWindowDialogProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        ResultWindowContext* ctx = (ResultWindowContext*)lp;
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)ctx);
        g_resultWindowHwnd = hDlg;

        int lineH = GetDialogFontLineHeight(hDlg);
        int editH = lineH + 10;

        const int MARGIN = 10;
        const int CLIENT_W = 560;
        const int LIST_H = 220;

        int y = MARGIN;

        HWND hList = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
            WS_CHILD | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP | (ctx->checking ? 0 : WS_VISIBLE),
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

        // 検査完了までの間、ListViewと同じ領域に進捗表示(マーキー型プログレスバー
        // + ステータステキスト)を重ねて置く。完了後はこちらを隠し、ListViewを
        // 表示する側に切り替える(ウィンドウサイズは変えない)。
        CreateWindowEx(0, _T("STATIC"), _T("Checking for updates..."),
            WS_CHILD | (ctx->checking ? WS_VISIBLE : 0) | SS_CENTER,
            MARGIN, y + LIST_H / 2 - editH, CLIENT_W - MARGIN * 2, editH,
            hDlg, (HMENU)(INT_PTR)IDC_RW_STATUS_LABEL, NULL, NULL);

        HWND hProgress = CreateWindowEx(0, PROGRESS_CLASS, NULL,
            WS_CHILD | (ctx->checking ? WS_VISIBLE : 0) | PBS_MARQUEE,
            MARGIN, y + LIST_H / 2, CLIENT_W - MARGIN * 2, editH,
            hDlg, (HMENU)(INT_PTR)IDC_RW_PROGRESS, NULL, NULL);
        SendMessage(hProgress, PBM_SETMARQUEE, TRUE, 30);

        // 検査完了後、結果が0件だった場合はこちらにemptyMessageを表示する
        // (ListViewとは排他表示。初期状態では非表示)。
        CreateWindowEx(0, _T("STATIC"), _T(""),
            WS_CHILD | SS_LEFT,
            MARGIN, y, CLIENT_W - MARGIN * 2, LIST_H,
            hDlg, (HMENU)(INT_PTR)IDC_RW_EMPTY_LABEL, NULL, NULL);

        y += LIST_H + 10;

        const int BTN_W = 150;
        CreateWindowEx(0, _T("BUTTON"), _T("Open Release Page"),
            WS_CHILD | WS_VISIBLE | (ctx->checking ? WS_DISABLED : 0),
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

        ApplyDialogFontToChildren(hDlg);

        // ListView自体にはダークモードフックを適用しない
        // (内部のカスタム描画と、手動で設定する行の色指定が競合するため)。
        // ダイアログ本体の背景と、ボタン類の見た目だけをテーマに合わせる。
        ctx->darkMode.AddDialog(hDlg);
        ctx->darkMode.AddCtrlAuto(GetDlgItem(hDlg, IDC_RW_OPEN_BTN));
        ctx->darkMode.AddCtrlAuto(GetDlgItem(hDlg, IDOK));

        // ListViewの行の文字色・背景色は完全に手動で設定する。
        {
            bool dark = (bool)ctx->darkMode;
            HWND listForColors = GetDlgItem(hDlg, IDC_RW_LIST);
            if (dark) {
                ListView_SetBkColor(listForColors, RGB(32, 32, 32));
                ListView_SetTextBkColor(listForColors, RGB(32, 32, 32));
                ListView_SetTextColor(listForColors, RGB(255, 255, 255));
            } else {
                ListView_SetBkColor(listForColors, GetSysColor(COLOR_WINDOW));
                ListView_SetTextBkColor(listForColors, GetSysColor(COLOR_WINDOW));
                ListView_SetTextColor(listForColors, GetSysColor(COLOR_WINDOWTEXT));
            }
            InvalidateRect(listForColors, NULL, TRUE);

            // 列見出し(ヘッダー)はListView本体とは別の子ウィンドウ(SysHeader32)
            // なので、行の色設定(ListView_Set*Color)の対象外。
            // 背景色はSetWindowThemeで切り替わるが、文字色までは追従しない。
            //
            // ヘッダーのNM_CUSTOMDRAW通知は「ヘッダーの親」であるListView自身に
            // 送られ、そこで止まってしまう(ダイアログのWM_NOTIFYまで届かない)。
            // そのためListView自体をサブクラス化し、そこで直接文字色を設定する。
            HWND hHeader = ListView_GetHeader(listForColors);
            if (hHeader) {
                SetWindowTheme(hHeader, dark ? L"DarkMode_ItemsView" : L"ItemsView", NULL);
            }
            SetWindowSubclass(listForColors, ListViewHeaderColorSubclassProc, 1, (DWORD_PTR)dark);
        }

        // ダイアログ表示と同時に検査を開始する(ここより前で、ダイアログの
        // 見た目(進捗表示)は既に組み終わっている)。ダイアログはモーダルだが、
        // fb2k::inMainThreadのコールバックは通常のウィンドウメッセージと同様に
        // 配送されるため、表示中も問題なく結果を受け取れる。
        //
        // ctx->checking == false の場合(自動チェックからの呼び出し)は、
        // 検査は既に完了しているため、ここでは検査を開始せずそのまま結果を表示する
        // (コントロールの初期表示状態は、生成時点で既にctx->checkingに基づいて
        // 決まっている。ここではListViewへの中身の反映のみ行う)。
        if (ctx->checking) {
            std::vector<InstalledComponentInfo> installedCopy = ctx->installed;
            fb2k::inCpuWorkerThread([installedCopy, hDlg] {
                abort_callback& abort = fb2k::mainAborter();
                std::vector<CheckResult> results = RunUpdateCheck(installedCopy, abort);

                fb2k::inMainThread([results, hDlg] {
                    // ユーザーが検査中にダイアログを閉じていた場合は何もしない
                    // (検査自体はバックグラウンドで完走させ、結果は捨てる)。
                    if (!IsWindow(hDlg)) return;

                    ResultWindowContext* ctx2 = (ResultWindowContext*)GetWindowLongPtr(hDlg, DWLP_USER);
                    if (!ctx2) return;

                    ctx2->results = results;
                    ctx2->checking = false;
                    SortResultsByStatus(ctx2->results);

                    ShowWindow(GetDlgItem(hDlg, IDC_RW_STATUS_LABEL), SW_HIDE);
                    ShowWindow(GetDlgItem(hDlg, IDC_RW_PROGRESS), SW_HIDE);

                    if (results.empty()) {
                        SetWindowTextA(GetDlgItem(hDlg, IDC_RW_EMPTY_LABEL), ctx2->emptyMessage.c_str());
                        ShowWindow(GetDlgItem(hDlg, IDC_RW_EMPTY_LABEL), SW_SHOW);
                    } else {
                        HWND hListDone = GetDlgItem(hDlg, IDC_RW_LIST);
                        PopulateListView(hListDone, ctx2->results);
                        ShowWindow(hListDone, SW_SHOW);
                        EnableWindow(GetDlgItem(hDlg, IDC_RW_OPEN_BTN), TRUE);
                    }
                });
            });
        } else {
            if (ctx->results.empty()) {
                SetWindowTextA(GetDlgItem(hDlg, IDC_RW_EMPTY_LABEL), ctx->emptyMessage.c_str());
            } else {
                PopulateListView(GetDlgItem(hDlg, IDC_RW_LIST), ctx->results);
                EnableWindow(GetDlgItem(hDlg, IDC_RW_OPEN_BTN), TRUE);
            }
        }

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

namespace {

// 実際にダイアログを開く共通処理。checking==trueならinstalledを使い、
// ダイアログ表示後に内部で検査を開始する(手動確認用)。checking==falseなら
// precomputedResultsをそのまま使い、検査は行わない(自動確認用。検査は
// 呼び出し元(automatic_check.cpp)で既に完了している)。
void ShowResultDialogInternal(
    HWND parent,
    std::vector<InstalledComponentInfo> const& installed,
    std::vector<CheckResult> const& precomputedResults,
    bool checking,
    const char* emptyMessage
) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    ResultWindowContext ctx;
    ctx.installed = installed;
    ctx.results = precomputedResults;
    ctx.checking = checking;
    ctx.emptyMessage = emptyMessage;
    SortResultsByStatus(ctx.results);

    struct {
        DLGTEMPLATE dlg;
        WORD menu = 0;
        WORD wclass = 0;
        WCHAR title[32] = L"Component Update Checker";
        WORD pointsize = 8;
        WCHAR typeface[15] = L"MS Shell Dlg 2";
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

    // ダイアログが閉じ終わったので、開いているウィンドウの記録を解除する
    // (WM_INITDIALOGで設定したものを、DialogBoxIndirectParamが返ってきた
    // = ダイアログが完全に閉じた、ここで初めて解除する)。
    g_resultWindowHwnd = NULL;
}

} // namespace

void ShowUpdateResultWindow(HWND parent, std::vector<InstalledComponentInfo> const& installed, const char* emptyMessage) {
    if (g_resultWindowHwnd && IsWindow(g_resultWindowHwnd)) {
        // 既に結果ウィンドウが開いている(自動確認の結果か、他の手動確認)。
        // 新しく開き直さず、既存のウィンドウを前面に出すだけにする。
        SetForegroundWindow(g_resultWindowHwnd);
        return;
    }

    ShowResultDialogInternal(parent, installed, {}, /*checking=*/true, emptyMessage);
}

void ShowUpdateResultWindowWithResults(HWND parent, std::vector<CheckResult> const& results, const char* emptyMessage) {
    if (g_resultWindowHwnd && IsWindow(g_resultWindowHwnd)) {
        // 手動確認のウィンドウ(または前回の自動確認の結果)が既に開いている。
        // Non-Intrusiveの原則により、今回の自動確認の表示は諦め、
        // 次回の定期確認に委ねる(前面に出す・待たせる、といったことはしない)。
        return;
    }

    ShowResultDialogInternal(parent, {}, results, /*checking=*/false, emptyMessage);
}
