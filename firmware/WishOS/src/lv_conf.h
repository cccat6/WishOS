#pragma once
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_USE_OS LV_OS_NONE
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM
#define LV_MEM_SIZE (40U*1024U)
#define LV_USE_LOG 0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 1
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_IMAGE 1
#define LV_USE_TJPGD 1
#define LV_USE_LODEPNG 1
/* Decoder allocations use the shared resource budget. Image admission is a
 * runtime policy; a second fixed allocation ceiling would ignore that policy. */
#define LV_USE_TINY_TTF 0
#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_DROPDOWN 0
#define LV_USE_KEYBOARD 0
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_SYSMON 0
#define LV_FONT_MONTSERRAT_14 1

#define LV_BIN_DECODER_RAM_LOAD 0
