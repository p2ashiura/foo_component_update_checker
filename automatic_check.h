#pragma once

// 自動確認の設定値(cfg_var)へのアクセスを、他のファイル(設定ダイアログ等)へ
// 公開するためのヘッダー。実体はautomatic_check.cppにある。

bool GetAutomaticCheckEnabled();
void SetAutomaticCheckEnabled(bool enabled);

// 単位: 日数。1未満は指定しないこと(呼び出し側でバリデーションする想定)。
int GetAutomaticCheckIntervalDays();
void SetAutomaticCheckIntervalDays(int days);

// 自動確認で、何が見つかったときにポップアップを出すか。
enum class AutoCheckNotificationLevel {
    UpdatesOnly = 0,      // 既定: 更新が見つかったときだけ
    UpdatesAndErrors = 1, // 更新に加えて、通信失敗などのエラーがあったときも
    Always = 2,           // 何も無くても(Up to dateのみでも)必ず表示する
};

AutoCheckNotificationLevel GetAutomaticCheckNotificationLevel();
void SetAutomaticCheckNotificationLevel(AutoCheckNotificationLevel level);
