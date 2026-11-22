#ifndef VF3_NAOMI_CART_VROM_H
#define VF3_NAOMI_CART_VROM_H

#include "cart_image.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NAOMI_CART_VROM_LOGICAL_SIZE 0x02000000u
#define NAOMI_CART_VROM_WORD_COUNT  0x00800000u
#define NAOMI_CART_VROM_SCRATCH_SIZE 0x00010000u
#define NAOMI_CART_VROM_FIFO_BYTES  0x00100000u

typedef enum {
    NAOMI_CART_VROM_OK = 0,
    NAOMI_CART_VROM_INVALID = -1,
    NAOMI_CART_VROM_IO_ERROR = -2,
    NAOMI_CART_VROM_SINK_ERROR = -3
} naomi_cart_vrom_status_t;

typedef int (*naomi_cart_vrom_word_sink_t)(void *context, uint32_t word);

typedef struct {
    uint32_t declared_words;
    uint32_t emitted_words;
} naomi_cart_vrom_result_t;

/* Streams the upstream Real3D texture-port operation without retaining VROM.
 * word_address is the original 24-bit word address. The packed image stores
 * one 32 MiB VROM copy; the upstream 64 MiB allocation mirrors it twice.
 * scratch must be 32-byte aligned and at least 64 KiB. The sink is called
 * synchronously for each word in original order. */
naomi_cart_vrom_status_t naomi_cart_vrom_texture_port(
    const naomi_cart_image_layout_t *image, uint32_t word_address,
    uint32_t fifo_words_used, uint32_t fifo_capacity_words,
    void *scratch, uint32_t scratch_size, uint32_t timeout_ms,
    naomi_cart_vrom_word_sink_t sink, void *sink_context,
    naomi_cart_vrom_result_t *result, naomi_cart_dma_result_t *io_result);

#ifdef __cplusplus
}
#endif

#endif
