#pragma once

// The opt-in CYD build carries both languages. Existing builds stay English.
#if defined(BAMBUHELPER_ZH_CN)
extern bool uiChinese;
#define BH_TEXT(english, chinese) (uiChinese ? chinese : english)
#else
#define BH_TEXT(english, chinese) english
#endif
