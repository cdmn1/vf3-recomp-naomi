#ifndef VF3_NAOMI_CART_VROM_BRIDGE_H
#define VF3_NAOMI_CART_VROM_BRIDGE_H

#include "cart_image_format.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bind the mounted NetDIMM image before the Model 3 board begins execution. */
void naomi_model3_vrom_bind(const naomi_cart_image_layout_t *image);

/* Returns 1 when the Naomi streaming path handled the port access, else 0
 * when no image is bound and the caller may use its resident VROM fallback. */
int naomi_model3_vrom_port(uint32_t word_address, void *texture_fifo);

/* Last transport/sink status; zero means the most recent handled request passed. */
int naomi_model3_vrom_last_status(void);

#ifdef __cplusplus
}
#endif

#endif
