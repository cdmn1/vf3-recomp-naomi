ifeq ($(KOS_SUBARCH),)
$(error KOS_SUBARCH is unset. Source the Naomi KOS environ.sh first)
endif
ifneq ($(KOS_SUBARCH),naomi)
$(error Wrong KOS target '$(KOS_SUBARCH)'. This target requires KOS_SUBARCH=naomi)
endif
ifeq ($(KOS_BASE),)
$(error KOS_BASE is unset. Source the Naomi KOS environ.sh first)
endif

TARGET = naomi_cart_read_probe.elf
ROM = naomi_cart_read_probe-$(BUILD_VERSION).bin
OBJS = main.o ../../platform/naomi/cart_dma.o ../../platform/naomi/cart_image.o ../../platform/naomi/cart_image_format.o ../../platform/naomi/cart_rom.o
NAOMIBINTOOL ?= $(KOS_BASE)/utils/naomibintool/naomibintool

CFLAGS += -O2 -fno-fast-math

all: $(TARGET)
rom: $(ROM)

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) -lm

$(NAOMIBINTOOL):
	$(MAKE) -C $(KOS_BASE)/utils/naomibintool CC=cc

$(ROM): $(TARGET) $(NAOMIBINTOOL)
	$(NAOMIBINTOOL) build $(ROM) -d "CDMN" -t "Naomi Cart Read $(BUILD_VERSION)" -e $(TARGET)

include $(KOS_BASE)/Makefile.rules

../../platform/naomi/cart_dma.o: ../../platform/naomi/cart_dma.c ../../platform/naomi/cart_dma.h
	kos-cc -O2 -c $< -o $@

../../platform/naomi/cart_image.o: ../../platform/naomi/cart_image.c ../../platform/naomi/cart_image.h ../../platform/naomi/cart_image_format.h ../../platform/naomi/cart_dma.h
	kos-cc -O2 -c $< -o $@

../../platform/naomi/cart_image_format.o: ../../platform/naomi/cart_image_format.c ../../platform/naomi/cart_image_format.h
	kos-cc -O2 -c $< -o $@

../../platform/naomi/cart_rom.o: ../../platform/naomi/cart_rom.c ../../platform/naomi/cart_rom.h ../../platform/naomi/cart_image.h
	kos-cc -O2 -c $< -o $@

clean:
	-rm -f $(TARGET) $(ROM) main.o ../../platform/naomi/cart_dma.o ../../platform/naomi/cart_image.o ../../platform/naomi/cart_image_format.o ../../platform/naomi/cart_rom.o

.PHONY: all rom clean
