#include "cart_dma.h"

#include <kos.h>
#include <kos/cache.h>
#include <kos/timer.h>
#include <stdint.h>

#define CART_DMA_OFFSETH (*(volatile uint16_t *)0xA05F700Cu)
#define CART_DMA_OFFSETL (*(volatile uint16_t *)0xA05F7010u)
#define G1_GDSTAR        (*(volatile uint32_t *)0xA05F7404u)
#define G1_GDLEN         (*(volatile uint32_t *)0xA05F7408u)
#define G1_GDDIR         (*(volatile uint32_t *)0xA05F740Cu)
#define G1_GDEN          (*(volatile uint32_t *)0xA05F7414u)
#define G1_GDST          (*(volatile uint32_t *)0xA05F7418u)
#define G1_GDSTARD       (*(volatile uint32_t *)0xA05F74F4u)
#define G1_GDLEND        (*(volatile uint32_t *)0xA05F74F8u)

static int aligned32(uintptr_t p) {
    return (p & (NAOMI_CART_DMA_ALIGNMENT - 1u)) == 0u;
}

naomi_cart_dma_status_t naomi_cart_dma_read(
    uint32_t source_offset, void *destination, uint32_t length,
    uint32_t timeout_ms, naomi_cart_dma_result_t *result) {
    naomi_cart_dma_result_t r = {0, 0, 0, 0, 0};
    uint8_t *out = (uint8_t *)destination;
    uint32_t remaining = length, offset = source_offset;
    uint64_t call_start = timer_ms_gettime64();

    if(result) *result = r;
    if(!destination || !length || !timeout_ms ||
       !aligned32((uintptr_t)destination) || !aligned32(source_offset) ||
       !aligned32(length) || source_offset > 0x7FFFFFE0u ||
       length - 1u > 0x7FFFFFFFu - source_offset)
        return NAOMI_CART_DMA_INVALID;

    while(remaining) {
        uint32_t chunk = remaining;
        uint32_t count;
        uint64_t transfer_start;

        if(chunk > NAOMI_CART_DMA_MAX_CHUNK)
            chunk = NAOMI_CART_DMA_MAX_CHUNK;

        if((G1_GDST & 1u) || (G1_GDEN & 1u)) {
            r.last_status = G1_GDST;
            r.last_count = G1_GDLEND;
            r.last_end = G1_GDSTARD;
            r.elapsed_ms = timer_ms_gettime64() - call_start;
            if(result) *result = r;
            return NAOMI_CART_DMA_BUSY;
        }

        /* Discard dirty CPU cache lines before the device writes RAM. */
        dcache_inval_range((uintptr_t)out, chunk);
        CART_DMA_OFFSETL = (uint16_t)(offset & 0xFFFFu);
        CART_DMA_OFFSETH = (uint16_t)((offset >> 16) & 0x7FFFu);
        G1_GDSTAR = (uint32_t)((uintptr_t)out & 0x1FFFFFE0u);
        G1_GDLEN = chunk;
        G1_GDDIR = 1u;
        G1_GDEN = 1u;

        transfer_start = timer_ms_gettime64();
        G1_GDST = 1u;
        while(G1_GDST & 1u) {
            if(timer_ms_gettime64() - transfer_start >= timeout_ms) {
                G1_GDEN = 0u;
                r.last_status = G1_GDST;
                r.last_count = G1_GDLEND;
                r.last_end = G1_GDSTARD;
                r.elapsed_ms = timer_ms_gettime64() - call_start;
                if(result) *result = r;
                return NAOMI_CART_DMA_TIMEOUT;
            }
            thd_pass();
        }

        r.last_status = G1_GDST;
        count = G1_GDLEND;
        r.last_count = count;
        r.last_end = G1_GDSTARD;
        dcache_inval_range((uintptr_t)out, chunk);
        G1_GDEN = 0u;

        if((G1_GDST & 1u) || count < chunk) {
            r.elapsed_ms = timer_ms_gettime64() - call_start;
            if(result) *result = r;
            return NAOMI_CART_DMA_SHORT_TRANSFER;
        }

        r.transferred += chunk;
        offset += chunk;
        out += chunk;
        remaining -= chunk;
    }

    r.elapsed_ms = timer_ms_gettime64() - call_start;
    if(result) *result = r;
    return NAOMI_CART_DMA_OK;
}
