#include "cart_rom.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_SIZE 0x08000000u
#define FIXED_OFF  0x01001000u
#define BANKED_OFF 0x01801000u
#define VROM_OFF   0x05801000u
#define PAYLOAD_SZ 0x06801000u

static uint8_t *cart;
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static uint32_t crc_zeros(uint32_t n){static const uint8_t z[65536]={0};uint32_t s=0xFFFFFFFFu;while(n){uint32_t k=n>sizeof(z)?sizeof(z):n;s=naomi_cart_crc32_update(s,z,k);n-=k;}return s^0xFFFFFFFFu;}
naomi_cart_dma_status_t naomi_cart_dma_read(uint32_t off,void *dst,uint32_t len,uint32_t timeout,naomi_cart_dma_result_t *r){
 if(!cart||!dst||!timeout||(off&31u)||((uintptr_t)dst&31u)||(len&31u)||(uint64_t)off+len>IMAGE_SIZE)return NAOMI_CART_DMA_INVALID;
 memcpy(dst,cart+off,len);if(r){r->transferred=len;r->last_count=len;r->last_status=0;r->last_end=0;r->elapsed_ms=0;}return NAOMI_CART_DMA_OK;
}
static void make_header(void){uint8_t*h=cart+0x01000000u;memset(h,0,4096);memcpy(h,NAOMI_CART_IMAGE_MAGIC,8);put32(h+8,1);put32(h+12,4096);put32(h+16,1);put32(h+20,3);put32(h+24,PAYLOAD_SZ);
 put32(h+32,1);put32(h+36,FIXED_OFF);put32(h+40,0x800000);put32(h+44,crc_zeros(0x800000));
 put32(h+48,2);put32(h+52,BANKED_OFF);put32(h+56,0x4000000);put32(h+60,crc_zeros(0x4000000));
 put32(h+64,3);put32(h+68,VROM_OFF);put32(h+72,0x2000000);put32(h+76,crc_zeros(0x2000000));put32(h+28,naomi_cart_crc32(h,4096));}
int main(void){
 static uint8_t hdr[4096] __attribute__((aligned(32)));
 static uint8_t fixed[0x800000] __attribute__((aligned(32)));
 static uint8_t cache[NAOMI_CART_CROM_CACHE_SIZE] __attribute__((aligned(32)));
 naomi_cart_image_layout_t layout;naomi_cart_rom_t rom;naomi_cart_dma_result_t io;
 cart=calloc(1,IMAGE_SIZE);assert(cart);make_header();
 for(unsigned b=0;b<8;b++)for(unsigned p=0;p<128;p++)memset(cart+BANKED_OFF+b*0x800000u+p*0x10000u,(int)(0x20+b),32);
 assert(naomi_cart_image_mount(0x01000000u,hdr,1000,&layout,&io)==NAOMI_CART_IMAGE_OK);
 assert(naomi_cart_rom_init(&rom,&layout,fixed,sizeof(fixed),cache,sizeof(cache),1000,&io)==NAOMI_CART_IMAGE_OK);
 assert(naomi_cart_rom_fixed(&rom)==fixed);
 for(unsigned b=0;b<8;b++){assert(naomi_cart_rom_select_bank(&rom,b)==NAOMI_CART_IMAGE_OK);const uint8_t*p=naomi_cart_rom_banked_page(&rom,0,&io);assert(p&&p[0]==(uint8_t)(0x20+b));}
 assert(naomi_cart_rom_select_bank(&rom,15)==NAOMI_CART_IMAGE_OK);
 const uint8_t*p=naomi_cart_rom_banked_page(&rom,0,&io);assert(p&&p[0]==0x27);
 p=naomi_cart_rom_banked_page(&rom,0x1234,&io);assert(p&&p[0]==0x27);
 p=naomi_cart_rom_banked_page(&rom,0x10000,&io);assert(p&&p[0]==0x27);
 assert(naomi_cart_rom_banked_page(&rom,NAOMI_CART_CROM_BANK_SIZE,&io)==0);
 free(cart);puts("VF3 CROM 64 KiB page cache: PASS");return 0;
}
