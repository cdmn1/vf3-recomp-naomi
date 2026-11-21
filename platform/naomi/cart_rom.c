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
    void *page_cache, uint32_t page_cache_size,
    uint32_t timeout_ms, naomi_cart_dma_result_t *io_result)
{
    naomi_cart_image_status_t rc;
    if(!rom || !image || !aligned32(fixed_crom) ||
       fixed_size < NAOMI_CART_CROM_FIXED_SIZE ||
       !aligned32(page_cache) || page_cache_size < NAOMI_CART_CROM_CACHE_SIZE ||
       !timeout_ms ||
       !naomi_cart_image_find_region(image, NAOMI_CART_REGION_FIXED_CROM) ||
       !naomi_cart_image_find_region(image, NAOMI_CART_REGION_BANKED_CROM))
        return NAOMI_CART_IMAGE_INVALID;

    memset(rom, 0, sizeof(*rom));
    rom->image = image;
    rom->fixed_crom = (uint8_t *)fixed_crom;
    rom->page_cache = (uint8_t *)page_cache;
    rom->timeout_ms = timeout_ms;
    for(unsigned i = 0; i < NAOMI_CART_CROM_CACHE_SLOTS; ++i)
        rom->page_tag[i] = 0xFFFFFFFFu;

    rc = naomi_cart_image_read_region(image, NAOMI_CART_REGION_FIXED_CROM,
        0, fixed_crom, NAOMI_CART_CROM_FIXED_SIZE, timeout_ms, io_result);
    if(rc != NAOMI_CART_IMAGE_OK)
        return rc;
    rom->fixed_valid = 1;
    return NAOMI_CART_IMAGE_OK;
}

naomi_cart_image_status_t naomi_cart_rom_select_bank(
    naomi_cart_rom_t *rom, uint32_t bank)
{
    if(!rom || !rom->image || !rom->fixed_valid)
        return NAOMI_CART_IMAGE_INVALID;
    bank &= NAOMI_CART_CROM_BANK_COUNT - 1u;
    if(!rom->bank_valid || rom->active_bank != bank) {
        rom->active_bank = bank;
        rom->bank_valid = 1;
        rom->age = 0;
        for(unsigned i = 0; i < NAOMI_CART_CROM_CACHE_SLOTS; ++i) {
            rom->page_tag[i] = 0xFFFFFFFFu;
            rom->page_age[i] = 0;
        }
    }
    return NAOMI_CART_IMAGE_OK;
}

const uint8_t *naomi_cart_rom_banked_page(
    naomi_cart_rom_t *rom, uint32_t bank_offset,
    naomi_cart_dma_result_t *io_result)
{
    uint32_t page, slot = 0, oldest = 0xFFFFFFFFu;
    naomi_cart_image_status_t rc;
    if(!rom || !rom->bank_valid || bank_offset >= NAOMI_CART_CROM_BANK_SIZE)
        return 0;

    page = bank_offset / NAOMI_CART_CROM_PAGE_SIZE;
    for(uint32_t i = 0; i < NAOMI_CART_CROM_CACHE_SLOTS; ++i) {
        if(rom->page_tag[i] == page) {
            rom->page_age[i] = ++rom->age;
            return rom->page_cache + i * NAOMI_CART_CROM_PAGE_SIZE;
        }
        if(rom->page_tag[i] == 0xFFFFFFFFu) {
            slot = i;
            oldest = 0;
        } else if(oldest != 0 && rom->page_age[i] < oldest) {
            oldest = rom->page_age[i];
            slot = i;
        }
    }

    rc = naomi_cart_image_read_region(rom->image, NAOMI_CART_REGION_BANKED_CROM,
        rom->active_bank * NAOMI_CART_CROM_BANK_SIZE + page * NAOMI_CART_CROM_PAGE_SIZE,
        rom->page_cache + slot * NAOMI_CART_CROM_PAGE_SIZE,
        NAOMI_CART_CROM_PAGE_SIZE, rom->timeout_ms, io_result);
    if(rc != NAOMI_CART_IMAGE_OK) {
        rom->page_tag[slot] = 0xFFFFFFFFu;
        return 0;
    }
    rom->page_tag[slot] = page;
    rom->page_age[slot] = ++rom->age;
    return rom->page_cache + slot * NAOMI_CART_CROM_PAGE_SIZE;
}
