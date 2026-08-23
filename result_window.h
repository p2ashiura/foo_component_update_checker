#pragma once

#include "update_check.h"

#include <windows.h>
#include <vector>

// 更新確認の結果を、コンソールではなくポップアップウィンドウ(ListView)で表示する。
// 各行を選択して「Open Release Page」、またはダブルクリックで、
// そのコンポーネントのリリースページを既定ブラウザで開ける。
//
// installedを渡すと、ダイアログ表示と同時にバックグラウンドで更新確認を開始する
// (呼び出し元は結果を待たずに済む)。確認中は進捗表示、完了後は結果一覧に
// 自動的に切り替わる。結果が空だった場合も、ウィンドウは閉じずemptyMessageを
// 表示に切り替える(MessageBoxへの切り替えは行わない)。
// メインスレッドから呼ぶこと。
//
// 既に結果ウィンドウが開いている場合、新しくは開かず既存のウィンドウを
// 前面に出すだけにする(結果ウィンドウは同時に1つまで)。
void ShowUpdateResultWindow(HWND parent, std::vector<InstalledComponentInfo> const& installed, const char* emptyMessage);

// 自動確認用。検査は呼び出し元で既に完了しているため、resultsをそのまま渡す
// (進捗表示は行わず、開いた瞬間から結果一覧が表示される)。
//
// 既に結果ウィンドウが開いている場合(手動確認中、または前回の自動確認の
// 結果が残っている場合)、Non-Intrusiveの原則により今回の表示は諦める
// (前面に出す等もしない。次回の定期確認に委ねる)。
// メインスレッドから呼ぶこと。
void ShowUpdateResultWindowWithResults(HWND parent, std::vector<CheckResult> const& results, const char* emptyMessage);
