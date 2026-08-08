// print_error error text table - GENERATED, DO NOT EDIT.
// Regenerate with: python tools/gen_error_tables.py
//
// Source: https://e.bambulab.com/query.php?lang=en
// Feed ver: 202608050942
// 539 codes, 324 unique strings, blank intros dropped.
//
// Keys are sorted ascending for binary search. Text is ASCII-folded and
// truncated to 200 chars; see the generator for the exact rules.
#ifndef PRINT_ERROR_TABLE_H
#define PRINT_ERROR_TABLE_H

#include <Arduino.h>

#define PRINT_ERROR_TABLE_VER   "202608050942"
#define PRINT_ERROR_TABLE_COUNT 539
#define PRINT_ERROR_TEXT_COUNT  324

static const uint32_t PRINT_ERROR_KEYS[PRINT_ERROR_TABLE_COUNT] PROGMEM = {
  0x03004000UL, 0x03004001UL, 0x03004002UL, 0x03004005UL, 0x03004006UL, 0x03004008UL, 0x03004009UL, 0x0300400AUL,
  0x0300400BUL, 0x0300400CUL, 0x0300400DUL, 0x0300400EUL, 0x03004013UL, 0x03004014UL, 0x03004020UL, 0x03004066UL,
  0x03004067UL, 0x03004090UL, 0x03008000UL, 0x03008001UL, 0x03008002UL, 0x03008003UL, 0x03008004UL, 0x03008005UL,
  0x03008006UL, 0x03008007UL, 0x03008008UL, 0x03008009UL, 0x0300800AUL, 0x0300800BUL, 0x0300800CUL, 0x0300800DUL,
  0x0300800EUL, 0x0300800FUL, 0x03008010UL, 0x03008011UL, 0x03008013UL, 0x03008014UL, 0x03008016UL, 0x03008017UL,
  0x03008018UL, 0x03008019UL, 0x0300806EUL, 0x0300806FUL, 0x05004001UL, 0x05004002UL, 0x05004003UL, 0x05004004UL,
  0x05004005UL, 0x05004006UL, 0x05004007UL, 0x05004008UL, 0x05004009UL, 0x0500400AUL, 0x0500400BUL, 0x0500400CUL,
  0x0500400DUL, 0x0500400EUL, 0x0500400FUL, 0x05004010UL, 0x05004011UL, 0x05004012UL, 0x05004013UL, 0x05004014UL,
  0x05004015UL, 0x05004016UL, 0x05004017UL, 0x05004018UL, 0x05004019UL, 0x0500401AUL, 0x0500401BUL, 0x0500401CUL,
  0x0500401DUL, 0x0500401EUL, 0x0500401FUL, 0x05004020UL, 0x05004021UL, 0x05004022UL, 0x05004023UL, 0x05004024UL,
  0x05004025UL, 0x05004026UL, 0x05004027UL, 0x05004028UL, 0x05004029UL, 0x0500402AUL, 0x0500402BUL, 0x0500402CUL,
  0x0500402DUL, 0x0500402EUL, 0x0500402FUL, 0x05004030UL, 0x05004031UL, 0x05004033UL, 0x05004037UL, 0x05004038UL,
  0x0500403CUL, 0x0500403FUL, 0x05004040UL, 0x05004041UL, 0x05004042UL, 0x05004043UL, 0x05004057UL, 0x05004095UL,
  0x05004098UL, 0x0500409DUL, 0x050040A3UL, 0x050040A6UL, 0x050040A8UL, 0x050040B4UL, 0x050040C0UL, 0x05008013UL,
  0x05008036UL, 0x0500803CUL, 0x05008040UL, 0x05008041UL, 0x05008057UL, 0x0500809BUL, 0x0500C010UL, 0x0500C04AUL,
  0x0500C04BUL, 0x0500C04CUL, 0x0500C04DUL, 0x0500C04EUL, 0x0500C04FUL, 0x05014017UL, 0x05014018UL, 0x05014019UL,
  0x0501401AUL, 0x0501401BUL, 0x0501401CUL, 0x0501401DUL, 0x0501401EUL, 0x0501401FUL, 0x05014020UL, 0x05014021UL,
  0x05014022UL, 0x05014023UL, 0x05014024UL, 0x05014025UL, 0x05014026UL, 0x05014027UL, 0x05014028UL, 0x05014029UL,
  0x05014031UL, 0x05014032UL, 0x05014033UL, 0x05014034UL, 0x05014035UL, 0x05014038UL, 0x05014039UL, 0x05014098UL,
  0x0501409DUL, 0x050140A3UL, 0x05024001UL, 0x05024004UL, 0x0502400DUL, 0x0502400FUL, 0x05024010UL, 0x05024011UL,
  0x05024027UL, 0x0502402DUL, 0x0502402EUL, 0x0502402FUL, 0x05024030UL, 0x05024098UL, 0x0502409DUL, 0x050240A3UL,
  0x0502C012UL, 0x0502C014UL, 0x0502C024UL, 0x0502C026UL, 0x0502C028UL, 0x0502C031UL, 0x0502C032UL, 0x0502C03AUL,
  0x0502C03DUL, 0x0502C03EUL, 0x0502C03FUL, 0x0502C042UL, 0x0502C043UL, 0x0502C046UL, 0x05034098UL, 0x0503409DUL,
  0x050340A3UL, 0x05804096UL, 0x0580409CUL, 0x058040A2UL, 0x05814096UL, 0x0581409CUL, 0x058140A2UL, 0x05824096UL,
  0x0582409CUL, 0x058240A2UL, 0x05834096UL, 0x0583409CUL, 0x058340A2UL, 0x05844096UL, 0x0584409CUL, 0x058440A2UL,
  0x05854096UL, 0x0585409CUL, 0x058540A2UL, 0x05864096UL, 0x0586409CUL, 0x058640A2UL, 0x05874096UL, 0x0587409CUL,
  0x058740A2UL, 0x07004001UL, 0x07004025UL, 0x07008001UL, 0x07008002UL, 0x07008003UL, 0x07008004UL, 0x07008005UL,
  0x07008006UL, 0x07008007UL, 0x07008010UL, 0x07008011UL, 0x07008012UL, 0x07008013UL, 0x07008016UL, 0x07008017UL,
  0x07008023UL, 0x0700C008UL, 0x0700C069UL, 0x0700C06AUL, 0x0700C06BUL, 0x0700C06CUL, 0x0700C06DUL, 0x0700C06EUL,
  0x07014001UL, 0x07014025UL, 0x07018001UL, 0x07018002UL, 0x07018003UL, 0x07018004UL, 0x07018005UL, 0x07018006UL,
  0x07018007UL, 0x07018010UL, 0x07018011UL, 0x07018012UL, 0x07018013UL, 0x07018016UL, 0x07018017UL, 0x07018023UL,
  0x0701C008UL, 0x0701C069UL, 0x0701C06AUL, 0x0701C06BUL, 0x0701C06CUL, 0x0701C06DUL, 0x0701C06EUL, 0x07024001UL,
  0x07024025UL, 0x07028001UL, 0x07028002UL, 0x07028003UL, 0x07028004UL, 0x07028005UL, 0x07028006UL, 0x07028007UL,
  0x07028010UL, 0x07028011UL, 0x07028012UL, 0x07028013UL, 0x07028016UL, 0x07028017UL, 0x07028023UL, 0x0702C008UL,
  0x0702C069UL, 0x0702C06AUL, 0x0702C06BUL, 0x0702C06CUL, 0x0702C06DUL, 0x0702C06EUL, 0x07034001UL, 0x07034025UL,
  0x07038001UL, 0x07038002UL, 0x07038003UL, 0x07038004UL, 0x07038005UL, 0x07038006UL, 0x07038007UL, 0x07038010UL,
  0x07038011UL, 0x07038012UL, 0x07038013UL, 0x07038016UL, 0x07038017UL, 0x07038023UL, 0x0703C008UL, 0x0703C069UL,
  0x0703C06AUL, 0x0703C06BUL, 0x0703C06CUL, 0x0703C06DUL, 0x0703C06EUL, 0x07044025UL, 0x07048003UL, 0x07048004UL,
  0x07048005UL, 0x07048006UL, 0x07048007UL, 0x07048010UL, 0x07048011UL, 0x07048012UL, 0x07048016UL, 0x07048023UL,
  0x0704C008UL, 0x07054025UL, 0x07058003UL, 0x07058004UL, 0x07058005UL, 0x07058006UL, 0x07058007UL, 0x07058010UL,
  0x07058011UL, 0x07058012UL, 0x07058016UL, 0x07058023UL, 0x0705C008UL, 0x07064025UL, 0x07068003UL, 0x07068004UL,
  0x07068005UL, 0x07068006UL, 0x07068007UL, 0x07068010UL, 0x07068011UL, 0x07068012UL, 0x07068016UL, 0x07068023UL,
  0x0706C008UL, 0x07074025UL, 0x07078003UL, 0x07078004UL, 0x07078005UL, 0x07078006UL, 0x07078007UL, 0x07078010UL,
  0x07078011UL, 0x07078012UL, 0x07078016UL, 0x07078023UL, 0x0707C008UL, 0x07FE8030UL, 0x07FEC030UL, 0x07FF4001UL,
  0x07FF8001UL, 0x07FF8002UL, 0x07FF8003UL, 0x07FF8004UL, 0x07FF8005UL, 0x07FF8006UL, 0x07FF8007UL, 0x07FF8010UL,
  0x07FF8011UL, 0x07FF8012UL, 0x07FF8013UL, 0x07FF8030UL, 0x07FFC003UL, 0x07FFC006UL, 0x07FFC008UL, 0x07FFC009UL,
  0x07FFC00AUL, 0x07FFC030UL, 0x0C00402CUL, 0x0C00402DUL, 0x0C008001UL, 0x0C008005UL, 0x0C008009UL, 0x0C00C003UL,
  0x0C00C004UL, 0x0C00C006UL, 0x1000C001UL, 0x1000C002UL, 0x1000C003UL, 0x10014001UL, 0x10014002UL, 0x10018003UL,
  0x10018004UL, 0x18004025UL, 0x18008003UL, 0x18008004UL, 0x18008005UL, 0x18008006UL, 0x18008007UL, 0x18008010UL,
  0x18008011UL, 0x18008012UL, 0x18008016UL, 0x18008017UL, 0x18008023UL, 0x1800C008UL, 0x1800C069UL, 0x1800C06AUL,
  0x1800C06BUL, 0x1800C06CUL, 0x1800C06DUL, 0x1800C06EUL, 0x18014025UL, 0x18018003UL, 0x18018004UL, 0x18018005UL,
  0x18018006UL, 0x18018007UL, 0x18018010UL, 0x18018011UL, 0x18018012UL, 0x18018016UL, 0x18018017UL, 0x18018023UL,
  0x1801C008UL, 0x1801C069UL, 0x1801C06AUL, 0x1801C06BUL, 0x1801C06CUL, 0x1801C06DUL, 0x1801C06EUL, 0x18024025UL,
  0x18028003UL, 0x18028004UL, 0x18028005UL, 0x18028006UL, 0x18028007UL, 0x18028010UL, 0x18028011UL, 0x18028012UL,
  0x18028016UL, 0x18028017UL, 0x18028023UL, 0x1802C008UL, 0x1802C069UL, 0x1802C06AUL, 0x1802C06BUL, 0x1802C06CUL,
  0x1802C06DUL, 0x1802C06EUL, 0x18034025UL, 0x18038003UL, 0x18038004UL, 0x18038005UL, 0x18038006UL, 0x18038007UL,
  0x18038010UL, 0x18038011UL, 0x18038012UL, 0x18038016UL, 0x18038017UL, 0x18038023UL, 0x1803C008UL, 0x1803C069UL,
  0x1803C06AUL, 0x1803C06BUL, 0x1803C06CUL, 0x1803C06DUL, 0x1803C06EUL, 0x18044025UL, 0x18048003UL, 0x18048004UL,
  0x18048005UL, 0x18048006UL, 0x18048007UL, 0x18048010UL, 0x18048011UL, 0x18048012UL, 0x18048016UL, 0x18048017UL,
  0x18048023UL, 0x1804C008UL, 0x1804C069UL, 0x1804C06AUL, 0x1804C06BUL, 0x1804C06CUL, 0x1804C06DUL, 0x1804C06EUL,
  0x18054025UL, 0x18058003UL, 0x18058004UL, 0x18058005UL, 0x18058006UL, 0x18058007UL, 0x18058010UL, 0x18058011UL,
  0x18058012UL, 0x18058016UL, 0x18058017UL, 0x18058023UL, 0x1805C008UL, 0x1805C069UL, 0x1805C06AUL, 0x1805C06BUL,
  0x1805C06CUL, 0x1805C06DUL, 0x1805C06EUL, 0x18064025UL, 0x18068003UL, 0x18068004UL, 0x18068005UL, 0x18068006UL,
  0x18068007UL, 0x18068010UL, 0x18068011UL, 0x18068012UL, 0x18068016UL, 0x18068017UL, 0x18068023UL, 0x1806C008UL,
  0x1806C069UL, 0x1806C06AUL, 0x1806C06BUL, 0x1806C06CUL, 0x1806C06DUL, 0x1806C06EUL, 0x18074025UL, 0x18078003UL,
  0x18078004UL, 0x18078005UL, 0x18078006UL, 0x18078007UL, 0x18078010UL, 0x18078011UL, 0x18078012UL, 0x18078016UL,
  0x18078017UL, 0x18078023UL, 0x1807C008UL, 0x1807C069UL, 0x1807C06AUL, 0x1807C06BUL, 0x1807C06CUL, 0x1807C06DUL,
  0x1807C06EUL, 0x18FF8005UL, 0x18FF8012UL,
};

