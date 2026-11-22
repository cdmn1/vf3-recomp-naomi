#ifndef VF3_NAOMI_CART_IMAGE_FORMAT_H
#define VF3_NAOMI_CART_IMAGE_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NAOMI_CART_IMAGE_MAGIC "VF3NROM\0"
#define NAOMI_CART_IMAGE_VERSION 1u
#define NAOMI_CART_IMAGE_HEADER_SIZE 4096u
#define NAOMI_CART_IMAGE_REGION_COUNT 3u
#define NAOMI_CART_IMAGE_PROFILE_VF3_REV_D 1u

typedef enum {
    NAOMI_CART_REGION_FIXED_CROM = 1,
    NAOMI_CART_REGION_BANKED_CROM = 2,
    NAOMI_CART_REGION_VROM = 3
} naomi_cart_region_id_t;

typedef enum {
    NAOMI_CART_IMAGE_FORMAT_OK = 0,
    NAOMI_CART_IMAGE_FORMAT_BAD_ARGUMENT = -1,
    NAOMI_CART_IMAGE_FORMAT_BAD_MAGIC = -2,
    NAOMI_CART_IMAGE_FORMAT_BAD_VERSION = -3,
    NAOMI_CART_IMAGE_FORMAT_BAD_HEADER = -4,
    NAOMI_CART_IMAGE_FORMAT_BAD_HEADER_CRC = -5,
    NAOMI_CART_IMAGE_FORMAT_BAD_REGION = -6,
    NAOMI_CART_IMAGE_FORMAT_BAD_RANGE = -7
} naomi_cart_image_format_status_t;

typedef struct {
    uint32_t id;
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;
} naomi_cart_image_region_t;

typedef struct {
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t profile_id;
    naomi_cart_image_region_t regions[NAOMI_CART_IMAGE_REGION_COUNT];
} naomi_cart_image_layout_t;

uint32_t naomi_cart_crc32(const void *data, uint32_t length);
uint32_t naomi_cart_crc32_update(uint32_t state, const void *data, uint32_t length);
naomi_cart_image_format_status_t naomi_cart_image_parse_header(
    const uint8_t header[NAOMI_CART_IMAGE_HEADER_SIZE],
    uint32_t header_offset,
    naomi_cart_image_layout_t *layout);
const naomi_cart_image_region_t *naomi_cart_image_find_region(
    const naomi_cart_image_layout_t *layout, uint32_t region_id);

#ifdef __cplusplus
}
#endif

#endif
