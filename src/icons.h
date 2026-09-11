#ifndef ICONS_H
#define ICONS_H

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "layout.h"   // LY_ICON16

// 16x16 1-bit icons stored as PROGMEM byte arrays (1 bit per pixel).
// Draw with: for each bit, if set draw accentColor, else skip (transparent).
// Row-major, MSB first. 16 pixels wide = 2 bytes per row, 16 rows = 32 bytes.

// Nozzle / hotend icon (16x16)
const uint8_t PROGMEM icon_nozzle[] = {
  0x03, 0xC0,  //     ####
  0x07, 0xE0,  //    ######
  0x0F, 0xF0,  //   ########
  0x0F, 0xF0,  //   ########
  0x0F, 0xF0,  //   ########
  0x07, 0xE0,  //    ######
  0x07, 0xE0,  //    ######
  0x0F, 0xF0,  //   ########
  0x1F, 0xF8,  //  ##########
  0x1F, 0xF8,  //  ##########
  0x0F, 0xF0,  //   ########
  0x07, 0xE0,  //    ######
  0x03, 0xC0,  //     ####
  0x01, 0x80,  //      ##
  0x01, 0x80,  //      ##
  0x00, 0x00,  //
};

// Bed / platform icon (16x16)
const uint8_t PROGMEM icon_bed[] = {
  0x00, 0x00,  //
  0x00, 0x00,  //
  0x00, 0x00,  //
  0x00, 0x00,  //
  0x00, 0x00,  //
  0x00, 0x00,  //
  0x00, 0x00,  //
  0x7F, 0xFE,  //  ##############
  0xFF, 0xFF,  // ################
  0xFF, 0xFF,  // ################
  0x7F, 0xFE,  //  ##############
  0x30, 0x0C,  //   ##        ##
  0x30, 0x0C,  //   ##        ##
  0x30, 0x0C,  //   ##        ##
  0x78, 0x1E,  //  ####      ####
  0x00, 0x00,  //
};

// Fan icon (16x16)
const uint8_t PROGMEM icon_fan[] = {
  0x00, 0x00,  //
  0x03, 0x80,  //      ###
  0x07, 0xC0,  //     #####
  0x07, 0xC0,  //     #####
  0x03, 0xC0,  //      ####
  0x03, 0xE0,  //      #####
  0x71, 0xF0,  //  ###   #####
  0xF9, 0x9F,  // #####  ##  #####
  0xF9, 0x9F,  // #####  ##  #####
  0x0F, 0x8E,  //     #####   ###
  0x07, 0xC0,  //     #####
  0x03, 0xC0,  //      ####
  0x03, 0xE0,  //      #####
  0x03, 0xE0,  //      #####
  0x01, 0xC0,  //       ###
  0x00, 0x00,  //
};

// Clock icon (16x16)
const uint8_t PROGMEM icon_clock[] = {
  0x07, 0xE0,  //      ######
  0x1F, 0xF8,  //    ##########
  0x3F, 0xFC,  //   ############
  0x38, 0x1C,  //   ###      ###
  0x71, 0x8E,  //  ###  ##   ###
  0x61, 0x86,  //  ##   ##    ##
  0xE1, 0x87,  // ###   ##    ###
  0xC1, 0x83,  // ##    ##     ##
  0xC1, 0xF3,  // ##    #######
  0xC0, 0x73,  // ##      ###  ##
  0xE0, 0x07,  // ###        ###
  0x60, 0x06,  //  ##        ##
  0x70, 0x0E,  //  ###      ###
  0x3F, 0xFC,  //   ############
  0x1F, 0xF8,  //    ##########
  0x07, 0xE0,  //      ######
};

// Layer / stack icon (16x16)
const uint8_t PROGMEM icon_layers[] = {
  0x00, 0x00,  //
  0x01, 0x80,  //       ##
  0x07, 0xE0,  //      ######
  0x1F, 0xF8,  //    ##########
  0x7F, 0xFE,  //  ##############
  0x1F, 0xF8,  //    ##########
  0x07, 0xE0,  //      ######
  0x1F, 0xF8,  //    ##########
  0x7F, 0xFE,  //  ##############
  0x1F, 0xF8,  //    ##########
  0x07, 0xE0,  //      ######
  0x1F, 0xF8,  //    ##########
  0x7F, 0xFE,  //  ##############
  0x1F, 0xF8,  //    ##########
  0x07, 0xE0,  //      ######
  0x00, 0x00,  //
};

// WiFi signal icon (16x16) - solid arcs with square dot
const uint8_t PROGMEM icon_wifi[] = {
  0x03, 0xC0,  //       ####
  0x0F, 0xF0,  //     ########
  0x3F, 0xFC,  //   ############
  0x78, 0x1E,  //  ####      ####
  0x60, 0x06,  //  ##          ##
  0x0F, 0xF0,  //     ########
  0x1F, 0xF8,  //    ##########
  0x38, 0x1C,  //   ###      ###
  0x10, 0x08,  //    #        #
  0x07, 0xE0,  //      ######
  0x07, 0xE0,  //      ######
  0x01, 0x80,  //       ##
  0x03, 0xC0,  //      ####
  0x03, 0xC0,  //      ####
  0x03, 0xC0,  //      ####
  0x00, 0x00,  //
};

