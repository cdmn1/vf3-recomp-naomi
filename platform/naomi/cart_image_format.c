#include "cart_image_format.h"

#include <stddef.h>
#include <string.h>

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const uint32_t crc_table[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

uint32_t naomi_cart_crc32_update(uint32_t state, const void *data, uint32_t length)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = state;
    for(uint32_t i = 0; i < length; ++i) {
        crc ^= p[i];
        crc = (crc >> 4) ^ crc_table[crc & 0x0Fu];
        crc = (crc >> 4) ^ crc_table[crc & 0x0Fu];
    }
    return crc;
}

uint32_t naomi_cart_crc32(const void *data, uint32_t length)
{
    return naomi_cart_crc32_update(0xFFFFFFFFu, data, length) ^ 0xFFFFFFFFu;
}

static uint32_t header_crc32(const uint8_t *header)
{
    static const uint8_t zero[4] = {0, 0, 0, 0};
    uint32_t crc = 0xFFFFFFFFu;
    crc = naomi_cart_crc32_update(crc, header, 28);
    crc = naomi_cart_crc32_update(crc, zero, sizeof(zero));
    crc = naomi_cart_crc32_update(crc, header + 32,
                                  NAOMI_CART_IMAGE_HEADER_SIZE - 32);
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t expected_region_size(uint32_t id)
{
    switch(id) {
    case NAOMI_CART_REGION_FIXED_CROM: return 0x00800000u;
    case NAOMI_CART_REGION_BANKED_CROM: return 0x04000000u;
    case NAOMI_CART_REGION_VROM: return 0x02000000u;
    default: return 0;
    }
}

naomi_cart_image_format_status_t naomi_cart_image_parse_header(
    const uint8_t header[NAOMI_CART_IMAGE_HEADER_SIZE],
    uint32_t header_offset,
    naomi_cart_image_layout_t *layout)
{
    uint32_t version, header_size, profile_id, count, payload_size;
    uint32_t stored_crc;
    uint64_t payload_end;
    uint32_t seen = 0;

    if(!header || !layout)
        return NAOMI_CART_IMAGE_FORMAT_BAD_ARGUMENT;
    if(memcmp(header, NAOMI_CART_IMAGE_MAGIC, 8) != 0)
        return NAOMI_CART_IMAGE_FORMAT_BAD_MAGIC;

    version = read_le32(header + 8);
    header_size = read_le32(header + 12);
    profile_id = read_le32(header + 16);
    count = read_le32(header + 20);
    payload_size = read_le32(header + 24);
    stored_crc = read_le32(header + 28);

    if(version != NAOMI_CART_IMAGE_VERSION)
        return NAOMI_CART_IMAGE_FORMAT_BAD_VERSION;
    if(header_size != NAOMI_CART_IMAGE_HEADER_SIZE ||
       profile_id != NAOMI_CART_IMAGE_PROFILE_VF3_REV_D ||
       count != NAOMI_CART_IMAGE_REGION_COUNT ||
       payload_size < NAOMI_CART_IMAGE_HEADER_SIZE)
        return NAOMI_CART_IMAGE_FORMAT_BAD_HEADER;
    if(header_crc32(header) != stored_crc)
        return NAOMI_CART_IMAGE_FORMAT_BAD_HEADER_CRC;

    payload_end = (uint64_t)header_offset + payload_size;
    if((header_offset & 31u) != 0u || payload_end > 0x80000000ull ||
       payload_end <= (uint64_t)header_offset + NAOMI_CART_IMAGE_HEADER_SIZE)
        return NAOMI_CART_IMAGE_FORMAT_BAD_RANGE;

    memset(layout, 0, sizeof(*layout));
    layout->payload_offset = header_offset;
    layout->payload_size = payload_size;
    layout->profile_id = profile_id;

    for(uint32_t i = 0; i < count; ++i) {
        const uint8_t *d = header + 32 + i * 16;
        naomi_cart_image_region_t *r = &layout->regions[i];
        uint64_t region_end;
        r->id = read_le32(d);
        r->offset = read_le32(d + 4);
        r->size = read_le32(d + 8);
        r->crc32 = read_le32(d + 12);

        if(r->id < NAOMI_CART_REGION_FIXED_CROM ||
           r->id > NAOMI_CART_REGION_VROM ||
           (seen & (1u << r->id)) != 0u ||
           r->size != expected_region_size(r->id))
            return NAOMI_CART_IMAGE_FORMAT_BAD_REGION;
        seen |= 1u << r->id;

        region_end = (uint64_t)r->offset + r->size;
        if((r->offset & 31u) != 0u ||
           r->offset < header_offset + NAOMI_CART_IMAGE_HEADER_SIZE ||
           region_end > payload_end || region_end > 0x80000000ull)
            return NAOMI_CART_IMAGE_FORMAT_BAD_RANGE;

        for(uint32_t j = 0; j < i; ++j) {
            const naomi_cart_image_region_t *other = &layout->regions[j];
            uint64_t other_end = (uint64_t)other->offset + other->size;
            if((uint64_t)r->offset < other_end &&
               (uint64_t)other->offset < region_end)
                return NAOMI_CART_IMAGE_FORMAT_BAD_RANGE;
        }
    }

    if(seen != ((1u << NAOMI_CART_REGION_FIXED_CROM) |
                (1u << NAOMI_CART_REGION_BANKED_CROM) |
                (1u << NAOMI_CART_REGION_VROM)))
        return NAOMI_CART_IMAGE_FORMAT_BAD_REGION;

    return NAOMI_CART_IMAGE_FORMAT_OK;
}

const naomi_cart_image_region_t *naomi_cart_image_find_region(
    const naomi_cart_image_layout_t *layout, uint32_t region_id)
{
    if(!layout) return NULL;
    for(uint32_t i = 0; i < NAOMI_CART_IMAGE_REGION_COUNT; ++i)
        if(layout->regions[i].id == region_id)
            return &layout->regions[i];
    return NULL;
}
