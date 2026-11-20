ifeq ($(KOS_SUBARCH),)
$(error KOS_SUBARCH is unset. Source the Naomi KOS environ.sh first)
endif
ifneq ($(KOS_SUBARCH),naomi)
$(error Wrong KOS target '$(KOS_SUBARCH)'. This target requires KOS_SUBARCH=naomi)
endif
ifeq ($(KOS_BASE),)
$(error KOS_BASE is unset. Source the Naomi KOS environ.sh first)
endif

TARGET = naomi_sh4_fpu_probe.elf
ROM = naomi_sh4_fpu_probe-$(BUILD_VERSION).bin
OBJS = main.o
NAOMIBINTOOL ?= $(KOS_BASE)/utils/naomibintool/naomibintool

CFLAGS += -O2 -fno-fast-math

all: $(TARGET)
rom: $(ROM)

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) -lm

$(NAOMIBINTOOL):
	$(MAKE) -C $(KOS_BASE)/utils/naomibintool CC=cc

$(ROM): $(TARGET) $(NAOMIBINTOOL)
	$(NAOMIBINTOOL) build $(ROM) -d "CDMN" -t "SH4 FPU Probe $(BUILD_VERSION)" -e $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(TARGET) $(ROM) $(OBJS)

.PHONY: all rom clean
