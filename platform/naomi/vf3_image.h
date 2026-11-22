#ifndef VF3_NAOMI_VF3_IMAGE_H
#define VF3_NAOMI_VF3_IMAGE_H

#include "cart_rom.h"
#include <stdint.h>

#define NAOMI_VF3_IMAGE_HEADER_OFFSET 0x01000000u
#define NAOMI_VF3_IMAGE_FIXED_SIZE NAOMI_CART_CROM_FIXED_SIZE
#define NAOMI_VF3_IMAGE_BANK_CACHE_SIZE NAOMI_CART_CROM_CACHE_SIZE
#define NAOMI_VF3_IMAGE_VERIFY_SIZE NAOMI_CART_IMAGE_VERIFY_CHUNK

#if defined(__cplusplus)
#define NAOMI_VF3_ALIGN32 alignas(32)
#else
#define NAOMI_VF3_ALIGN32 _Alignas(32)
#endif

typedef enum {
    NAOMI_VF3_IMAGE_OK = 0,
    NAOMI_VF3_IMAGE_INVALID = -1,
    NAOMI_VF3_IMAGE_MOUNT_ERROR = -2,
    NAOMI_VF3_IMAGE_ROM_ERROR = -3,
    NAOMI_VF3_IMAGE_FIXED_CRC_ERROR = -4,
    NAOMI_VF3_IMAGE_BANKED_VERIFY_ERROR = -5,
    NAOMI_VF3_IMAGE_BANKED_CRC_ERROR = -6,
    NAOMI_VF3_IMAGE_VROM_VERIFY_ERROR = -7,
    NAOMI_VF3_IMAGE_VROM_CRC_ERROR = -8
} naomi_vf3_image_status_t;

/* Keep this object alive for the full Model 3 session: both ROM bridges retain
 * pointers into its layout and ROM-cache state. All large buffers are supplied
 * by the caller, so this layer performs no allocation. */
typedef struct {
    naomi_cart_image_layout_t layout;
    naomi_cart_rom_t crom;
    NAOMI_VF3_ALIGN32 uint8_t header[NAOMI_CART_IMAGE_HEADER_SIZE];
    uint8_t mounted;
} naomi_vf3_image_t;

/* Mounts the VF3NROM v1 header at 16 MiB, loads fixed CROM, verifies all three
 * regions, and binds the Model 3 CROM/VROM bridges. fixed_crom, page_cache and
 * verify_scratch must be 32-byte aligned caller-owned buffers. */
#ifdef __cplusplus
extern "C" {
#endif

naomi_vf3_image_status_t naomi_vf3_image_mount(
    naomi_vf3_image_t *image,
    void *fixed_crom, uint32_t fixed_crom_size,
    void *page_cache, uint32_t page_cache_size,
    void *verify_scratch, uint32_t verify_scratch_size,
    uint32_t timeout_ms, naomi_cart_dma_result_t *io_result);

#ifdef __cplusplus
}
#endif

#undef NAOMI_VF3_ALIGN32

#endif