// Padlock closed (16x16)
const uint8_t PROGMEM icon_lock[] = {
  0x00, 0x00,  //
  0x07, 0xE0,  //      ######
  0x0C, 0x30,  //     ##    ##
  0x18, 0x18,  //    ##      ##
  0x18, 0x18,  //    ##      ##
  0x18, 0x18,  //    ##      ##
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x00, 0x00,  //
  0x00, 0x00,  //
};

// Padlock open (16x16)
const uint8_t PROGMEM icon_unlock[] = {
  0x07, 0xE0,  //      ######
  0x0C, 0x30,  //     ##    ##
  0x18, 0x18,  //    ##      ##
  0x00, 0x18,  //            ##
  0x00, 0x18,  //            ##
  0x00, 0x18,  //            ##
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x3F, 0xFC,  //   ############
  0x00, 0x00,  //
  0x00, 0x00,  //
};

// Checkmark icon (16x16)
const uint8_t PROGMEM icon_check[] = {
  0x00, 0x00,  //
  0x00, 0x00,  //
  0x00, 0x06,  //              ##
  0x00, 0x0E,  //             ###
  0x00, 0x1C,  //            ###
  0x00, 0x38,  //           ###
  0x00, 0x70,  //          ###
  0x00, 0xE0,  //         ###
  0xC1, 0xC0,  // ##     ###
  0xE3, 0x80,  // ###   ###
  0x77, 0x00,  //  ### ###
  0x3E, 0x00,  //   #####
  0x1C, 0x00,  //    ###
  0x08, 0x00,  //     #
  0x00, 0x00,  //
  0x00, 0x00,  //
};

// File / gcode icon (16x16)
const uint8_t PROGMEM icon_file[] = {
  0x1F, 0x80,  //    ######
  0x10, 0xC0,  //    #    ##
  0x10, 0x60,  //    #     ##
  0x10, 0x30,  //    #      ##
  0x10, 0x1E,  //    #     ####
  0x10, 0x02,  //    #        #
  0x10, 0x02,  //    #        #
  0x10, 0x02,  //    #        #
  0x17, 0xC2,  //    # #####  #
  0x17, 0xC2,  //    # #####  #
  0x10, 0x02,  //    #        #
  0x17, 0x82,  //    # ####   #
  0x17, 0x82,  //    # ####   #
  0x10, 0x02,  //    #        #
  0x1F, 0xFE,  //    ##########
  0x00, 0x00,  //
};

// Lightning bolt icon (16x16) - power monitoring indicator
const uint8_t PROGMEM icon_lightning[] = {
  0x00, 0x00,  //
  0x00, 0x00,  //
  0x07, 0x80,  //     .....####.......  top diagonal (cols 5-8)
  0x0F, 0x00,  //     ....(####).......
  0x1E, 0x00,  //     ...(####)........
  0x3C, 0x00,  //     ..(####).........
  0x3F, 0xE0,  //     ..(#########)....  wide center bar (cols 2-10)
  0x00, 0xF8,  //     ........(#####)..  bottom diagonal (cols 8-12)
  0x01, 0xF0,  //     .......(#####)...
  0x03, 0xE0,  //     ......(#####)....
  0x07, 0xC0,  //     .....(#####).....
  0x0F, 0x80,  //     ....(#####)......
  0x07, 0x00,  //     .....(###).......  tip (cols 5-7)
  0x00, 0x00,  //
  0x00, 0x00,  //
  0x00, 0x00,  //
};

// Helper: draw a 16x16 1-bit icon at (x, y) with given color, transparent bg.
// LY_ICON16 is the drawn footprint - LY_SC(16) - so on a scaled profile each
// source pixel becomes a block. Callers size their layout from LY_ICON16, not
// from the literal 16.
inline void drawIcon16(lgfx::LovyanGFX& gfx, int16_t x, int16_t y,
                       const uint8_t* icon, uint16_t color) {
  constexpr int z = LY_ICON16 / 16;
  for (int row = 0; row < 16; row++) {
    uint8_t b0 = pgm_read_byte(&icon[row * 2]);
    uint8_t b1 = pgm_read_byte(&icon[row * 2 + 1]);
    uint16_t bits = (b0 << 8) | b1;
    for (int col = 0; col < 16; col++) {
      if (bits & (0x8000 >> col)) {
        if (z == 1) gfx.drawPixel(x + col, y + row, color);
        else        gfx.fillRect(x + col * z, y + row * z, z, z, color);
      }
    }
  }
}

// Helper: draw a 32x32 1-bit icon at (x, y) with given color, transparent bg
inline void drawIcon32(lgfx::LovyanGFX& gfx, int16_t x, int16_t y,
                       const uint8_t* icon, uint16_t color) {
  for (int row = 0; row < 32; row++) {
    uint32_t bits = ((uint32_t)pgm_read_byte(&icon[row * 4]) << 24) |
                    ((uint32_t)pgm_read_byte(&icon[row * 4 + 1]) << 16) |
                    ((uint32_t)pgm_read_byte(&icon[row * 4 + 2]) << 8) |
                    (uint32_t)pgm_read_byte(&icon[row * 4 + 3]);
    for (int col = 0; col < 32; col++) {
      if (bits & (0x80000000UL >> col)) {
        gfx.drawPixel(x + col, y + row, color);
      }
    }
  }
}

#endif // ICONS_H