static const uint16_t PRINT_ERROR_TEXT_ID[PRINT_ERROR_TABLE_COUNT] PROGMEM = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
  12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
  24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 3, 34,
  35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46,
  47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
  59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
  71, 69, 72, 73, 71, 69, 73, 68, 69, 73, 74, 69,
  70, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
  86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
  98, 99, 100, 32, 101, 86, 102, 103, 104, 105, 106, 107,
  108, 109, 110, 111, 112, 65, 66, 67, 68, 69, 70, 71,
  69, 72, 73, 71, 69, 73, 68, 69, 73, 74, 69, 70,
  113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124,
  125, 126, 127, 128, 129, 130, 130, 130, 131, 132, 133, 134,
  135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146,
  147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158,
  159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170,
  171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182,
  183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194,
  195, 196, 197, 198, 199, 177, 178, 179, 180, 181, 182, 183,
  184, 185, 186, 187, 188, 189, 200, 201, 192, 202, 203, 204,
  205, 206, 207, 199, 177, 178, 179, 180, 181, 182, 183, 184,
  185, 186, 187, 188, 189, 208, 209, 192, 210, 211, 212, 213,
  214, 215, 199, 177, 178, 179, 180, 181, 182, 183, 184, 185,
  186, 187, 188, 189, 216, 217, 192, 218, 219, 220, 221, 222,
  223, 177, 180, 181, 182, 183, 184, 185, 186, 187, 189, 224,
  192, 177, 180, 181, 182, 183, 184, 185, 186, 187, 189, 225,
  192, 177, 180, 181, 182, 183, 184, 185, 186, 187, 189, 226,
  192, 177, 180, 181, 182, 183, 184, 185, 186, 187, 189, 227,
  192, 228, 228, 199, 178, 179, 229, 230, 231, 232, 233, 234,
  235, 187, 188, 228, 236, 232, 237, 232, 238, 228, 239, 240,
  241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252,
  253, 177, 180, 254, 255, 256, 184, 257, 258, 187, 189, 259,
  260, 192, 261, 262, 263, 264, 265, 266, 177, 180, 254, 255,
  256, 184, 257, 258, 187, 189, 267, 268, 192, 269, 270, 271,
  272, 273, 274, 177, 180, 254, 255, 256, 184, 257, 258, 187,
  189, 275, 276, 192, 277, 278, 279, 280, 281, 282, 177, 180,
  254, 255, 256, 184, 257, 258, 187, 189, 283, 284, 192, 285,
  286, 287, 288, 289, 290, 177, 180, 254, 255, 256, 184, 257,
  258, 187, 189, 291, 292, 192, 293, 294, 295, 296, 297, 298,
  177, 180, 254, 255, 256, 184, 257, 258, 187, 189, 299, 300,
  192, 301, 302, 303, 304, 305, 306, 177, 180, 254, 255, 256,
  184, 257, 258, 187, 189, 307, 308, 192, 309, 310, 311, 312,
  313, 314, 177, 180, 254, 255, 256, 184, 257, 258, 187, 189,
  315, 316, 192, 317, 318, 319, 320, 321, 322, 323, 187,
};

