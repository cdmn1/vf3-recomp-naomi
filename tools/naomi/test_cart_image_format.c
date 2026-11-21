#include "cart_image_format.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

static void make_header(uint8_t *h)
{
    memset(h, 0, NAOMI_CART_IMAGE_HEADER_SIZE);
    memcpy(h, NAOMI_CART_IMAGE_MAGIC, 8);
    put32(h+8, 1); put32(h+12, 4096); put32(h+16, 1);
    put32(h+20, 3); put32(h+24, 0x06801000u);
    put32(h+32, 1); put32(h+36, 0x01001000u); put32(h+40, 0x00800000u); put32(h+44, 0x11111111u);
    put32(h+48, 2); put32(h+52, 0x01801000u); put32(h+56, 0x04000000u); put32(h+60, 0x22222222u);
    put32(h+64, 3); put32(h+68, 0x05801000u); put32(h+72, 0x02000000u); put32(h+76, 0x33333333u);
    put32(h+28, 0);
    put32(h+28, naomi_cart_crc32(h, NAOMI_CART_IMAGE_HEADER_SIZE));
}

int main(void)
{
    uint8_t h[NAOMI_CART_IMAGE_HEADER_SIZE];
    naomi_cart_image_layout_t layout;
    assert(naomi_cart_crc32("123456789", 9) == 0xCBF43926u);
    make_header(h);
    assert(naomi_cart_image_parse_header(h, 0x01000000u, &layout) == NAOMI_CART_IMAGE_FORMAT_OK);
    assert(layout.payload_offset == 0x01000000u);
    assert(layout.payload_size == 0x06801000u);
    assert(naomi_cart_image_find_region(&layout, 1)->size == 0x00800000u);
    assert(naomi_cart_image_find_region(&layout, 2)->size == 0x04000000u);
    assert(naomi_cart_image_find_region(&layout, 3)->size == 0x02000000u);

    h[100] ^= 1;
    assert(naomi_cart_image_parse_header(h, 0x01000000u, &layout) == NAOMI_CART_IMAGE_FORMAT_BAD_HEADER_CRC);
    make_header(h);
    put32(h+52, 0x01001000u);
    put32(h+28, 0);
    put32(h+28, naomi_cart_crc32(h, NAOMI_CART_IMAGE_HEADER_SIZE));
    assert(naomi_cart_image_parse_header(h, 0x01000000u, &layout) == NAOMI_CART_IMAGE_FORMAT_BAD_RANGE);
    make_header(h);
    put32(h+40, 0x007FFFFFu);
    put32(h+28, 0);
    put32(h+28, naomi_cart_crc32(h, NAOMI_CART_IMAGE_HEADER_SIZE));
    assert(naomi_cart_image_parse_header(h, 0x01000000u, &layout) == NAOMI_CART_IMAGE_FORMAT_BAD_REGION);
    puts("VF3NROM header parser: PASS");
    return 0;
}
