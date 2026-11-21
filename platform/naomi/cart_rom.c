#include "cart_rom.h"

#include <stdint.h>
#include <string.h>

static int aligned32(const void *p)
{
    return p && ((((uintptr_t)p) & 31u) == 0u);
}

naomi_cart_image_status_t naomi_cart_rom_init(
    naomi_cart_rom_t *rom, const naomi_cart_image_layout_t *image,
    void *fixed_crom, uint32_t fixed_size,
    void *bank_window, uint32_t bank_window_size,
    uint32_t timeout_ms, naomi_cart_dma_result_t *io_result)
{
    naomi_cart_image_status_t rc;
    if(!rom || !image || !aligned32(fixed_crom) ||
       fixed_size < NAOMI_CART_CROM_FIXED_SIZE ||
       !aligned32(bank_window) || bank_window_size < NAOMI_CART_CROM_BANK_SIZE ||
       !timeout_ms ||
       !naomi_cart_image_find_region(image, NAOMI_CART_REGION_FIXED_CROM) ||
       !naomi_cart_image_find_region(image, NAOMI_CART_REGION_BANKED_CROM))
        return NAOMI_CART_IMAGE_INVALID;

    memset(rom, 0, sizeof(*rom));
    rom->image = image;
    rom->fixed_crom = (uint8_t *)fixed_crom;
    rom->bank_window = (uint8_t *)bank_window;
    rom->timeout_ms = timeout_ms;
    rom->active_bank = -1;

    rc = naomi_cart_image_read_region(image, NAOMI_CART_REGION_FIXED_CROM,
        0, fixed_crom, NAOMI_CART_CROM_FIXED_SIZE, timeout_ms, io_result);
    if(rc != NAOMI_CART_IMAGE_OK)
        return rc;
    rom->fixed_valid = 1;
    return NAOMI_CART_IMAGE_OK;
}

naomi_cart_image_status_t naomi_cart_rom_select_bank(
    naomi_cart_rom_t *rom, uint32_t bank,
    naomi_cart_dma_result_t *io_result)
{
    uint32_t logical_bank;
    naomi_cart_image_status_t rc;
    if(!rom || !rom->image || !rom->fixed_valid || !rom->bank_window)
        return NAOMI_CART_IMAGE_INVALID;

    logical_bank = bank & (NAOMI_CART_CROM_BANK_COUNT - 1u);
    if(rom->bank_valid && rom->active_bank == (int32_t)logical_bank)
        return NAOMI_CART_IMAGE_OK;

    /* Invalidate before refill: callers must not use stale contents on error. */
    rom->bank_valid = 0;
    rc = naomi_cart_image_read_region(rom->image, NAOMI_CART_REGION_BANKED_CROM,
        logical_bank * NAOMI_CART_CROM_BANK_SIZE, rom->bank_window,
        NAOMI_CART_CROM_BANK_SIZE, rom->timeout_ms, io_result);
    if(rc != NAOMI_CART_IMAGE_OK) {
        rom->active_bank = -1;
        return rc;
    }

    rom->active_bank = (int32_t)logical_bank;
    rom->bank_valid = 1;
    return NAOMI_CART_IMAGE_OK;
}
