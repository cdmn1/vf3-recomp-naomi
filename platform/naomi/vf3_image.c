#include "vf3_image.h"
#include "cart_vrom_bridge.h"

#include <stdint.h>

naomi_vf3_image_status_t naomi_vf3_image_mount(
    naomi_vf3_image_t *image,
    void *fixed_crom, uint32_t fixed_crom_size,
    void *page_cache, uint32_t page_cache_size,
    void *verify_scratch, uint32_t verify_scratch_size,
    uint32_t timeout_ms, naomi_cart_dma_result_t *io_result)
{
    const naomi_cart_image_region_t *fixed_region;
    uint32_t actual_crc = 0;
    naomi_cart_image_status_t rc;
    naomi_cart_dma_result_t last = {0, 0, 0, 0, 0};

    if (!image)
        return NAOMI_VF3_IMAGE_INVALID;

    image->mounted = 0;
    naomi_model3_crom_bind(0);
    naomi_model3_vrom_bind(0);

    if (!fixed_crom || !page_cache || !verify_scratch ||
        !timeout_ms || fixed_crom_size < NAOMI_VF3_IMAGE_FIXED_SIZE ||
        page_cache_size < NAOMI_VF3_IMAGE_BANK_CACHE_SIZE ||
        verify_scratch_size < NAOMI_VF3_IMAGE_VERIFY_SIZE)
        return NAOMI_VF3_IMAGE_INVALID;

    rc = naomi_cart_image_mount(NAOMI_VF3_IMAGE_HEADER_OFFSET,
        image->header, timeout_ms, &image->layout, &last);
    if (rc != NAOMI_CART_IMAGE_OK) {
        if (io_result) *io_result = last;
        return NAOMI_VF3_IMAGE_MOUNT_ERROR;
    }

    rc = naomi_cart_rom_init(&image->crom, &image->layout,
        fixed_crom, fixed_crom_size, page_cache, page_cache_size,
        timeout_ms, &last);
    if (rc != NAOMI_CART_IMAGE_OK) {
        if (io_result) *io_result = last;
        return NAOMI_VF3_IMAGE_ROM_ERROR;
    }

    fixed_region = naomi_cart_image_find_region(
        &image->layout, NAOMI_CART_REGION_FIXED_CROM);
    if (!fixed_region) {
        if (io_result) *io_result = last;
        return NAOMI_VF3_IMAGE_MOUNT_ERROR;
    }
    actual_crc = naomi_cart_crc32(fixed_crom, NAOMI_VF3_IMAGE_FIXED_SIZE);
    if (actual_crc != fixed_region->crc32) {
        if (io_result) *io_result = last;
        return NAOMI_VF3_IMAGE_FIXED_CRC_ERROR;
    }

    rc = naomi_cart_image_verify_region(&image->layout,
        NAOMI_CART_REGION_BANKED_CROM, verify_scratch,
        verify_scratch_size, timeout_ms, &actual_crc, &last);
    if (rc != NAOMI_CART_IMAGE_OK) {
        if (io_result) *io_result = last;
        return rc == NAOMI_CART_IMAGE_CHECKSUM_ERROR ?
            NAOMI_VF3_IMAGE_BANKED_CRC_ERROR :
            NAOMI_VF3_IMAGE_BANKED_VERIFY_ERROR;
    }

    rc = naomi_cart_image_verify_region(&image->layout,
        NAOMI_CART_REGION_VROM, verify_scratch,
        verify_scratch_size, timeout_ms, &actual_crc, &last);
    if (rc != NAOMI_CART_IMAGE_OK) {
        if (io_result) *io_result = last;
        return rc == NAOMI_CART_IMAGE_CHECKSUM_ERROR ?
            NAOMI_VF3_IMAGE_VROM_CRC_ERROR :
            NAOMI_VF3_IMAGE_VROM_VERIFY_ERROR;
    }

    naomi_model3_crom_bind(&image->crom);
    naomi_model3_vrom_bind(&image->layout);
    image->mounted = 1;
    if (io_result) *io_result = last;
    return NAOMI_VF3_IMAGE_OK;
}
