#ifndef VF3_NAOMI_CART_ROM_H
#define VF3_NAOMI_CART_ROM_H

#include "cart_image.h"

#define NAOMI_CART_CROM_FIXED_SIZE 0x00800000u
#define NAOMI_CART_CROM_BANK_SIZE  0x00800000u
#define NAOMI_CART_CROM_BANK_COUNT 8u
#define NAOMI_CART_CROM_PAGE_SIZE  0x00010000u
#define NAOMI_CART_CROM_CACHE_SLOTS 4u
#define NAOMI_CART_CROM_CACHE_SIZE (NAOMI_CART_CROM_PAGE_SIZE * NAOMI_CART_CROM_CACHE_SLOTS)

typedef struct {
    const naomi_cart_image_layout_t *image;
    uint8_t *fixed_crom;
    uint8_t *page_cache;
    uint32_t timeout_ms;
    uint32_t active_bank;
    uint32_t age;
    uint32_t page_tag[NAOMI_CART_CROM_CACHE_SLOTS];
    uint32_t page_age[NAOMI_CART_CROM_CACHE_SLOTS];
    uint8_t fixed_valid;
    uint8_t bank_valid;
} naomi_cart_rom_t;

/* Fixed CROM is resident. Banked CROM uses four caller-owned 64 KiB cache
 * pages (256 KiB total), not an 8 MiB bank allocation. All buffers are
 * caller-owned, 32-byte aligned and remain resident. */
naomi_cart_image_status_t naomi_cart_rom_init(
    naomi_cart_rom_t *rom, const naomi_cart_image_layout_t *image,
    void *fixed_crom, uint32_t fixed_size,
    void *page_cache, uint32_t page_cache_size,
    uint32_t timeout_ms, naomi_cart_dma_result_t *io_result);

/* Logical banks 8..15 mirror banks 0..7, as in upstream's duplicated ROM. */
naomi_cart_image_status_t naomi_cart_rom_select_bank(
    naomi_cart_rom_t *rom, uint32_t bank);

/* Returns a pointer to the resident 64 KiB page containing bank_offset.
 * The pointer remains valid until that cache slot is replaced or the bank changes. */
const uint8_t *naomi_cart_rom_banked_page(
    naomi_cart_rom_t *rom, uint32_t bank_offset,
    naomi_cart_dma_result_t *io_result);

static inline const uint8_t *naomi_cart_rom_fixed(const naomi_cart_rom_t *rom)
{
    return rom && rom->fixed_valid ? rom->fixed_crom : 0;
}

#endif
