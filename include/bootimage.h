#ifndef __BOOTIMAGE_H
#define __BOOTIMAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
extern const uint8_t bootimage_gif[];
extern const uint8_t bootimage_gif_end[];
#ifdef __cplusplus
}
#endif

inline size_t bootimage_gif_len(void) { return (size_t)(bootimage_gif_end - bootimage_gif); }

#endif
