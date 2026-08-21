#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"
#include "SDK-2025-03-07/foobar2000/SDK/coreDarkMode.h"
#include "repository_mapping.h"
#include "repository_mapping_ui.h"
#include "remote_registry.h"

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

#include <vector>
#include <string>
#include <algorithm>

// ------------------------------------------------------------------
// Manage Component Sourcesダイアログ(旧称: Repository Mapping)
//
// foo_albumtrainの設定ダイアログと同じ方式(.rcファイルを使わず、
// DialogBoxIndirectParam + 実行時のCreateWindowExでコントロールを組み立てる)
// を踏襲している。
//
// レイアウト:
//   [Component:      [ドロップダウン(導入済みコンポーネント一覧)]]
//   [Page URL:        [Edit(GitHub/GitLab/Codeberg/marc2k3.github.io/
//                            SourceForge/foobar.hyv.fi いずれかのURLを貼り付ける)]]
//   [Save] [Suggest for Shared Registry...]
//   [登録済み一覧(ListBox)]
//   [Remove Selected]
//   [Close]
//
// URLは source ごとに owner/repo(github/gitlab/codeberg)または url(marc2k3)
// へ解析してから保存する(TryParseRepositoryUrl)。owner/repoモデルの保存形式
// 自体は従来通りowner/repo文字列のままなので、Remote Registryとの互換性に
// 影響しない。
// ------------------------------------------------------------------

namespace {

// コントロールID(このダイアログ内のみで完結する値なので、他のダイアログとの
// 重複は気にしなくてよい)
const int IDC_RM_COMPONENT_COMBO = 2001;
const int IDC_RM_URL_EDIT = 2002;
const int IDC_RM_SAVE_BTN = 2004;
const int IDC_RM_LIST = 2005;
const int IDC_RM_REMOVE_BTN = 2006;
const int IDC_RM_SUGGEST_BTN = 2007;

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
        if (e.source == "marc2k3" || e.source == "sourceforge" || e.source == "hyv") {
            // urlモデル: owner/repoが空なので、代わりに登録ページURLを表示する。
            line << e.dllName.c_str() << "  ->  " << e.url.c_str();
        } else {
            line << e.dllName.c_str() << "  ->  " << e.owner.c_str() << "/" << e.repo.c_str();
        }
        SendMessageA(listBox, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
}

// UTF-8のstd::stringをUnicodeテキストとしてクリップボードへコピーする。
bool CopyTextToClipboard(HWND hDlg, std::string const& utf8Text) {
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), -1, NULL, 0);
    if (wideLen <= 0) return false;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wideLen * sizeof(wchar_t));
    if (!hMem) return false;

    wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
    MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), -1, pMem, wideLen);
    GlobalUnlock(hMem);

    if (!OpenClipboard(hDlg)) {
        GlobalFree(hMem);
        return false;
    }

    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    // hMemの所有権はクリップボードに渡ったのでGlobalFreeしない。

    return true;
}

