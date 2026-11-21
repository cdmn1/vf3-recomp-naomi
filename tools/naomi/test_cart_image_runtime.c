#include "cart_image.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_IMAGE_SIZE 0x08000000u
#define HEADER_OFFSET   0x01000000u
#define FIXED_OFFSET    0x01001000u
#define BANKED_OFFSET   0x01801000u
#define VROM_OFFSET     0x05801000u
#define PAYLOAD_SIZE    0x06801000u

static uint8_t *cart_memory;

static void put32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

static uint32_t zero_crc(uint32_t length)
{
    static const uint8_t zeros[NAOMI_CART_IMAGE_VERIFY_CHUNK] = {0};
    uint32_t state = 0xFFFFFFFFu;
    while(length) {
        uint32_t n = length > sizeof(zeros) ? sizeof(zeros) : length;
        state = naomi_cart_crc32_update(state, zeros, n);
        length -= n;
    }
    return state ^ 0xFFFFFFFFu;
}

naomi_cart_dma_status_t naomi_cart_dma_read(
    uint32_t source_offset, void *destination, uint32_t length,
    uint32_t timeout_ms, naomi_cart_dma_result_t *result)
{
    if(!cart_memory || !destination || !length || !timeout_ms ||
       (source_offset & 31u) || ((uintptr_t)destination & 31u) ||
       (length & 31u) || (uint64_t)source_offset + length > TEST_IMAGE_SIZE)
        return NAOMI_CART_DMA_INVALID;
    memcpy(destination, cart_memory + source_offset, length);
    if(result) {
        result->transferred = length;
        result->last_count = length;
        result->last_status = 0;
        result->last_end = 0;
        result->elapsed_ms = 0;
    }
    return NAOMI_CART_DMA_OK;
}

static void build_test_header(void)
{
    uint8_t *h = cart_memory + HEADER_OFFSET;
    memset(h, 0, NAOMI_CART_IMAGE_HEADER_SIZE);
    memcpy(h, NAOMI_CART_IMAGE_MAGIC, 8);
    put32(h+8, NAOMI_CART_IMAGE_VERSION);
    put32(h+12, NAOMI_CART_IMAGE_HEADER_SIZE);
    put32(h+16, NAOMI_CART_IMAGE_PROFILE_VF3_REV_D);
    put32(h+20, NAOMI_CART_IMAGE_REGION_COUNT);
    put32(h+24, PAYLOAD_SIZE);
    put32(h+32, NAOMI_CART_REGION_FIXED_CROM);
    put32(h+36, FIXED_OFFSET);
    put32(h+40, 0x00800000u);
    put32(h+44, zero_crc(0x00800000u));
    put32(h+48, NAOMI_CART_REGION_BANKED_CROM);
    put32(h+52, BANKED_OFFSET);
    put32(h+56, 0x04000000u);
    put32(h+60, zero_crc(0x04000000u));
    put32(h+64, NAOMI_CART_REGION_VROM);
    put32(h+68, VROM_OFFSET);
    put32(h+72, 0x02000000u);
    put32(h+76, zero_crc(0x02000000u));
    put32(h+28, naomi_cart_crc32(h, NAOMI_CART_IMAGE_HEADER_SIZE));
}

int main(void)
{
    static uint8_t header[NAOMI_CART_IMAGE_HEADER_SIZE] __attribute__((aligned(32)));
    static uint8_t scratch[NAOMI_CART_IMAGE_VERIFY_CHUNK] __attribute__((aligned(32)));
    static uint8_t sample[64] __attribute__((aligned(32)));
    naomi_cart_image_layout_t layout;
    naomi_cart_dma_result_t io;
    uint32_t actual_crc = 0;

    cart_memory = (uint8_t *)calloc(1, TEST_IMAGE_SIZE);
    assert(cart_memory != NULL);
    build_test_header();

    assert(naomi_cart_image_mount(HEADER_OFFSET, header, 1000, &layout, &io) == NAOMI_CART_IMAGE_OK);
    assert(layout.payload_offset == HEADER_OFFSET);
    assert(naomi_cart_image_read_region(&layout, NAOMI_CART_REGION_BANKED_CROM,
        0x00012340u, sample, sizeof(sample), 1000, &io) == NAOMI_CART_IMAGE_OK);
    for(unsigned i = 0; i < sizeof(sample); ++i) assert(sample[i] == 0);
    assert(naomi_cart_image_read_region(&layout, NAOMI_CART_REGION_BANKED_CROM,
        0x03FFFFE0u, sample, sizeof(sample), 1000, &io) == NAOMI_CART_IMAGE_INVALID);
    assert(naomi_cart_image_verify_region(&layout, NAOMI_CART_REGION_FIXED_CROM,
        scratch, sizeof(scratch), 1000, &actual_crc, &io) == NAOMI_CART_IMAGE_OK);
    assert(actual_crc == naomi_cart_image_find_region(&layout,
        NAOMI_CART_REGION_FIXED_CROM)->crc32);

    layout.regions[0].crc32 ^= 1u;
    assert(naomi_cart_image_verify_region(&layout, NAOMI_CART_REGION_FIXED_CROM,
        scratch, sizeof(scratch), 1000, &actual_crc, &io) == NAOMI_CART_IMAGE_CHECKSUM_ERROR);

    free(cart_memory);
    puts("VF3NROM DMA reader mock: PASS");
    return 0;
}
