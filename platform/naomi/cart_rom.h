#ifndef VF3_NAOMI_CART_ROM_H
#define VF3_NAOMI_CART_ROM_H

#include "cart_image.h"

#define NAOMI_CART_CROM_FIXED_SIZE 0x00800000u
#define NAOMI_CART_CROM_BANK_SIZE  0x00800000u
#define NAOMI_CART_CROM_BANK_COUNT 8u

typedef struct {
    const naomi_cart_image_layout_t *image;
    uint8_t *fixed_crom;
    uint8_t *bank_window;
    uint32_t timeout_ms;
    int32_t active_bank;
    uint8_t fixed_valid;
    uint8_t bank_valid;
} naomi_cart_rom_t;

/* Buffers are caller-owned, 32-byte aligned and remain resident.
 * This deliberately allocates no memory: Naomi's 32 MiB budget is tight. */
naomi_cart_image_status_t naomi_cart_rom_init(
    naomi_cart_rom_t *rom, const naomi_cart_image_layout_t *image,
    void *fixed_crom, uint32_t fixed_size,
    void *bank_window, uint32_t bank_window_size,
    uint32_t timeout_ms, naomi_cart_dma_result_t *io_result);

/* Banks 8..15 mirror banks 0..7, matching upstream's duplicated 64 MiB ROM. */
naomi_cart_image_status_t naomi_cart_rom_select_bank(
    naomi_cart_rom_t *rom, uint32_t bank,
    naomi_cart_dma_result_t *io_result);

static inline const uint8_t *naomi_cart_rom_fixed(const naomi_cart_rom_t *rom)
{
    return rom && rom->fixed_valid ? rom->fixed_crom : 0;
}

static inline const uint8_t *naomi_cart_rom_banked(const naomi_cart_rom_t *rom)
{
    return rom && rom->bank_valid ? rom->bank_window : 0;
}

#endif
