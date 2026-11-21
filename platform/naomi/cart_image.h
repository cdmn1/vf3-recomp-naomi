#ifndef VF3_NAOMI_CART_IMAGE_H
#define VF3_NAOMI_CART_IMAGE_H

#include "cart_image_format.h"
#include "cart_dma.h"

#define NAOMI_CART_IMAGE_VERIFY_CHUNK 0x00010000u

typedef enum {
    NAOMI_CART_IMAGE_OK = 0,
    NAOMI_CART_IMAGE_INVALID = -1,
    NAOMI_CART_IMAGE_IO_ERROR = -2,
    NAOMI_CART_IMAGE_BAD_FORMAT = -3,
    NAOMI_CART_IMAGE_CHECKSUM_ERROR = -4
} naomi_cart_image_status_t;

/* The 4 KiB header buffer must be 32-byte aligned and remain valid for the call. */
naomi_cart_image_status_t naomi_cart_image_mount(
    uint32_t header_offset, void *header_buffer, uint32_t timeout_ms,
    naomi_cart_image_layout_t *layout, naomi_cart_dma_result_t *io_result);

/* Direct, synchronous reads. Offset, length and destination must be 32-byte aligned. */
naomi_cart_image_status_t naomi_cart_image_read_region(
    const naomi_cart_image_layout_t *layout, uint32_t region_id,
    uint32_t region_offset, void *destination, uint32_t length,
    uint32_t timeout_ms, naomi_cart_dma_result_t *io_result);

/* Verify one complete region using a caller-owned, 64 KiB aligned scratch buffer. */
naomi_cart_image_status_t naomi_cart_image_verify_region(
    const naomi_cart_image_layout_t *layout, uint32_t region_id,
    void *scratch, uint32_t scratch_size, uint32_t timeout_ms,
    uint32_t *actual_crc32, naomi_cart_dma_result_t *io_result);

#endif
