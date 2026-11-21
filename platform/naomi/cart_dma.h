#ifndef VF3_NAOMI_CART_DMA_H
#define VF3_NAOMI_CART_DMA_H
#include <stdint.h>

#define NAOMI_CART_DMA_ALIGNMENT 32u
#define NAOMI_CART_DMA_MAX_CHUNK 0x00400000u

typedef enum {
    NAOMI_CART_DMA_OK = 0,
    NAOMI_CART_DMA_INVALID = -1,
    NAOMI_CART_DMA_BUSY = -2,
    NAOMI_CART_DMA_TIMEOUT = -3,
    NAOMI_CART_DMA_SHORT_TRANSFER = -4
} naomi_cart_dma_status_t;

typedef struct {
    uint32_t transferred;
    uint32_t last_count;
    uint32_t last_status;
    uint32_t last_end;
    uint64_t elapsed_ms;
} naomi_cart_dma_result_t;

/* Synchronous, allocation-free cartridge read. Source, destination and
 * length must be 32-byte aligned. Requests above 4 MiB are split into
 * completed G1 transactions. Caller owns and retains the destination. */
naomi_cart_dma_status_t naomi_cart_dma_read(
    uint32_t source_offset, void *destination, uint32_t length,
    uint32_t timeout_ms, naomi_cart_dma_result_t *result);
#endif
