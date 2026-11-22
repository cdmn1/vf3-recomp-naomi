#include "cart_vrom_bridge.h"
#include "cart_rom.h"
#include "cart_vrom.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static uint8_t *cart;

extern "C" naomi_cart_dma_status_t naomi_cart_dma_read(
    uint32_t offset, void *destination, uint32_t length,
    uint32_t timeout_ms, naomi_cart_dma_result_t *result)
{
    if (!cart || !destination || !timeout_ms || (offset & 31u) ||
        (length & 31u) || ((uintptr_t)destination & 31u) ||
        (uint64_t)offset + length > 0x08000000u)
        return NAOMI_CART_DMA_INVALID;
    memcpy(destination, cart + offset, length);
    if (result) {
        result->transferred = length;
        result->last_count = length;
        result->last_status = 0;
        result->last_end = 0;
        result->elapsed_ms = 0;
    }
    return NAOMI_CART_DMA_OK;
}

int main()
{
    const uint32_t fixed_offset = 0x1000u;
    const uint32_t banked_offset = 0x00801000u;
    const uint32_t vrom_offset = 0x04801000u;
    naomi_cart_image_layout_t image = {};
    naomi_cart_rom_t rom;
    naomi_cart_dma_result_t io = {};
    void *fixed = 0, *cache = 0;

    cart = static_cast<uint8_t *>(calloc(1, 0x08000000u));
    assert(cart);
    assert(posix_memalign(&fixed, 32, NAOMI_CART_CROM_FIXED_SIZE) == 0);
    assert(posix_memalign(&cache, 32, NAOMI_CART_CROM_CACHE_SIZE) == 0);

    image.regions[0].id = NAOMI_CART_REGION_FIXED_CROM;
    image.regions[0].offset = fixed_offset;
    image.regions[0].size = NAOMI_CART_CROM_FIXED_SIZE;
    image.regions[1].id = NAOMI_CART_REGION_BANKED_CROM;
    image.regions[1].offset = banked_offset;
    image.regions[1].size = NAOMI_CART_CROM_BANK_SIZE * NAOMI_CART_CROM_BANK_COUNT;
    image.regions[2].id = NAOMI_CART_REGION_VROM;
    image.regions[2].offset = vrom_offset;
    image.regions[2].size = NAOMI_CART_VROM_LOGICAL_SIZE;
    { uint32_t header = 30, words[3] = {0x11223344u, 0x11223345u, 0x11223346u};
      memcpy(cart + vrom_offset, &header, 4);
      memcpy(cart + vrom_offset + 4, words, sizeof(words)); }

    for (uint32_t bank = 0; bank < NAOMI_CART_CROM_BANK_COUNT; ++bank) {
        uint8_t *p = cart + banked_offset + bank * NAOMI_CART_CROM_BANK_SIZE;
        for (uint32_t i = 0; i < 32; ++i)
            p[i] = static_cast<uint8_t>(bank * 16u + i);
    }

    assert(naomi_cart_rom_init(&rom, &image, fixed, NAOMI_CART_CROM_FIXED_SIZE,
        cache, NAOMI_CART_CROM_CACHE_SIZE, 1000, &io) == NAOMI_CART_IMAGE_OK);
    naomi_model3_crom_bind(&rom);
    assert(naomi_model3_crom_is_bound());

    assert(naomi_model3_crom_read8(3) == 3);
    assert(naomi_model3_crom_read16(2) == 0x0302);
    assert(naomi_model3_crom_read32(4) == 0x07060504u);

    assert(naomi_model3_crom_select_bank(0xFE)); /* logical bank 1 */
    assert(naomi_model3_crom_read8(0) == 16);
    assert(naomi_model3_crom_read32(4) == 0x17161514u);

    assert(naomi_model3_crom_select_bank(0xF7)); /* logical bank 8 mirrors bank 0 */
    assert(naomi_model3_crom_read8(0) == 0);

    naomi_model3_vrom_bind(&image);
    std::vector<uint32_t> fifo;
    assert(naomi_model3_vrom_port(0, &fifo));
    assert(naomi_model3_vrom_last_status() == NAOMI_CART_VROM_OK);
    assert(fifo.size() == 4);
    assert(fifo[0] == 30 && fifo[1] == 0x11223344u &&
           fifo[2] == 0x11223345u && fifo[3] == 0x11223346u);

    free(cart);
    free(fixed);
    free(cache);
    puts("VF3 Model 3 CROM bridge: PASS");
    return 0;
}
