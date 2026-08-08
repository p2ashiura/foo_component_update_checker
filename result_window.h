#pragma once

#include "update_check.h"

#include <windows.h>
#include <vector>

// 更新確認の結果を、コンソールではなくポップアップウィンドウ(ListView)で表示する。
// 各行を選択して「Open Release Page」、またはダブルクリックで、
// そのコンポーネントのリリースページを既定ブラウザで開ける。
//
// resultsが空の場合は、ウィンドウの代わりにemptyMessageをMessageBoxで表示する。
// メインスレッドから呼ぶこと。
void ShowUpdateResultWindow(HWND parent, std::vector<CheckResult> const& results, const char* emptyMessage);