std::string TrimWhitespace(std::string s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// リポジトリURL(様々な表記ゆれ)から source を判定し、
//   - owner/repoモデル(github/gitlab/codeberg) の場合は outOwner/outRepo
//   - urlモデル(marc2k3) の場合は outUrl
// を埋める。対応例:
//   https://github.com/owner/repo
//   https://gitlab.com/owner/repo
//   https://codeberg.org/owner/repo
//   https://marc2k3.github.io/component/xxx/
//   (github/gitlab/codebergはhttp/https有無、末尾に/releases等が付いていても、
//    .gitサフィックスが付いていても可)
//
// 現時点でこれらのいずれとも解釈できない入力は、誤登録を避けるため
// 素直に失敗を返す(DP-0009: 誤判定するくらいなら明示的にエラーにする)。
// GitLabのグループ/サブグループ(owner部分に複数階層のパスを持つ場合)は
// 現時点では未対応。
bool TryParseRepositoryUrl(
    std::string url, std::string& outSource, std::string& outOwner,
    std::string& outRepo, std::string& outUrl
) {
    url = TrimWhitespace(url);
    if (url.empty()) return false;

    // marc2k3.github.io: owner/repoモデルではなく、コンポーネント個別ページの
    // URLをそのまま識別子として保持する特別扱い。
    if (url.find("marc2k3.github.io/") != std::string::npos) {
        std::string normalized = url;

        // クエリ文字列・フラグメントを切り落とす
        size_t cutPos = normalized.find_first_of("?#");
        if (cutPos != std::string::npos) normalized = normalized.substr(0, cutPos);

        // スキームが無ければ https:// を補う
        if (normalized.compare(0, 8, "https://") != 0 &&
            normalized.compare(0, 7, "http://") != 0) {
            normalized = "https://" + normalized;
        }

        outSource = "marc2k3";
        outOwner = "";
        outRepo = "";
        outUrl = normalized;
        return true;
    }

    // sourceforge.net: marc2k3と同様にurlモデル。ただしこちらはSourceForge
    // というプラットフォーム自体の共通機能(RSSフィード)を使うため、特定の
    // プロジェクトに限らず汎用的に対応できる。登録はプロジェクトのファイル
    // 一覧ページ(フォルダ)のURLで行う。
    if (url.find("sourceforge.net/projects/") != std::string::npos) {
        std::string normalized = url;

        size_t cutPos = normalized.find_first_of("?#");
        if (cutPos != std::string::npos) normalized = normalized.substr(0, cutPos);

        if (normalized.compare(0, 8, "https://") != 0 &&
            normalized.compare(0, 7, "http://") != 0) {
            normalized = "https://" + normalized;
        }

        // 末尾にスラッシュが無ければ揃えておく(フォルダページである前提のため)。
        if (normalized.back() != '/') normalized += "/";

        outSource = "sourceforge";
        outOwner = "";
        outRepo = "";
        outUrl = normalized;
        return true;
    }

    // foobar.hyv.fi: marc2k3と同様、このサイト専用のurlモデル(Case氏の
    // 個人配布サイト)。ただしクエリパラメータ(?view=<component>)自体が
    // コンポーネントの識別子なので、marc2k3/sourceforgeと違いクエリ文字列は
    // 切り落とさず残す(フラグメントのみ切り落とす)。
    if (url.find("foobar.hyv.fi/") != std::string::npos) {
        std::string normalized = url;

        size_t fragPos = normalized.find('#');
        if (fragPos != std::string::npos) normalized = normalized.substr(0, fragPos);

        if (normalized.compare(0, 8, "https://") != 0 &&
            normalized.compare(0, 7, "http://") != 0) {
            normalized = "https://" + normalized;
        }

        outSource = "hyv";
        outOwner = "";
        outRepo = "";
        outUrl = normalized;
        return true;
    }

    struct SiteMarker {
        const char* marker;
        const char* source;
    };
    static const SiteMarker sites[] = {
        { "github.com/", "github" },
        { "gitlab.com/", "gitlab" },
        { "codeberg.org/", "codeberg" },
    };

    const char* matchedSource = nullptr;
    size_t pos = std::string::npos;
    size_t markerLen = 0;

    for (auto const& site : sites) {
        size_t p = url.find(site.marker);
        if (p != std::string::npos) {
            matchedSource = site.source;
            pos = p;
            markerLen = std::string(site.marker).length();
            break;
        }
    }

    if (matchedSource == nullptr) return false;

    std::string rest = url.substr(pos + markerLen);

    // クエリ文字列・フラグメントを切り落とす
    size_t cutPos = rest.find_first_of("?#");
    if (cutPos != std::string::npos) rest = rest.substr(0, cutPos);

    // owner/repo/(残り、あれば無視) に分割
    size_t slashPos = rest.find('/');
    if (slashPos == std::string::npos) return false; // owner だけではリポジトリが特定できない

    std::string owner = rest.substr(0, slashPos);
    std::string repoRest = rest.substr(slashPos + 1);

    size_t secondSlashPos = repoRest.find('/');
    std::string repo = (secondSlashPos == std::string::npos) ? repoRest : repoRest.substr(0, secondSlashPos);

    // ".git" サフィックスを除去
    const std::string gitSuffix = ".git";
    if (repo.size() > gitSuffix.size() &&
        repo.compare(repo.size() - gitSuffix.size(), gitSuffix.size(), gitSuffix) == 0) {
        repo = repo.substr(0, repo.size() - gitSuffix.size());
    }

    if (owner.empty() || repo.empty()) return false;

    outSource = matchedSource;
    outOwner = owner;
    outRepo = repo;
    outUrl = ""; // owner/repoベースのサイトではurlは使わない
    return true;
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
        // Save + Suggest for Shared Registry... ボタンが横に並ぶため、
        // 通常のフィールド幅より広く必要になる。
        const int BUTTON_ROW_W = 80 + 8 + 180;
        const int CLIENT_W = FIELD_X + (std::max)(FIELD_W, BUTTON_ROW_W) + MARGIN;

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

        // ---- Page URL ----
        CreateWindowEx(0, _T("STATIC"), _T("Page URL:"),
            WS_CHILD | WS_VISIBLE,
            MARGIN, y + 2, LABEL_W, labelH, hDlg, NULL, NULL, NULL);

        CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), NULL,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            FIELD_X, y, FIELD_W, editH, hDlg, (HMENU)(INT_PTR)IDC_RM_URL_EDIT,
            NULL, NULL);

        y += rowH + 4;

        // ---- Save / Suggest for Shared Registry ----
        const int BTN_W = 80;
        CreateWindowEx(0, _T("BUTTON"), _T("Save"),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            FIELD_X, y, BTN_W, editH, hDlg, (HMENU)(INT_PTR)IDC_RM_SAVE_BTN,
            NULL, NULL);

        const int SUGGEST_BTN_W = 180;
        CreateWindowEx(0, _T("BUTTON"), _T("Suggest for Shared Registry..."),
            WS_CHILD | WS_VISIBLE,
            FIELD_X + BTN_W + 8, y, SUGGEST_BTN_W, editH, hDlg, (HMENU)(INT_PTR)IDC_RM_SUGGEST_BTN,
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

        ApplyDialogFontToChildren(hDlg);

        RefreshEntryListBox(hDlg);

        // lpにはShowRepositoryMappingDialog()側で確保したCCoreDarkModeHooksへの
        // ポインタが渡ってくる(呼び出し元のスタックフレームがダイアログの
        // 表示中ずっと生きているので、ここで参照しても問題ない)。
        if (lp) {
            ((fb2k::CCoreDarkModeHooks*)lp)->AddDialogWithControls(hDlg);
        }

        return TRUE;
    }

    case WM_COMMAND: {
        if (LOWORD(wp) == IDC_RM_SAVE_BTN) {
            HWND componentCombo = GetDlgItem(hDlg, IDC_RM_COMPONENT_COMBO);
            int sel = (int)SendMessage(componentCombo, CB_GETCURSEL, 0, 0);
            if (sel == CB_ERR) {
                MessageBox(hDlg, _T("Please select a component."), _T("Manage Component Sources"), MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            char dllNameBuf[256] = {};
            SendMessageA(componentCombo, CB_GETLBTEXT, sel, (LPARAM)dllNameBuf);

            char urlBuf[512] = {};
            GetWindowTextA(GetDlgItem(hDlg, IDC_RM_URL_EDIT), urlBuf, sizeof(urlBuf));

            std::string source, owner, repo, url;
            if (!TryParseRepositoryUrl(urlBuf, source, owner, repo, url)) {
                MessageBox(hDlg,
                    _T("Please paste a page URL from a supported site, e.g.\n")
                    _T("https://github.com/owner/repo\n")
                    _T("https://gitlab.com/owner/repo\n")
                    _T("https://codeberg.org/owner/repo\n")
                    _T("https://marc2k3.github.io/component/xxx/\n")
                    _T("https://sourceforge.net/projects/<project>/files/<folder>/\n")
                    _T("https://foobar.hyv.fi/?view=<component>"),
                    _T("Manage Component Sources"), MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            RepositoryMappingEntry entry;
            entry.dllName = dllNameBuf;
            entry.source = source;
            entry.owner = owner;
            entry.repo = repo;
            entry.url = url;
            upsertRepositoryMappingEntry(entry);

            RefreshEntryListBox(hDlg);
            return TRUE;
        }

        if (LOWORD(wp) == IDC_RM_REMOVE_BTN) {
            HWND listBox = GetDlgItem(hDlg, IDC_RM_LIST);
            int sel = (int)SendMessage(listBox, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) {
                MessageBox(hDlg, _T("Please select an entry to remove."), _T("Manage Component Sources"), MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            auto entries = loadRepositoryMapping();
            if (sel >= 0 && (size_t)sel < entries.size()) {
                removeRepositoryMappingEntry(entries[sel].dllName);
                RefreshEntryListBox(hDlg);
            }
            return TRUE;
        }

        if (LOWORD(wp) == IDC_RM_SUGGEST_BTN) {
            HWND componentCombo = GetDlgItem(hDlg, IDC_RM_COMPONENT_COMBO);
            int sel = (int)SendMessage(componentCombo, CB_GETCURSEL, 0, 0);
            if (sel == CB_ERR) {
                MessageBox(hDlg, _T("Please select a component."), _T("Suggest for Shared Registry"), MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            char dllNameBuf[256] = {};
            SendMessageA(componentCombo, CB_GETLBTEXT, sel, (LPARAM)dllNameBuf);

            char urlBuf[512] = {};
            GetWindowTextA(GetDlgItem(hDlg, IDC_RM_URL_EDIT), urlBuf, sizeof(urlBuf));

            std::string source, owner, repo, url;
            if (!TryParseRepositoryUrl(urlBuf, source, owner, repo, url)) {
                MessageBox(hDlg,
                    _T("Please paste a page URL from a supported site first, e.g.\n")
                    _T("https://github.com/owner/repo\n")
                    _T("https://gitlab.com/owner/repo\n")
                    _T("https://codeberg.org/owner/repo\n")
                    _T("https://marc2k3.github.io/component/xxx/\n")
                    _T("https://sourceforge.net/projects/<project>/files/<folder>/\n")
                    _T("https://foobar.hyv.fi/?view=<component>"),
                    _T("Suggest for Shared Registry"), MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            // known_components.json の "components" 配列にそのまま貼り付けられる形の
            // JSONスニペットを組み立てる。source によって owner/repo か url かが
            // 変わる(repository_mapping.h / remote_registry.h と同じモデル)。
            std::string snippet;
            snippet += "    {\n";
            snippet += "      \"dll\": \"" + std::string(dllNameBuf) + "\",\n";
            snippet += "      \"source\": \"" + source + "\",\n";
            if (source == "marc2k3" || source == "sourceforge" || source == "hyv") {
                snippet += "      \"url\": \"" + url + "\"\n";
            } else {
                snippet += "      \"owner\": \"" + owner + "\",\n";
                snippet += "      \"repo\": \"" + repo + "\"\n";
            }
            snippet += "    },";

            bool copied = CopyTextToClipboard(hDlg, snippet);

            MessageBox(hDlg,
                copied
                    ? _T("A registry entry has been copied to your clipboard.\n\n")
                      _T("Your browser will now open the shared registry file on GitHub. ")
                      _T("Paste the entry into the \"components\" array, then click ")
                      _T("\"Propose changes\" (or \"Commit changes\") to open a pull request.")
                    : _T("Could not copy to clipboard. Your browser will still open the ")
                      _T("registry file; you can copy the entry manually."),
                _T("Suggest for Shared Registry"), MB_OK);

            std::string editUrl = std::string("https://github.com/") + k_registryRepoOwner
                + "/" + k_registryRepoName + "/edit/main/" + k_registryFilePath;
            ShellExecuteA(hDlg, "open", editUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);

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
        WCHAR title[32] = L"Manage Component Sources";
        // DS_SHELLFONT指定時は、フォールバック用のpointsize/typefaceを
        // 続けて格納しておく必要がある(無いとテーマフォントが正しく
        // 適用されず、大きい既定フォントで表示されてしまう)。
        // DS_SHELLFONTなので実際にはシステムのシェルフォント(Segoe UI等)に
        // 置き換えられるが、この値は互換性のためのフォールバックとして必要。
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

    fb2k::CCoreDarkModeHooks darkMode;

    DialogBoxIndirectParam(
        NULL, (LPCDLGTEMPLATE)&dlgData, parent,
        (DLGPROC)RepositoryMappingDialogProc, (LPARAM)&darkMode);
}
