#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"
#include "repository_mapping.h"
#include "repository_mapping_ui.h"

#include <windows.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#include <vector>
#include <string>
#include <algorithm>

// ------------------------------------------------------------------
// Repository Mapping登録用ダイアログ
//
// foo_albumtrainの設定ダイアログと同じ方式(.rcファイルを使わず、
// DialogBoxIndirectParam + 実行時のCreateWindowExでコントロールを組み立てる)
// を踏襲している。
//
// レイアウト:
//   [Component:      [ドロップダウン(導入済みコンポーネント一覧)]]
//   [GitHub Owner:    [Edit]]
//   [GitHub Repo:     [Edit]]
//   [Save]
//   [登録済み一覧(ListBox)]
//   [Remove Selected]
//   [Close]
// ------------------------------------------------------------------

namespace {

// コントロールID(このダイアログ内のみで完結する値なので、他のダイアログとの
// 重複は気にしなくてよい)
const int IDC_RM_COMPONENT_COMBO = 2001;
const int IDC_RM_OWNER_EDIT = 2002;
const int IDC_RM_REPO_EDIT = 2003;
const int IDC_RM_SAVE_BTN = 2004;
const int IDC_RM_LIST = 2005;
const int IDC_RM_REMOVE_BTN = 2006;

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

// componentversion::enumerate() から、導入済みコンポーネントのDLL名一覧を
// 重複無し・アルファベット順で取得する。
std::vector<std::string> GetInstalledComponentDllNames() {
    std::vector<std::string> names;

    for (auto ptr : componentversion::enumerate()) {
        pfc::string8 fileName;
        ptr->get_file_name(fileName);
        names.push_back(fileName.c_str());
    }

    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

void RefreshEntryListBox(HWND hDlg) {
    HWND listBox = GetDlgItem(hDlg, IDC_RM_LIST);
    SendMessage(listBox, LB_RESETCONTENT, 0, 0);

    auto entries = loadRepositoryMapping();
    for (auto const& e : entries) {
        pfc::string8 line;
        line << e.dllName.c_str() << "  ->  " << e.owner.c_str() << "/" << e.repo.c_str();
        SendMessageA(listBox, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
}

LRESULT CALLBACK RepositoryMappingDialogProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        int lineH = GetDialogFontLineHeight(hDlg);
        int rowH = lineH + 8;
        int editH = lineH + 10;
        int labelH = lineH + 4;

        const int MARGIN = 10;
        const int LABEL_W = 110;
        const int FIELD_X = MARGIN + LABEL_W + 6;
        const int FIELD_W = 220;
        const int CLIENT_W = FIELD_X + FIELD_W + MARGIN;

        int y = MARGIN;

        // ---- Component ----
        CreateWindowEx(0, _T("STATIC"), _T("Component:"),
            WS_CHILD | WS_VISIBLE,
            MARGIN, y + 2, LABEL_W, labelH, hDlg, NULL, NULL, NULL);

        HWND componentCombo = CreateWindowEx(WS_EX_CLIENTEDGE, WC_COMBOBOX, NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            FIELD_X, y, FIELD_W, editH * 8, hDlg, (HMENU)(INT_PTR)IDC_RM_COMPONENT_COMBO,
            NULL, NULL);

        for (auto const& name : GetInstalledComponentDllNames()) {
            SendMessageA(componentCombo, CB_ADDSTRING, 0, (LPARAM)name.c_str());
        }
        if (SendMessage(componentCombo, CB_GETCOUNT, 0, 0) > 0) {
            SendMessage(componentCombo, CB_SETCURSEL, 0, 0);
        }

        y += rowH + 4;

        // ---- GitHub Owner ----
        CreateWindowEx(0, _T("STATIC"), _T("GitHub Owner:"),
            WS_CHILD | WS_VISIBLE,
            MARGIN, y + 2, LABEL_W, labelH, hDlg, NULL, NULL, NULL);

        CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), NULL,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            FIELD_X, y, FIELD_W, editH, hDlg, (HMENU)(INT_PTR)IDC_RM_OWNER_EDIT,
            NULL, NULL);

        y += rowH + 4;

        // ---- GitHub Repo ----
        CreateWindowEx(0, _T("STATIC"), _T("GitHub Repo:"),
            WS_CHILD | WS_VISIBLE,
            MARGIN, y + 2, LABEL_W, labelH, hDlg, NULL, NULL, NULL);

        CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), NULL,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            FIELD_X, y, FIELD_W, editH, hDlg, (HMENU)(INT_PTR)IDC_RM_REPO_EDIT,
            NULL, NULL);

        y += rowH + 4;

        // ---- Save ----
        const int BTN_W = 80;
        CreateWindowEx(0, _T("BUTTON"), _T("Save"),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            FIELD_X, y, BTN_W, editH, hDlg, (HMENU)(INT_PTR)IDC_RM_SAVE_BTN,
            NULL, NULL);

        y += rowH + 10;

        // ---- 登録済み一覧 ----
        CreateWindowEx(0, _T("STATIC"), _T("Registered:"),
            WS_CHILD | WS_VISIBLE,
            MARGIN, y, CLIENT_W - MARGIN * 2, labelH, hDlg, NULL, NULL, NULL);

        y += labelH;

        const int LIST_H = rowH * 5;
        CreateWindowEx(WS_EX_CLIENTEDGE, _T("LISTBOX"), NULL,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_HASSTRINGS,
            MARGIN, y, CLIENT_W - MARGIN * 2, LIST_H, hDlg, (HMENU)(INT_PTR)IDC_RM_LIST,
            NULL, NULL);

        y += LIST_H + 6;

        CreateWindowEx(0, _T("BUTTON"), _T("Remove Selected"),
            WS_CHILD | WS_VISIBLE,
            MARGIN, y, 130, editH, hDlg, (HMENU)(INT_PTR)IDC_RM_REMOVE_BTN,
            NULL, NULL);

        y += rowH + 12;

        // ---- Close ----
        const int CLOSE_BTN_W = 80;
        CreateWindowEx(0, _T("BUTTON"), _T("Close"),
            WS_CHILD | WS_VISIBLE,
            CLIENT_W - MARGIN - CLOSE_BTN_W, y, CLOSE_BTN_W, editH, hDlg, (HMENU)(INT_PTR)IDOK,
            NULL, NULL);

        y += editH + MARGIN;

        // -----------------------------------------------
        // ダイアログのサイズを実ピクセル単位で確定させる
        // (クライアント領域のサイズをそのままSetWindowPosに渡すと、
        //  タイトルバー・枠線の分だけ実際の表示領域が狭くなり、
        //  右端や下端のコントロールが欠ける。AdjustWindowRectExで
        //  外枠込みのウィンドウサイズへ変換してから確定する)
        // -----------------------------------------------
        int desiredClientW = CLIENT_W;
        int desiredClientH = y;

        RECT rc = { 0, 0, desiredClientW, desiredClientH };
        AdjustWindowRectEx(&rc,
            (DWORD)GetWindowLongPtr(hDlg, GWL_STYLE),
            FALSE,
            (DWORD)GetWindowLongPtr(hDlg, GWL_EXSTYLE));

        int winW = rc.right - rc.left;
        int winH = rc.bottom - rc.top;

        // 親ウィンドウ(foobar2000本体)があるモニターの中央に表示する
        int posX, posY;
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
        posX = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - winW) / 2;
        posY = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - winH) / 2;

        SetWindowPos(hDlg, NULL, posX, posY, winW, winH, SWP_NOZORDER);

        RefreshEntryListBox(hDlg);
        return TRUE;
    }

    case WM_COMMAND: {
        if (LOWORD(wp) == IDC_RM_SAVE_BTN) {
            HWND componentCombo = GetDlgItem(hDlg, IDC_RM_COMPONENT_COMBO);
            int sel = (int)SendMessage(componentCombo, CB_GETCURSEL, 0, 0);
            if (sel == CB_ERR) {
                MessageBox(hDlg, _T("Please select a component."), _T("Repository Mapping"), MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            char dllNameBuf[256] = {};
            SendMessageA(componentCombo, CB_GETLBTEXT, sel, (LPARAM)dllNameBuf);

            char ownerBuf[256] = {};
            GetWindowTextA(GetDlgItem(hDlg, IDC_RM_OWNER_EDIT), ownerBuf, sizeof(ownerBuf));

            char repoBuf[256] = {};
            GetWindowTextA(GetDlgItem(hDlg, IDC_RM_REPO_EDIT), repoBuf, sizeof(repoBuf));

            if (ownerBuf[0] == '\0' || repoBuf[0] == '\0') {
                MessageBox(hDlg, _T("Please fill in both GitHub Owner and Repo."), _T("Repository Mapping"), MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            RepositoryMappingEntry entry;
            entry.dllName = dllNameBuf;
            entry.owner = ownerBuf;
            entry.repo = repoBuf;
            upsertRepositoryMappingEntry(entry);

            RefreshEntryListBox(hDlg);
            return TRUE;
        }

        if (LOWORD(wp) == IDC_RM_REMOVE_BTN) {
            HWND listBox = GetDlgItem(hDlg, IDC_RM_LIST);
            int sel = (int)SendMessage(listBox, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) {
                MessageBox(hDlg, _T("Please select an entry to remove."), _T("Repository Mapping"), MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            auto entries = loadRepositoryMapping();
            if (sel >= 0 && (size_t)sel < entries.size()) {
                removeRepositoryMappingEntry(entries[sel].dllName);
                RefreshEntryListBox(hDlg);
            }
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

void ShowRepositoryMappingDialog(HWND parent) {
    struct {
        DLGTEMPLATE dlg;
        WORD menu = 0;
        WORD wclass = 0;
        WCHAR title[24] = L"Repository Mapping";
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
        (DLGPROC)RepositoryMappingDialogProc, 0);
}
