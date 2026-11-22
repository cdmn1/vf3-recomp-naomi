#ifndef VF3_NAOMI_CART_VROM_BRIDGE_H
#define VF3_NAOMI_CART_VROM_BRIDGE_H

#include "cart_image_format.h"
#include "cart_rom.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bind the mounted NetDIMM image before the Model 3 board begins execution. */
void naomi_model3_vrom_bind(const naomi_cart_image_layout_t *image);
int naomi_model3_vrom_port(uint32_t word_address, void *texture_fifo);
int naomi_model3_vrom_read_words(uint32_t word_address, void *destination, uint32_t word_count);
int naomi_model3_vrom_last_status(void);

/* Bind the initialized resident-fixed-CROM / bank-page-cache state. */
void naomi_model3_crom_bind(naomi_cart_rom_t *rom);
const uint8_t *naomi_model3_crom_fixed(void);
uint8_t *naomi_model3_crom_fixed_mutable(void);
int naomi_model3_crom_is_bound(void);
int naomi_model3_crom_select_bank(uint8_t bank_register);
uint8_t naomi_model3_crom_read8(uint32_t bank_offset);
uint16_t naomi_model3_crom_read16(uint32_t bank_offset);
uint32_t naomi_model3_crom_read32(uint32_t bank_offset);
int naomi_model3_crom_last_status(void);

#ifdef __cplusplus
}
#endif

#endif
