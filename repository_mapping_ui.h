#pragma once

#include <windows.h>

// repository_mapping_ui.cppで実装されているダイアログを、
// Preferencesページ(preferences_page.cpp)からボタン経由で呼び出せるように公開する。
void ShowRepositoryMappingDialog(HWND parent);
