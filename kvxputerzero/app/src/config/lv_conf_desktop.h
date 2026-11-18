/**
 * lv_conf_desktop.h
 *
 * Desktop / SDL2 Emulator configuration
 * Target:
 *   - Linux
 *   - SDL2
 *   - 32-bit color depth
 *   - LVGL v9.x
 */

#pragma once

/*====================
   COLOR SETTINGS
 *====================*/

/* 32-bit ARGB8888 */
#define LV_COLOR_DEPTH                     32

/* Use 32-bit color swap */
#define LV_COLOR_16_SWAP                   0

/*====================
   MEMORY SETTINGS
 *====================*/

/* stdlib malloc/free */
#define LV_USE_STDLIB_MALLOC               LV_STDLIB_BUILTIN

/* Memory size */
#define LV_MEM_SIZE                        (16U * 1024U * 1024U)

#define LV_DRAW_THREAD_STACK_SIZE          (32U * 1024U)

#define LV_USE_FLOAT                       1

/*====================
   OS SETTINGS
 *====================*/
#ifdef _WIN32
    #define LV_USE_OS                      LV_OS_SDL2
#else
    #define LV_USE_OS                      LV_OS_PTHREAD
#endif


/*====================
   RENDER SETTINGS
 *====================*/

/* Use draw buffers */
#define LV_DRAW_BUF_STRIDE_ALIGN           4
#define LV_DRAW_BUF_ALIGN                  4

/* Desktop-only rendering throughput */
#define LV_DRAW_SW_DRAW_UNIT_CNT           4
#define LV_DRAW_THREAD_PRIO                LV_THREAD_PRIO_HIGH

#if defined(__aarch64__) || defined(__ARM_NEON) || defined(__ARM_NEON__)
#define LV_USE_DRAW_SW_ASM                 LV_DRAW_SW_ASM_NEON
#else
#define LV_USE_DRAW_SW_ASM                 LV_DRAW_SW_ASM_NONE
#endif

/* Cache common complex primitives; the desktop simulator can afford the RAM. */
#define LV_DRAW_SW_SHADOW_CACHE_SIZE       32
#define LV_DRAW_SW_CIRCLE_CACHE_SIZE       8

/*====================
   DATA BINDING
 *====================*/

#define LV_USE_OBSERVER                    1

/*====================
   LOG SETTINGS
 *====================*/

#define LV_USE_LOG                         1

#define LV_LOG_LEVEL                       LV_LOG_LEVEL_INFO

/* Printf backend */
#define LV_LOG_PRINTF                      1

/*====================
   ASSERT SETTINGS
 *====================*/

#define LV_USE_ASSERT_NULL                 1
#define LV_USE_ASSERT_MALLOC               1
#define LV_USE_ASSERT_STYLE                0
#define LV_USE_ASSERT_MEM_INTEGRITY        0
#define LV_USE_ASSERT_OBJ                  1

/*====================
   FONT SETTINGS
 *====================*/

#define LV_FONT_MONTSERRAT_10              1
#define LV_FONT_MONTSERRAT_12              1
#define LV_FONT_MONTSERRAT_14              1
#define LV_FONT_MONTSERRAT_16              1
#define LV_FONT_MONTSERRAT_18              1
#define LV_FONT_MONTSERRAT_20              1
#define LV_FONT_MONTSERRAT_24              1
#define LV_FONT_MONTSERRAT_28              1
#define LV_USE_FONT_PLACEHOLDER            1

#define LV_USE_FREETYPE                    1
#define LV_USE_SNAPSHOT                    1
#define LV_FREETYPE_CACHE_FT_GLYPH_CNT     256

#define LV_USE_TINY_TTF                    0
#define LV_TINY_TTF_FILE_SUPPORT           0
#define LV_TINY_TTF_CACHE_GLYPH_CNT        128

/*====================
   IMAGE SETTINGS
 *====================*/

#define LV_USE_LIBPNG                      1
#define LV_USE_LODEPNG                     0
#define LV_USE_BMP                         1
#define LV_USE_LIBJPEG_TURBO               0
#define LV_USE_TJPGD                       1
#define LV_USE_GIF                         1
#define LV_GIF_CACHE_DECODE_DATA           0

#define LV_CACHE_DEF_SIZE                  (8U * 1024U * 1024U)
#define LV_IMAGE_HEADER_CACHE_DEF_CNT      64

/*====================
   FILESYSTEM
 *====================*/

#define LV_USE_FS_STDIO                    1
#define LV_FS_STDIO_LETTER                 'A'
#define LV_FS_DEFAULT_DRIVER_LETTER        'A'
#define LV_FS_STDIO_PATH                   ""

/* Optional cache */
#define LV_FS_STDIO_CACHE_SIZE             0

/*====================
   SDL2 DRIVER
 *====================*/

#define LV_USE_SDL                         1
#define LV_SDL_INCLUDE_PATH                <SDL.h>
#define LV_SDL_RENDER_MODE                 LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_SDL_BUF_COUNT                   1
#define LV_SDL_ACCELERATED                 1
#define LV_SDL_FULLSCREEN                  0
#define LV_SDL_DIRECT_EXIT                 1
#define LV_DEF_REFR_PERIOD                 16
/*====================
   THEMES
 *====================*/

#define LV_USE_THEME_DEFAULT               1

#define LV_THEME_DEFAULT_DARK              1


/*====================
   WIDGETS
 *====================*/

#define LV_USE_LABEL                       1
#define LV_USE_BUTTON                      1
#define LV_USE_IMAGE                       1
#define LV_USE_LIST                        1
#define LV_USE_TEXTAREA                    1
#define LV_USE_KEYBOARD                    1
#define LV_USE_FLEX                        1
#define LV_USE_GRID                        1

/*====================
   PERFORMANCE MONITOR
 *====================*/

#define LV_USE_PERF_MONITOR                1
#define LV_USE_SYSMON                      1

/*====================
   ANIMATION
 *====================*/

#define LV_USE_ANIMATION                   1

/*====================
   OTHERS
 *====================*/

#define LV_USE_SHADOW                      1
#define LV_USE_GRADIENTS                   1
#define LV_OBJ_STYLE_CACHE                 1
