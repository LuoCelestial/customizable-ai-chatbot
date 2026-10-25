#include "assistant_wallpaper.h"

#include <string.h>

#define WALLPAPER_PARTITION_LABEL "wallpaper"
#define WALLPAPER_PARTITION_SUBTYPE 0x40
#define WALLPAPER_MAGIC 0x50575041u /* "APWP" in little-endian storage. */
#define WALLPAPER_VERSION 1u
#define WALLPAPER_FORMAT_RGB565_LE 1u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t format;
    uint32_t bytes;
} wallpaper_header_t;

_Static_assert(sizeof(wallpaper_header_t) == 16, "wallpaper header layout changed");

static const esp_partition_t *s_upload_partition;
static size_t s_uploaded;
static bool s_upload_active;

static const esp_partition_t *wallpaper_partition(void)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        (esp_partition_subtype_t)WALLPAPER_PARTITION_SUBTYPE,
        WALLPAPER_PARTITION_LABEL);
    if (!partition || partition->size < sizeof(wallpaper_header_t) + ASSISTANT_WALLPAPER_BYTES)
        return NULL;
    return partition;
}

static bool header_valid(const wallpaper_header_t *header)
{
    return header && header->magic == WALLPAPER_MAGIC &&
           header->version == WALLPAPER_VERSION &&
           header->width == ASSISTANT_WALLPAPER_WIDTH &&
           header->height == ASSISTANT_WALLPAPER_HEIGHT &&
           header->format == WALLPAPER_FORMAT_RGB565_LE &&
           header->bytes == ASSISTANT_WALLPAPER_BYTES;
}

bool assistant_wallpaper_available(void)
{
    const esp_partition_t *partition = wallpaper_partition();
    wallpaper_header_t header = {0};
    return partition &&
           esp_partition_read(partition, 0, &header, sizeof(header)) == ESP_OK &&
           header_valid(&header);
}

esp_err_t assistant_wallpaper_begin(void)
{
    const esp_partition_t *partition = wallpaper_partition();
    if (!partition) return ESP_ERR_NOT_FOUND;
    s_upload_active = false;
    s_uploaded = 0;
    esp_err_t err = esp_partition_erase_range(partition, 0, partition->size);
    if (err != ESP_OK) return err;
    s_upload_partition = partition;
    s_upload_active = true;
    return ESP_OK;
}

esp_err_t assistant_wallpaper_write(size_t offset, const void *data, size_t bytes)
{
    if (!s_upload_active || !s_upload_partition || !data || !bytes ||
        offset != s_uploaded || bytes > ASSISTANT_WALLPAPER_BYTES - s_uploaded)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = esp_partition_write(s_upload_partition,
                                        sizeof(wallpaper_header_t) + offset,
                                        data, bytes);
    if (err == ESP_OK) s_uploaded += bytes;
    return err;
}

esp_err_t assistant_wallpaper_commit(void)
{
    if (!s_upload_active || !s_upload_partition || s_uploaded != ASSISTANT_WALLPAPER_BYTES)
        return ESP_ERR_INVALID_STATE;
    const wallpaper_header_t header = {
        .magic = WALLPAPER_MAGIC,
        .version = WALLPAPER_VERSION,
        .width = ASSISTANT_WALLPAPER_WIDTH,
        .height = ASSISTANT_WALLPAPER_HEIGHT,
        .format = WALLPAPER_FORMAT_RGB565_LE,
        .bytes = ASSISTANT_WALLPAPER_BYTES,
    };
    /* Commit metadata first and the magic word last. A reset or write failure
     * before the final four bytes leaves the erased magic and cannot activate
     * a partial image. */
    esp_err_t err = esp_partition_write(s_upload_partition, sizeof(header.magic),
                                        (const uint8_t *)&header + sizeof(header.magic),
                                        sizeof(header) - sizeof(header.magic));
    if (err == ESP_OK)
        err = esp_partition_write(s_upload_partition, 0, &header.magic, sizeof(header.magic));
    s_upload_active = false;
    s_upload_partition = NULL;
    s_uploaded = 0;
    return err;
}

void assistant_wallpaper_abort(void)
{
    s_upload_active = false;
    s_upload_partition = NULL;
    s_uploaded = 0;
}

esp_err_t assistant_wallpaper_clear(void)
{
    const esp_partition_t *partition = wallpaper_partition();
    if (!partition) return ESP_ERR_NOT_FOUND;
    assistant_wallpaper_abort();
    return esp_partition_erase_range(partition, 0, partition->erase_size);
}

esp_err_t assistant_wallpaper_map(const uint8_t **pixels,
                                  esp_partition_mmap_handle_t *handle)
{
    if (!pixels || !handle) return ESP_ERR_INVALID_ARG;
    *pixels = NULL;
    const esp_partition_t *partition = wallpaper_partition();
    wallpaper_header_t header = {0};
    if (!partition || esp_partition_read(partition, 0, &header, sizeof(header)) != ESP_OK ||
        !header_valid(&header)) return ESP_ERR_NOT_FOUND;
    const void *mapped = NULL;
    esp_err_t err = esp_partition_mmap(partition, sizeof(header),
                                       ASSISTANT_WALLPAPER_BYTES,
                                       ESP_PARTITION_MMAP_DATA, &mapped, handle);
    if (err == ESP_OK) *pixels = mapped;
    return err;
}

void assistant_wallpaper_unmap(esp_partition_mmap_handle_t handle)
{
    esp_partition_munmap(handle);
}
