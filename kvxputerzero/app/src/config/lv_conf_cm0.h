#pragma once

/*====================
   COLOR
 *====================*/

#define LV_COLOR_DEPTH                     32
#define LV_COLOR_16_SWAP                   0

/*====================
   MEMORY
 *====================*/

#define LV_USE_STDLIB_MALLOC               LV_STDLIB_BUILTIN

#define LV_MEM_SIZE                        (32U * 1024U * 1024U)

#define LV_USE_FLOAT                       1

/*====================
   OS
 *====================*/

#define LV_USE_OS                          LV_OS_PTHREAD

#define LV_DRAW_THREAD_STACK_SIZE          (64U * 1024U)

/*====================
   DATA BINDING
 *====================*/

#define LV_USE_OBSERVER                    1

/*====================
   LOG
 *====================*/

#define LV_USE_LOG                         1
#define LV_LOG_LEVEL                       LV_LOG_LEVEL_ERROR
#define LV_LOG_PRINTF                      1

/*====================
   ASSERT
 *====================*/

#define LV_USE_ASSERT_NULL                 1
#define LV_USE_ASSERT_MALLOC               1
#define LV_USE_ASSERT_OBJ                  1

/*====================
   DRM/KMS
 *====================*/

#ifndef APP_USE_DRM
#define APP_USE_DRM                        1
#endif
#define LV_USE_LINUX_DRM                   APP_USE_DRM
#define LV_USE_LINUX_FBDEV                 1
#define LV_LINUX_FBDEV_RENDER_MODE         LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_LINUX_FBDEV_BUFFER_COUNT        0
#define LV_LINUX_FBDEV_BUFFER_SIZE         60
#define LV_LINUX_FBDEV_MMAP                1
#define LV_DEF_REFR_PERIOD                 16

/* Rendering
 * Pure software rendering
 */
#define LV_LINUX_DRM_USE_EGL               0

#define LV_USE_DRAW_OPENGLES               0

/*====================
   INPUT
 *====================*/

#define LV_USE_EVDEV                       1

/*====================
   FILESYSTEM
 *====================*/

#define LV_USE_FS_STDIO                    1
#define LV_FS_STDIO_LETTER                 'A'
#define LV_FS_DEFAULT_DRIVER_LETTER        'A'
#define LV_FS_STDIO_PATH                   ""
#define LV_FS_STDIO_CACHE_SIZE             0

/*====================
   FONTS
 *====================*/

#define LV_USE_FREETYPE                    1
#define LV_USE_SNAPSHOT                    1
#define LV_FREETYPE_CACHE_FT_GLYPH_CNT     256

#define LV_USE_TINY_TTF                    0

#define LV_FONT_MONTSERRAT_10              1
#define LV_FONT_MONTSERRAT_12              1
#define LV_FONT_MONTSERRAT_14              1
#define LV_FONT_MONTSERRAT_16              1
#define LV_FONT_MONTSERRAT_18              1
#define LV_FONT_MONTSERRAT_20              1
#define LV_FONT_MONTSERRAT_24              1
#define LV_FONT_MONTSERRAT_28              1
#define LV_USE_FONT_PLACEHOLDER            1

/*====================
   IMAGES
 *====================*/

#define LV_USE_LIBPNG                      1
#define LV_USE_LODEPNG                     0
#define LV_USE_BMP                         1
#define LV_USE_LIBJPEG_TURBO               0
#define LV_USE_TJPGD                       1
#define LV_USE_GIF                         1
#define LV_GIF_CACHE_DECODE_DATA           0

#define LV_IMAGE_CACHE_DEF_SIZE            16

/*====================
   THEMES
 *====================*/

#define LV_USE_THEME_DEFAULT               1
#define LV_THEME_DEFAULT_DARK              1

/*====================
   LAYOUT
 *====================*/

#define LV_USE_FLEX                        1
#define LV_USE_GRID                        1

/*====================
   WIDGETS
 *====================*/

#define LV_USE_LABEL                       1
#define LV_USE_BUTTON                      1
#define LV_USE_IMAGE                       1
#define LV_USE_LIST                        1
#define LV_USE_TEXTAREA                    1
#define LV_USE_KEYBOARD                    1

/*====================
   PERFORMANCE
 *====================*/

#define LV_USE_PERF_MONITOR                0
#define LV_USE_SYSMON                      0

/*====================
   EFFECTS
 *====================*/

#define LV_USE_SHADOW                      1
#define LV_USE_GRADIENTS                   1

/*====================
   DRAW BUFFER
 *====================*/

/* fbdev draw buffers are allocated by LVGL; keep alignment compatible with lv_malloc. */
#define LV_DRAW_BUF_ALIGN                  4
#define LV_DRAW_BUF_STRIDE_ALIGN           4
