#include "cart_vrom.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *cart;static uint32_t received[16],received_count;
naomi_cart_dma_status_t naomi_cart_dma_read(uint32_t offset,void *destination,uint32_t length,uint32_t timeout_ms,naomi_cart_dma_result_t *result){
 if(!cart||!destination||!timeout_ms||(offset&31u)||(length&31u)||((uintptr_t)destination&31u)||(uint64_t)offset+length>0x04000000u)return NAOMI_CART_DMA_INVALID;
 memcpy(destination,cart+offset,length);if(result){result->transferred=length;result->last_count=length;result->last_status=0;result->last_end=0;result->elapsed_ms=0;}return NAOMI_CART_DMA_OK;
}
static int collect(void *context,uint32_t word){(void)context;assert(received_count<16);received[received_count++]=word;return 1;}
int main(void){
 const uint32_t region_offset=0x1000u;naomi_cart_image_layout_t image;naomi_cart_vrom_result_t result;naomi_cart_dma_result_t io;
 static uint8_t scratch[NAOMI_CART_VROM_SCRATCH_SIZE] __attribute__((aligned(32)));
 cart=calloc(1,0x04000000u);assert(cart);memset(&image,0,sizeof(image));image.regions[2].id=NAOMI_CART_REGION_VROM;image.regions[2].offset=region_offset;image.regions[2].size=NAOMI_CART_VROM_LOGICAL_SIZE;
 cart[region_offset+NAOMI_CART_VROM_LOGICAL_SIZE-4]=30;
 for(uint32_t i=0;i<3;i++){uint32_t v=0x11223344u+i;memcpy(cart+region_offset+i*4u,&v,4);}
 assert(naomi_cart_vrom_texture_port(&image,0x00FFFFFFu,0,8,scratch,sizeof(scratch),1000,collect,0,&result,&io)==NAOMI_CART_VROM_OK);
 assert(result.declared_words==4&&result.emitted_words==4);assert(received_count==4);assert(received[0]==30);assert(received[1]==0x11223344u);assert(received[2]==0x11223345u);assert(received[3]==0x11223346u);
 received_count=0;
 assert(naomi_cart_vrom_texture_port(&image,0x00FFFFFFu,7,8,scratch,sizeof(scratch),1000,collect,0,&result,&io)==NAOMI_CART_VROM_OK);
 assert(result.declared_words==4&&result.emitted_words==1);assert(received_count==1&&received[0]==30);
 received_count=0;
 assert(naomi_cart_vrom_texture_port(&image,0,8,8,scratch,sizeof(scratch),1000,collect,0,&result,&io)==NAOMI_CART_VROM_OK);
 assert(result.declared_words>0&&result.emitted_words==0);assert(received_count==0);
 free(cart);puts("VF3 VROM texture-port streaming: PASS");return 0;
}
