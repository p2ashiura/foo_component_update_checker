#pragma once

// 自動確認の設定値(cfg_var)へのアクセスを、他のファイル(設定ダイアログ等)へ
// 公開するためのヘッダー。実体はautomatic_check.cppにある。

bool GetAutomaticCheckEnabled();
void SetAutomaticCheckEnabled(bool enabled);

// 単位: 日数。1未満は指定しないこと(呼び出し側でバリデーションする想定)。
int GetAutomaticCheckIntervalDays();
void SetAutomaticCheckIntervalDays(int days);
