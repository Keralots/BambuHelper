#pragma once

// Select the localized firmware at build time; existing environments stay English.
#if defined(BAMBUHELPER_ZH_CN)
#define BH_TEXT(english, chinese) chinese
#else
#define BH_TEXT(english, chinese) english
#endif
