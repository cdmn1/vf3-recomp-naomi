#include "cart_image.h"

#include <stdint.h>

static int aligned32(const void *p)
{
    return (((uintptr_t)p) & 31u) == 0u;
}

static naomi_cart_image_status_t map_dma_status(naomi_cart_dma_status_t status)
{
    return status == NAOMI_CART_DMA_OK ? NAOMI_CART_IMAGE_OK : NAOMI_CART_IMAGE_IO_ERROR;
}

naomi_cart_image_status_t naomi_cart_image_mount(
    uint32_t header_offset, void *header_buffer, uint32_t timeout_ms,
    naomi_cart_image_layout_t *layout, naomi_cart_dma_result_t *io_result)
{
    naomi_cart_dma_status_t rc;
    naomi_cart_image_format_status_t format_rc;
    if(!header_buffer || !layout || !timeout_ms || !aligned32(header_buffer) ||
       (header_offset & 31u))
        return NAOMI_CART_IMAGE_INVALID;

    rc = naomi_cart_dma_read(header_offset, header_buffer,
                             NAOMI_CART_IMAGE_HEADER_SIZE, timeout_ms, io_result);
    if(rc != NAOMI_CART_DMA_OK)
        return map_dma_status(rc);

    format_rc = naomi_cart_image_parse_header(
        (const uint8_t *)header_buffer, header_offset, layout);
    return format_rc == NAOMI_CART_IMAGE_FORMAT_OK ?
           NAOMI_CART_IMAGE_OK : NAOMI_CART_IMAGE_BAD_FORMAT;
}

naomi_cart_image_status_t naomi_cart_image_read_region(
    const naomi_cart_image_layout_t *layout, uint32_t region_id,
    uint32_t region_offset, void *destination, uint32_t length,
    uint32_t timeout_ms, naomi_cart_dma_result_t *io_result)
{
    const naomi_cart_image_region_t *region =
        naomi_cart_image_find_region(layout, region_id);
    uint64_t source;
    if(!region || !destination || !length || !timeout_ms ||
       !aligned32(destination) || (region_offset & 31u) || (length & 31u) ||
       (uint64_t)region_offset + length > region->size)
        return NAOMI_CART_IMAGE_INVALID;
    source = (uint64_t)region->offset + region_offset;
    if(source + length > 0x80000000ull)
        return NAOMI_CART_IMAGE_INVALID;
    return map_dma_status(naomi_cart_dma_read((uint32_t)source, destination,
                                               length, timeout_ms, io_result));
}

naomi_cart_image_status_t naomi_cart_image_verify_region(
    const naomi_cart_image_layout_t *layout, uint32_t region_id,
    void *scratch, uint32_t scratch_size, uint32_t timeout_ms,
    uint32_t *actual_crc32, naomi_cart_dma_result_t *io_result)
{
    const naomi_cart_image_region_t *region =
        naomi_cart_image_find_region(layout, region_id);
    uint32_t state = 0xFFFFFFFFu;
    uint32_t offset = 0;
    naomi_cart_dma_result_t last = {0, 0, 0, 0, 0};

    if(actual_crc32) *actual_crc32 = 0;
    if(!region || !scratch || !aligned32(scratch) ||
       scratch_size < NAOMI_CART_IMAGE_VERIFY_CHUNK ||
       (scratch_size & 31u) || !timeout_ms)
        return NAOMI_CART_IMAGE_INVALID;

    while(offset < region->size) {
        uint32_t chunk = region->size - offset;
        if(chunk > NAOMI_CART_IMAGE_VERIFY_CHUNK)
            chunk = NAOMI_CART_IMAGE_VERIFY_CHUNK;
        naomi_cart_image_status_t rc = naomi_cart_image_read_region(
            layout, region_id, offset, scratch, chunk, timeout_ms, &last);
        if(rc != NAOMI_CART_IMAGE_OK) {
            if(io_result) *io_result = last;
            return rc;
        }
        state = naomi_cart_crc32_update(state, scratch, chunk);
        offset += chunk;
    }

    state ^= 0xFFFFFFFFu;
    if(actual_crc32) *actual_crc32 = state;
    if(io_result) *io_result = last;
    return state == region->crc32 ? NAOMI_CART_IMAGE_OK :
                                    NAOMI_CART_IMAGE_CHECKSUM_ERROR;
}
