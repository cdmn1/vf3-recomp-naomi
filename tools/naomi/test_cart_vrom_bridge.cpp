#include "vf3_image.h"
#include "cart_vrom_bridge.h"
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

static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

static uint32_t make_header_crc(const uint8_t *header)
{
    const uint8_t zero[4] = {};
    uint32_t state = 0xFFFFFFFFu;
    state = naomi_cart_crc32_update(state, header, 28);
    state = naomi_cart_crc32_update(state, zero, 4);
    state = naomi_cart_crc32_update(state, header + 32,
        NAOMI_CART_IMAGE_HEADER_SIZE - 32);
    return state ^ 0xFFFFFFFFu;
}

static void write_region(uint8_t *d, uint32_t id, uint32_t offset,
                         uint32_t size, uint32_t crc)
{
    write_le32(d, id);
    write_le32(d + 4, offset);
    write_le32(d + 8, size);
    write_le32(d + 12, crc);
}

int main()
{
    const uint32_t header_offset = NAOMI_VF3_IMAGE_HEADER_OFFSET;
    const uint32_t fixed_offset = header_offset + NAOMI_CART_IMAGE_HEADER_SIZE;
    const uint32_t banked_offset = fixed_offset + NAOMI_CART_CROM_FIXED_SIZE;
    const uint32_t vrom_offset = banked_offset +
        NAOMI_CART_CROM_BANK_SIZE * NAOMI_CART_CROM_BANK_COUNT;
    const uint32_t payload_size = NAOMI_CART_IMAGE_HEADER_SIZE +
        NAOMI_CART_CROM_FIXED_SIZE +
        NAOMI_CART_CROM_BANK_SIZE * NAOMI_CART_CROM_BANK_COUNT +
        NAOMI_CART_VROM_LOGICAL_SIZE;

    naomi_vf3_image_t image = {};
    naomi_cart_dma_result_t io = {};
    void *fixed = 0, *cache = 0, *scratch = 0;
    uint8_t *header;

    cart = static_cast<uint8_t *>(calloc(1, 0x08000000u));
    assert(cart);
    assert(posix_memalign(&fixed, 32, NAOMI_VF3_IMAGE_FIXED_SIZE) == 0);
    assert(posix_memalign(&cache, 32, NAOMI_VF3_IMAGE_BANK_CACHE_SIZE) == 0);
    assert(posix_memalign(&scratch, 32, NAOMI_VF3_IMAGE_VERIFY_SIZE) == 0);

    for (uint32_t bank = 0; bank < NAOMI_CART_CROM_BANK_COUNT; ++bank) {
        uint8_t *p = cart + banked_offset + bank * NAOMI_CART_CROM_BANK_SIZE;
        for (uint32_t i = 0; i < 32; ++i)
            p[i] = static_cast<uint8_t>(bank * 16u + i);
    }

    {
        uint32_t vrom_header = 30;
        uint32_t words[3] = {0x11223344u, 0x11223345u, 0x11223346u};
        memcpy(cart + vrom_offset, &vrom_header, 4);
        memcpy(cart + vrom_offset + 4, words, sizeof(words));
    }

    header = cart + header_offset;
    memcpy(header, "VF3NROM\0", 8);
    write_le32(header + 8, NAOMI_CART_IMAGE_VERSION);
    write_le32(header + 12, NAOMI_CART_IMAGE_HEADER_SIZE);
    write_le32(header + 16, NAOMI_CART_IMAGE_PROFILE_VF3_REV_D);
    write_le32(header + 20, NAOMI_CART_IMAGE_REGION_COUNT);
    write_le32(header + 24, payload_size);
    write_le32(header + 28, 0);
    write_region(header + 32, NAOMI_CART_REGION_FIXED_CROM,
        fixed_offset, NAOMI_CART_CROM_FIXED_SIZE,
        naomi_cart_crc32(cart + fixed_offset, NAOMI_CART_CROM_FIXED_SIZE));
    write_region(header + 48, NAOMI_CART_REGION_BANKED_CROM,
        banked_offset, NAOMI_CART_CROM_BANK_SIZE * NAOMI_CART_CROM_BANK_COUNT,
        naomi_cart_crc32(cart + banked_offset,
            NAOMI_CART_CROM_BANK_SIZE * NAOMI_CART_CROM_BANK_COUNT));
    write_region(header + 64, NAOMI_CART_REGION_VROM,
        vrom_offset, NAOMI_CART_VROM_LOGICAL_SIZE,
        naomi_cart_crc32(cart + vrom_offset, NAOMI_CART_VROM_LOGICAL_SIZE));
    write_le32(header + 28, make_header_crc(header));

    assert(naomi_vf3_image_mount(&image, fixed, NAOMI_VF3_IMAGE_FIXED_SIZE,
        cache, NAOMI_VF3_IMAGE_BANK_CACHE_SIZE, scratch,
        NAOMI_VF3_IMAGE_VERIFY_SIZE, 1000, &io) == NAOMI_VF3_IMAGE_OK);
    assert(image.mounted == 1);
    assert(naomi_model3_crom_is_bound());

    assert(naomi_model3_crom_read8(3) == 3);
    assert(naomi_model3_crom_read16(2) == 0x0302);
    assert(naomi_model3_crom_read32(4) == 0x07060504u);
    assert(naomi_model3_crom_select_bank(0xFE));
    assert(naomi_model3_crom_read8(0) == 16);
    assert(naomi_model3_crom_read32(4) == 0x17161514u);
    assert(naomi_model3_crom_select_bank(0xF7));
    assert(naomi_model3_crom_read8(0) == 0);

    naomi_model3_vrom_bind(&image.layout);
    std::vector<uint32_t> fifo;
    assert(naomi_model3_vrom_port(0, &fifo));
    assert(naomi_model3_vrom_last_status() == NAOMI_CART_VROM_OK);
    assert(fifo.size() == 4);
    assert(fifo[0] == 30 && fifo[1] == 0x11223344u &&
           fifo[2] == 0x11223345u && fifo[3] == 0x11223346u);

    free(cart);
    free(fixed);
    free(cache);
    free(scratch);
    puts("VF3 NetDIMM image mount and Model 3 ROM bridges: PASS");
    return 0;
}
