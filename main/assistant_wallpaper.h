#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_partition.h"

#define ASSISTANT_WALLPAPER_WIDTH 240
#define ASSISTANT_WALLPAPER_HEIGHT 320
#define ASSISTANT_WALLPAPER_BYTES \
    (ASSISTANT_WALLPAPER_WIDTH * ASSISTANT_WALLPAPER_HEIGHT * sizeof(uint16_t))

/* Wallpaper pixels are little-endian RGB565. Uploads are sequential and the
 * validity header is written last, so interrupted writes are ignored at boot. */
bool assistant_wallpaper_available(void);
esp_err_t assistant_wallpaper_begin(void);
esp_err_t assistant_wallpaper_write(size_t offset, const void *data, size_t bytes);
esp_err_t assistant_wallpaper_commit(void);
void assistant_wallpaper_abort(void);
esp_err_t assistant_wallpaper_clear(void);

/* Maps the wallpaper directly from Flash without a full-frame RAM copy. The
 * returned handle remains owned by the caller until assistant_wallpaper_unmap. */
esp_err_t assistant_wallpaper_map(const uint8_t **pixels,
                                  esp_partition_mmap_handle_t *handle);
void assistant_wallpaper_unmap(esp_partition_mmap_handle_t handle);