static const uint32_t PRINT_ERROR_TEXT_OFF[PRINT_ERROR_TEXT_COUNT] PROGMEM = {
  0, 49, 122, 175, 217, 240, 275, 298, 352, 385,
  408, 440, 469, 519, 574, 652, 692, 734, 802, 891,
  970, 1100, 1291, 1335, 1446, 1615, 1764, 1868, 1900, 2000,
  2111, 2305, 2431, 2523, 2574, 2686, 2755, 2952, 3079, 3199,
  3232, 3345, 3493, 3638, 3710, 3765, 3862, 3970, 4033, 4150,
  4227, 4310, 4363, 4436, 4539, 4595, 4645, 4669, 4751, 4827,
  4934, 5019, 5111, 5199, 5326, 5396, 5459, 5527, 5595, 5792,
  5908, 6023, 6182, 6382, 6494, 6652, 6842, 6935, 7133, 7150,
  7285, 7452, 7521, 7614, 7701, 7818, 7932, 8095, 8163, 8275,
  8325, 8517, 8605, 8744, 8826, 8913, 9041, 9145, 9292, 9428,
  9527, 9660, 9737, 9840, 9946, 10139, 10264, 10344, 10456, 10592,
  10660, 10777, 10858, 10925, 11123, 11316, 11451, 11599, 11683, 11777,
  11829, 11916, 12044, 12148, 12225, 12367, 12437, 12541, 12620, 12819,
  12957, 13058, 13150, 13237, 13365, 13469, 13496, 13580, 13747, 13855,
  13934, 14044, 14173, 14253, 14332, 14376, 14437, 14547, 14679, 14727,
  14814, 14942, 15046, 15139, 15270, 15364, 15457, 15588, 15682, 15775,
  15906, 16000, 16093, 16224, 16318, 16411, 16542, 16636, 16729, 16860,
  16954, 17047, 17178, 17272, 17365, 17496, 17590, 17748, 17789, 17842,
  17906, 18038, 18159, 18347, 18507, 18565, 18659, 18734, 18800, 18896,
  19037, 19116, 19232, 19357, 19437, 19508, 19584, 19659, 19749, 19834,
  19978, 20057, 20173, 20253, 20324, 20400, 20475, 20565, 20650, 20729,
  20845, 20925, 20996, 21072, 21147, 21237, 21322, 21401, 21517, 21597,
  21668, 21744, 21819, 21909, 21994, 22110, 22226, 22342, 22458, 22603,
  22798, 22914, 23040, 23120, 23273, 23332, 23391, 23589, 23783, 23939,
  23990, 24061, 24168, 24256, 24303, 24354, 24497, 24551, 24648, 24715,
  24813, 24891, 24963, 25087, 25229, 25353, 25544, 25707, 25804, 25885,
  25967, 26086, 26169, 26243, 26322, 26403, 26496, 26584, 26666, 26785,
  26868, 26942, 27021, 27102, 27195, 27283, 27365, 27484, 27567, 27641,
  27720, 27801, 27894, 27982, 28064, 28183, 28266, 28340, 28419, 28500,
  28593, 28681, 28763, 28882, 28965, 29039, 29118, 29199, 29292, 29380,
  29462, 29581, 29664, 29738, 29817, 29898, 29991, 30079, 30161, 30280,
  30363, 30437, 30516, 30597, 30690, 30778, 30860, 30979, 31062, 31136,
  31215, 31296, 31389, 31477,
};

static const char PRINT_ERROR_TEXT_BLOB[] PROGMEM =
  "Z axis homing failed; the task has been stopped." "\0"
  "The printer timed out waiting for the nozzle to cool down before homing." "\0"
  "Auto Bed Leveling failed; the task has been stopped." "\0"
  "The hotend cooling fan speed is abnormal." "\0"
  "The nozzle is clogged." "\0"
  "The AMS failed to change filament." "\0"
  "Homing XY axis failed." "\0"
  "Mechanical resonance frequency identification failed." "\0"
  "Internal communication exception" "\0"
  "The task was canceled." "\0"
  "Resume failed after power loss." "\0"
  "The motor self-check failed." "\0"
  "Printing cannot be initiated while AMS is drying." "\0"
  "Homing Z axis failed: temperature control abnormality." "\0"
  "The nozzle presence detection failed. Please check the Assistant for details." "\0"
  "Calibration of motion precision failed." "\0"
  "Calibration result is over the threshold." "\0"
  "Print command timed out due to a communication issue. Please retry." "\0"
  "Printing was paused for unknown reason. You can select \"Resume\" to resume the print job." "\0"
  "Printing was paused by the user. You can select \"Resume\" to continue printing." "\0"
  "First layer defects were detected by the Micro Lidar. Please check the quality of the printed model before continuing your print." "\0"
  "Spaghetti defects were detected by the AI Print Monitoring. Please check the quality of the printed model before continuing your print. Cleaning the build plate or drying the filament can..." "\0"
  "Filament ran out. Please load new filament." "\0"
  "Toolhead front cover fell off. Please remount the front cover and check to make sure your print is going okay." "\0"
  "The build plate marker was not detected. Please confirm the build plate is correctly positioned on the heatbed with all four corners aligned, and the marker is visible." "\0"
  "There was an unfinished print job when the printer lost power. If the model is still adhered to the build plate, you can try resuming the print job." "\0"
  "Nozzle temperature malfunction. Check the Assistant page and resolve the issue before retrying heating." "\0"
  "Heatbed temperature malfunction" "\0"
  "A Filament pile-up was detected by AI Print Monitoring. Please clean filament from the waste chute." "\0"
  "The cutter is stuck. Please make sure the cutter handle is out and check the filament sensor cable connection." "\0"
  "Multiple motion steps lost detected. Please check if the toolhead is blocked by obstructions or making abnormal noise. You may continue printing to observe. If the print shows layer shifting..." "\0"
  "Detected that the extruder is not extruding normally. If the defects are acceptable, select \"Resume\" to resume the print job." "\0"
  "The print file is not available. Please check to see if the storage media has been removed." "\0"
  "The door seems to be open, so printing was paused." "\0"
  "Detected build plate is not the same as the Gcode file. Please adjust slicer settings or use the correct plate." "\0"
  "Printing paused due to the pause command added to the printing file." "\0"
  "The nozzle is covered with filament, or the build plate is installed incorrectly. Please cancel this print and clean the nozzle or adjust the build plate according to the actual status. You can..." "\0"
  "The nozzle is clogged with filament. Please cancel this print and clean the nozzle or select \"Resume\" to resume the print job." "\0"
  "Foreign objects detected on heatbed. Please check and clean the heatbed. Then, select \"Resume\" to resume the print job." "\0"
  "Chamber temperature malfunction." "\0"
  "Build plate not detected. Please ensure there are no bulges or debris on the plate and that the nozzle is clean." "\0"
  "Abnormal nozzle temperature control detected; the heating module may be damaged. Please disconnect the power immediately and stop using the device." "\0"
  "Abnormal temperature rise detected on the heatbed. The heating module may be damaged. Please power off the device immediately and stop using it." "\0"
  "Failed to connect to Bambu Cloud. Please check your network connection." "\0"
  "Unsupported file path or name. Please resend the task." "\0"
  "Printing stopped because the printer was unable to parse the file. Please resend your print job." "\0"
  "Device is busy and cannot start new task. Please wait for current task to complete before sending new task." "\0"
  "Print jobs are not allowed to be sent while updating firmware." "\0"
  "There is not enough free storage space for the print job. Restoring to factory settings can free up available space." "\0"
  "The device requires a repair upgrade, and printing is currently unavailable." "\0"
  "Starting printing failed; please power cycle the printer and resend the print job." "\0"
  "Jobs are not allowed to be sent while updating logs." "\0"
  "The file name is not supported. Please rename and restart the print job." "\0"
  "There was a problem downloading a file. Please check your network connection and resend the print job." "\0"
  "Please insert a MicroSD card and restart the print job." "\0"
  "Please run a self-test and restart the print job." "\0"
  "Printing was cancelled." "\0"
  "AMS is initializing and cannot be upgraded at the moment. Please try again later." "\0"
  "AMS is drying and cannot be upgraded at the moment. Please try again later." "\0"
  "The printer is loading or unloading filament and cannot be upgraded at the moment. Please try again later." "\0"
  "The device is printing and cannot be upgraded at the moment. Please try again later." "\0"
  "AMS is in operation and cannot be upgraded at the moment. Please try again when it is idle." "\0"
  "Slicing for the print job failed. Please check your settings and restart the print job." "\0"
  "There is not enough free storage space for the print job. Please format or clear files from the MicroSD card to free up space." "\0"
  "The MicroSD Card is write-protected. Please replace the MicroSD Card." "\0"
  "Binding failed. Please retry or restart the printer and retry." "\0"
  "Binding configuration information parsing failed; please try again." "\0"
  "The printer has already been bound. Please unbind it and try again." "\0"
  "Cloud access failed. Possible reasons include network instability caused by interference, inability to access the internet, or router firewall configuration restrictions. You can try moving the..." "\0"
  "Cloud response is invalid. If you have tried multiple times and are still failing, please contact customer support." "\0"
  "Cloud access is rejected. If you have tried multiple times and are still failing, please contact customer support." "\0"
  "Cloud access failed, which may be caused by network instability due to interference. You can try moving the printer closer to the router before you try again." "\0"
  "Authorization timed out. Please make sure that your phone or PC has access to the internet, and ensure that the Bambu Studio/Bambu Handy APP is running in the foreground during the binding operation." "\0"
  "Cloud access rejected. If you have tried multiple times and are still failing, please contact customer support." "\0"
  "Cloud access failed; this may be caused by network instability due to interference. You can try moving the printer closer to the router before you try again." "\0"
  "Failed to connect to the router, which may be caused by wireless interference or being too far away from the router. Please try again or move the printer closer to the router and try again." "\0"
  "Router connection failed due to incorrect password. Please check the password and try again." "\0"
  "Failed to obtain IP address, which may be caused by wireless interference resulting in data transmission failure or the DHCP address pool of the router being full. Please move the printer closer..." "\0"
  "System exception" "\0"
  "The system does not support the file system currently used by the MicroSD card. Please replace the MicroSD card or format it to FAT32." "\0"
  "The MicroSD card sector data is damaged. Please use the SD card repair tool to repair or format it. If it still cannot be identified, please replace the MicroSD card." "\0"
  "The device is currently upgrading. Please try again when it is idle." "\0"
  "The accessory firmware does not match the printer. Please upgrade it on the \"Firmware\" page." "\0"
  "The AMS firmware does not match the printer. Please upgrade it on the \"Firmware\" page." "\0"
  "The preset device model in the sliced file is incompatible with the current device model. The job cannot be started." "\0"
  "The nozzle diameter in sliced file is not consistent with the current nozzle setting. This file can't be printed." "\0"
  "The current nozzle setting does not match the slicing file. Continuing to print may affect print quality. It is recommended to re-slice before starting the print." "\0"
  "Failed to download print job; please check your network connection." "\0"
  "The printer has reached its power limit. Please connect a dedicated power adapter to this AMS to enable drying." "\0"
  "The AMS drying cannot be started during printing." "\0"
  "Printing and calibration cannot be performed while the AMS is drying. Please stop the drying process or connect a power adapter to any AMS unit not used for the current print, then try again." "\0"
  "Due to power limitations, only one AMS is allowed to use the device's power for drying." "\0"
  "The filament selected in the slicer requires a harder nozzle. Please replace the nozzle or adjust the filament settings before reprinting." "\0"
  "No print plate detected. Please place it correctly and recalibrate on the device." "\0"
  "The device cannot detect AMS A. Please reconnect the AMS cable or restart the printer." "\0"
  "The firmware of AMS A does not match the printer; the device cannot continue working. Please upgrade it on the \"Firmware\" page." "\0"
  "AMS(or AMS lite) A communication is abnormal. Please reconnect the module cable or restart the printer." "\0"
  "File download failed due to missing certificates. Please check the Fleet Hub certificate configuration and restart the printer before trying again" "\0"
  "The device firmware requires a repair upgrade, and the current operation cannot be performed. Please upgrade it on the \"Firmware\" page." "\0"
  "Task execution error. Please restart the device. If the issue persists, contact technical support." "\0"
  "Communication error detected with AMS, AMS lite or AMS HT. Please reconnect the module cable or restart the printer when it is idle." "\0"
  "Your sliced file is not consistent with the current printer model. Continue?" "\0"
  "Toolhead front cover is detached. Moving the toolhead may damage the printer. Do you want to continue?" "\0"
  "The filament in hotend is too cold. Extrusion may damage the extruder. Still feeding in/out the filament?" "\0"
  "The filament hardness selected in the slicer exceeds the current nozzle hardness. Continuing the print may cause nozzle wear, leading to leakage and unstable flow. Please proceed with caution." "\0"
  "Build plate not properly positioned, may collide with the waste chute. Please reposition build plate and align with heatbed." "\0"
  "MicroSD Card read/write exception: please reinsert or replace the MicroSD Card." "\0"
  "AMS is calibrating, reading RFID or loading/unloading material, unable to initiate drying process, please wait." "\0"
  "Filament in AMS outlet, the high drying temperature may cause AMS blockage. Drying cannot be started. Please unload the filament first." "\0"
  "The AMS is currently drying. Please do not start the process again." "\0"
  "The device is currently in laser or cutting mode and cannot start drying. Please do not initiate the drying process." "\0"
  "Please connect a power adapter to the AMS-HT before starting the drying process." "\0"
  "The AMS is drying and cannot perform this operation at the moment." "\0"
  "Device discovery binding is in progress, and the QR code cannot be displayed on the screen. You can wait for the binding to finish or abort the device discovery binding process in the APP/Studio..." "\0"
  "QR code binding is in progress, so device discovery binding cannot be performed. You can scan the QR code on the screen for binding or exit the QR code display page on screen and try device..." "\0"
  "Your APP region does not match with your printer; please download the APP in the corresponding region and register your account again." "\0"
  "The slicing progress has not been updated for a long time, and the printing task has exited. Please confirm the parameters and reinitiate printing." "\0"
  "The device is in the process of binding and cannot respond to new binding requests." "\0"
  "The regional settings do not match the printer; please check the printer's regional settings." "\0"
  "Device login has expired; please try to bind again." "\0"
  "The device cannot detect AMS B. Please reconnect the AMS cable or restart the printer." "\0"
  "The firmware of AMS B does not match the printer; the device cannot continue working. Please upgrade it on the \"Firmware\" page." "\0"
  "AMS(or AMS lite) B communication is abnormal. Please reconnect the module cable or restart the printer." "\0"
  "Current filament will be used in this print job. Settings cannot be changed." "\0"
  "Some features are not supported by the current device. Please check the Studio feature settings or update the firmware to the latest version." "\0"
  "Failed to start a new task: filament loading/unloading not completed." "\0"
  "Printer incompatible with current slicing software. Download the latest Bambu Studio from bambulab.com." "\0"
  "Printer incompatible with sliced file. Re-slice using the latest Bambu Studio." "\0"
  "Authorized slicing feature detected. Ensure all required authorizations are obtained before use. Bambu Lab cannot authorize such use of the product and assumes no liability for any use undertaken..." "\0"
  "The current AMS does not support drying while printing. Please connect to the network and update the AMS firmware on the \"Firmware\" page." "\0"
  "Printing stopped because the printer was unable to parse the 3mf file. Please resend your print job." "\0"
  "Failed to parse the G-code file. The task has been stopped. Please restart the G-code task." "\0"
  "The device cannot detect AMS C. Please reconnect the AMS cable or restart the printer." "\0"
  "The firmware of AMS C does not match the printer; the device cannot continue working. Please upgrade it on the \"Firmware\" page." "\0"
  "AMS(or AMS lite) C communication is abnormal. Please reconnect the module cable or restart the printer." "\0"
  "The task cannot be paused." "\0"
  "The AMS Remaining Filament Estimation is enabled by default and cannot be disabled." "\0"
  "The flow dynamic calibration records have exceeded the storage limit. Please delete some historical records in the slicer software before adding new calibration data." "\0"
  "The device is busy with the current task and cannot perform this operation for now. Please try again later." "\0"
  "The filament currently loaded in the extruder does not support manual feeding." "\0"
  "Please check and remove any printed parts or debris from the heatbed surface before continuing the cold pull." "\0"
  "Please check and remove any printed parts or debris from the heatbed surface and underside before continuing the drying process." "\0"
  "Device is busy. Free move cannot be enabled. Try again when the device is idle." "\0"
  "Hotend temperature below 170degC. Extrusion or retraction control unavailable." "\0"
  "Device busy. Cannot set hotend temperature." "\0"
  "No available hotend. Nozzle offset calibration cannot start." "\0"
  "Please check and remove any printed parts or debris from the Heatbed surface before continuing the cold pull." "\0"
  "Please check and remove any printed objects or foreign matter from the surface and bottom of the Heatbed, then proceed with drying." "\0"
  "Current hotend type does not support cold pull." "\0"
  "The device cannot detect AMS D. Please reconnect the AMS cable or restart the printer." "\0"
  "The firmware of AMS D does not match the printer; the device cannot continue working. Please upgrade it on the \"Firmware\" page." "\0"
  "AMS(or AMS lite) D communication is abnormal. Please reconnect the module cable or restart the printer." "\0"
  "The device cannot detect AMS-HT A. Please reconnect the AMS-HT cable or restart the printer." "\0"
  "The firmware of AMS-HT A does not match the printer; the device cannot continue working. Please upgrade it on the \"Firmware\" page." "\0"
  "AMS-HT A communication is abnormal. Please reconnect the module cable or restart the printer." "\0"
  "The device cannot detect AMS-HT B. Please reconnect the AMS-HT cable or restart the printer." "\0"
  "The firmware of AMS-HT B does not match the printer; the device cannot continue working. Please upgrade it on the \"Firmware\" page." "\0"
  "AMS-HT B communication is abnormal. Please reconnect the module cable or restart the printer." "\0"
  "The device cannot detect AMS-HT C. Please reconnect the AMS-HT cable or restart the printer." "\0"
  "The firmware of AMS-HT C does not match the printer; the device cannot continue working. Please upgrade it on the \"Firmware\" page." "\0"
  "AMS-HT C communication is abnormal. Please reconnect the module cable or restart the printer." "\0"
  "The device cannot detect AMS-HT D. Please reconnect the AMS-HT cable or restart the printer." "\0"
  "The firmware of AMS-HT D does not match the printer; the device cannot continue working. Please upgrade it on the \"Firmware\" page." "\0"
  "AMS-HT D communication is abnormal. Please reconnect the module cable or restart the printer." "\0"
  "The device cannot detect AMS-HT E. Please reconnect the AMS-HT cable or restart the printer." "\0"
  "The firmware of AMS-HT E does not match the printer; the device cannot continue working. Please upgrade it on the \"Firmware\" page." "\0"
  "AMS-HT E communication is abnormal. Please reconnect the module cable or restart the printer." "\0"
  "The device cannot detect AMS-HT F. Please reconnect the AMS-HT cable or restart the printer." "\0"
  "The firmware of AMS-HT F does not match the printer; the device cannot continue working. Please upgrade it on the \"Firmware\" page." "\0"
  "AMS-HT F communication is abnormal. Please reconnect the module cable or restart the printer." "\0"
  "The device cannot detect AMS-HT G. Please reconnect the AMS-HT cable or restart the printer." "\0"
  "The firmware of AMS-HT G does not match the printer; the device cannot continue working. Please upgrade it on the \"Firmware\" page." "\0"
  "AMS-HT G communication is abnormal. Please reconnect the module cable or restart the printer." "\0"
  "The device cannot detect AMS-HT H. Please reconnect the AMS-HT cable or restart the printer." "\0"
  "The firmware of AMS-HT H does not match the printer; the device cannot continue working. Please upgrade it on the \"Firmware\" page." "\0"
  "AMS-HT H communication is abnormal. Please reconnect the module cable or restart the printer." "\0"
  "The AMS has been disabled for a print, but it still has filament loaded. Please unload the AMS filament and switch to the spool holder filament for printing." "\0"
  "Failed to read the filament information." "\0"
  "Failed to cut the filament. Please check the cutter." "\0"
  "The cutter is stuck. Please make sure the cutter handle is out." "\0"
  "Failed to pull out the filament from the extruder. This might be caused by clogged extruder or filament broken inside the extruder." "\0"
  "AMS failed to pull back filament. This could be due to a stuck spool or the end of the filament being stuck in the path." "\0"
  "The AMS failed to send out filament. You can clip the end of your filament flat, and reinsert. If this message persists, please check the PTFE tubes in AMS for any signs of wear and tear." "\0"
  "Unable to feed filament into the extruder. This could be due to an entangled filament or a stuck spool. If not, please check if the AMS PTFE tube is connected." "\0"
  "Extruding filament failed. The extruder might be clogged." "\0"
  "The AMS assist motor is overloaded. This could be due to entangled filament or a stuck spool." "\0"
  "AMS filament ran out. Please insert a new filament into the same AMS slot." "\0"
  "Failed to get AMS mapping table; please select \"Resume\" to retry." "\0"
  "Timeout purging old filament: Please check if the filament is stuck or the extruder is clogged." "\0"
  "The extruder is not extruding normally; please refer to the Assistant. After trouble shooting. If the defects are acceptable, please resume." "\0"
  "AMS A is drying. Please stop drying process before loading/unloading material." "\0"
  "AMS A cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "AMS used for the current print is disconnected. Check the connection. Printing will resume automatically after reconnection." "\0"
  "An error occurred during AMS A drying. Please go to Assistant for more details." "\0"
  "AMS A is reading RFID. Unable to start drying. Please try again later." "\0"
  "AMS A is changing filament. Unable to start drying. Please try again later." "\0"
  "AMS A is in Feed Assist Mode. Please unload filament and try drying again." "\0"
  "AMS A is assisting in filament insertion. Unable to start drying. Please try again later." "\0"
  "AMS A motor is performing self-test. Unable to start drying. Please try again later." "\0"
  "Filament is still loaded from the AMS after it has been disabled. Please unload the filament, load from the spool holder, and restart printing." "\0"
  "AMS B is drying. Please stop drying process before loading/unloading material." "\0"
  "AMS B cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "An error occurred during AMS B drying. Please go to Assistant for more details." "\0"
  "AMS B is reading RFID. Unable to start drying. Please try again later." "\0"
  "AMS B is changing filament. Unable to start drying. Please try again later." "\0"
  "AMS B is in Feed Assist Mode. Please unload filament and try drying again." "\0"
  "AMS B is assisting in filament insertion. Unable to start drying. Please try again later." "\0"
  "AMS B motor is performing self-test. Unable to start drying. Please try again later." "\0"
  "AMS C is drying. Please stop drying process before loading/unloading material." "\0"
  "AMS C cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "An error occurred during AMS C drying. Please go to Assistant for more details." "\0"
  "AMS C is reading RFID. Unable to start drying. Please try again later." "\0"
  "AMS C is changing filament. Unable to start drying. Please try again later." "\0"
  "AMS C is in Feed Assist Mode. Please unload filament and try drying again." "\0"
  "AMS C is assisting in filament insertion. Unable to start drying. Please try again later." "\0"
  "AMS C motor is performing self-test. Unable to start drying. Please try again later." "\0"
  "AMS D is drying. Please stop drying process before loading/unloading material." "\0"
  "AMS D cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "An error occurred during AMS D drying. Please go to Assistant for more details." "\0"
  "AMS D is reading RFID. Unable to start drying. Please try again later." "\0"
  "AMS D is changing filament. Unable to start drying. Please try again later." "\0"
  "AMS D is in Feed Assist Mode. Please unload filament and try drying again." "\0"
  "AMS D is assisting in filament insertion. Unable to start drying. Please try again later." "\0"
  "AMS D motor is performing self-test. Unable to start drying. Please try again later." "\0"
  "AMS E cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "AMS F cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "AMS G cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "AMS H cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "The filament specified in the slicer has been used up. Printing is paused. Please go to the machine to replace the material and resume printing." "\0"
  "Please pull out the filament on the spool holder. If this message persists, please check to see if there is filament broken in the extruder. (Connect a PTFE tube if you are about to use an AMS.)" "\0"
  "Failed to pull back the filament from the toolhead to AMS. Please check whether the filament or the spool is stuck." "\0"
  "Failed to feed the filament outside the AMS. Please clip the end of the filament flat and check to see if the spool is stuck." "\0"
  "Please feed filament into the PTFE tube until it can not be pushed any farther." "\0"
  "Please observe the nozzle. If the filament has been extruded, select \"Done\"; if not, please push the filament forward slightly, and then select \"Retry\"." "\0"
  "Check if the external filament spool or filament is stuck." "\0"
  "External filament has run out; please load a new filament." "\0"
  "Please pull out the filament on the spool holder. If this message persists, please check to see if there is filament broken in the extruder or PTFE tube. (Connect a PTFE tube if you are about to..." "\0"
  "Please pull out the filament on the spool holder. If this message persists, please check to see if there is filament broken in the extruder. (Connect a PTFE tube if you are about to use an AMS)" "\0"
  "Please observe the nozzle. If the filament has been extruded, select \"Continue\"; if not, please push the filament forward slightly and then select \"Retry\"." "\0"
  "Device data link error. Please reboot the printer." "\0"
  "The toolhead camera is not working properly; please reboot the device." "\0"
  "First layer defects were detected. If the defects are acceptable, select \"Resume\" to resume the print job." "\0"
  "Purged filament has piled up in the waste chute, which may cause a tool head collision." "\0"
  "Build plate localization marker was not found." "\0"
  "Possible defects were detected in the first layer." "\0"
  "Possible spaghetti failure was detected. Cleaning the build plate or drying the filament can effectively reduce the risk of spaghetti failure." "\0"
  "Purged filament may have piled up in the waste chute." "\0"
  "High bed temperature may lead to filament clogging in the nozzle. You may open the chamber door." "\0"
  "Printing CF material with stainless steel may cause nozzle damage." "\0"
  "Enabling Timelapse in traditional mode may cause defects; please activate this feature as needed." "\0"
  "Timelapse is not supported as Spiral Vase mode is enabled in slicing presets." "\0"
  "Timelapse is not supported as the Print sequence is set to \"By object\"." "\0"
  "The time-lapse mode is set to Traditional in the slicing file. This may cause surface defects. Would you like to enable it?" "\0"
  "Prime Tower is not enabled and time-lapse mode is set to Smooth in slicing file. This may cause surface defects. Would you like to enable it?" "\0"
  "AMS-HT failed to pull back filament. This could be due to a stuck spool or the end of the filament being stuck in the path." "\0"
  "The AMS-HT failed to send out filament. You can clip the end of your filament flat, and reinsert. If this message persists, please check the PTFE tubes in AMS for any signs of wear and tear." "\0"
  "Unable to feed filament into the extruder. This could be due to an entangled filament or a stuck spool. If not, please check if the AMS-HT PTFE tube is connected." "\0"
  "The AMS-HT assist motor is overloaded. This could be due to entangled filament or a stuck spool." "\0"
  "AMS-HT filament ran out. Please insert a new filament into the same AMS-HT slot." "\0"
  "AMS-HT A is drying. Please stop drying process before loading/unloading material." "\0"
  "AMS-HT A cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "An error occurred during AMS-HT A drying. Please go to Assistant for more details." "\0"
  "AMS-HT A is reading RFID. Unable to start drying. Please try again later." "\0"
  "AMS-HT A is changing filament. Unable to start drying. Please try again later." "\0"
  "AMS-HT A is in Feed Assist Mode. Unable to start drying. Please try again later." "\0"
  "AMS-HT A is assisting in filament insertion. Unable to start drying. Please try again later." "\0"
  "AMS-HT A motor is performing self-test. Unable to start drying. Please try again later." "\0"
  "AMS-HT B is drying. Please stop drying process before loading/unloading material." "\0"
  "AMS-HT B cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "An error occurred during AMS-HT B drying. Please go to Assistant for more details." "\0"
  "AMS-HT B is reading RFID. Unable to start drying. Please try again later." "\0"
  "AMS-HT B is changing filament. Unable to start drying. Please try again later." "\0"
  "AMS-HT B is in Feed Assist Mode. Unable to start drying. Please try again later." "\0"
  "AMS-HT B is assisting in filament insertion. Unable to start drying. Please try again later." "\0"
  "AMS-HT B motor is performing self-test. Unable to start drying. Please try again later." "\0"
  "AMS-HT C is drying. Please stop drying process before loading/unloading material." "\0"
  "AMS-HT C cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "An error occurred during AMS-HT C drying. Please go to Assistant for more details." "\0"
  "AMS-HT C is reading RFID. Unable to start drying. Please try again later." "\0"
  "AMS-HT C is changing filament. Unable to start drying. Please try again later." "\0"
  "AMS-HT C is in Feed Assist Mode. Unable to start drying. Please try again later." "\0"
  "AMS-HT C is assisting in filament insertion. Unable to start drying. Please try again later." "\0"
  "AMS-HT C motor is performing self-test. Unable to start drying. Please try again later." "\0"
  "AMS-HT D is drying. Please stop drying process before loading/unloading material." "\0"
  "AMS-HT D cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "An error occurred during AMS-HT D drying. Please go to Assistant for more details." "\0"
  "AMS-HT D is reading RFID. Unable to start drying. Please try again later." "\0"
  "AMS-HT D is changing filament. Unable to start drying. Please try again later." "\0"
  "AMS-HT D is in Feed Assist Mode. Unable to start drying. Please try again later." "\0"
  "AMS-HT D is assisting in filament insertion. Unable to start drying. Please try again later." "\0"
  "AMS-HT D motor is performing self-test. Unable to start drying. Please try again later." "\0"
  "AMS-HT E is drying. Please stop drying process before loading/unloading material." "\0"
  "AMS-HT E cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "An error occurred during AMS-HT E drying. Please go to Assistant for more details." "\0"
  "AMS-HT E is reading RFID. Unable to start drying. Please try again later." "\0"
  "AMS-HT E is changing filament. Unable to start drying. Please try again later." "\0"
  "AMS-HT E is in Feed Assist Mode. Unable to start drying. Please try again later." "\0"
  "AMS-HT E is assisting in filament insertion. Unable to start drying. Please try again later." "\0"
  "AMS-HT E motor is performing self-test. Unable to start drying. Please try again later." "\0"
  "AMS-HT F is drying. Please stop drying process before loading/unloading material." "\0"
  "AMS-HT F cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "An error occurred during AMS-HT F drying. Please go to Assistant for more details." "\0"
  "AMS-HT F is reading RFID. Unable to start drying. Please try again later." "\0"
  "AMS-HT F is changing filament. Unable to start drying. Please try again later." "\0"
  "AMS-HT F is in Feed Assist Mode. Unable to start drying. Please try again later." "\0"
  "AMS-HT F is assisting in filament insertion. Unable to start drying. Please try again later." "\0"
  "AMS-HT F motor is performing self-test. Unable to start drying. Please try again later." "\0"
  "AMS-HT G is drying. Please stop drying process before loading/unloading material." "\0"
  "AMS-HT G cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "An error occurred during AMS-HT G drying. Please go to Assistant for more details." "\0"
  "AMS-HT G is reading RFID. Unable to start drying. Please try again later." "\0"
  "AMS-HT G is changing filament. Unable to start drying. Please try again later." "\0"
  "AMS-HT G is in Feed Assist Mode. Unable to start drying. Please try again later." "\0"
  "AMS-HT G is assisting in filament insertion. Unable to start drying. Please try again later." "\0"
  "AMS-HT G motor is performing self-test. Unable to start drying. Please try again later." "\0"
  "AMS-HT H is drying. Please stop drying process before loading/unloading material." "\0"
  "AMS-HT H cooling failed. The ambient temperature may be too high. Please operate the device in a suitable environment." "\0"
  "An error occurred during AMS-HT H drying. Please go to Assistant for more details." "\0"
  "AMS-HT H is reading RFID. Unable to start drying. Please try again later." "\0"
  "AMS-HT H is changing filament. Unable to start drying. Please try again later." "\0"
  "AMS-HT H is in Feed Assist Mode. Unable to start drying. Please try again later." "\0"
  "AMS-HT H is assisting in filament insertion. Unable to start drying. Please try again later." "\0"
  "AMS-HT H motor is performing self-test. Unable to start drying. Please try again later." "\0"
  "Failed to feed the filament outside the AMS-HT. Please clip the end of the filament flat and check to see if the spool is stuck.";

#endif  // PRINT_ERROR_TABLE_H
