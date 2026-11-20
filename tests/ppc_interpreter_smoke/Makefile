ifeq ($(KOS_SUBARCH),)
$(error KOS_SUBARCH is unset. Source the Naomi KOS environ.sh first)
endif
ifneq ($(KOS_SUBARCH),naomi)
$(error Wrong KOS target '$(KOS_SUBARCH)'. This target requires KOS_SUBARCH=naomi)
endif
ifeq ($(KOS_BASE),)
$(error KOS_BASE is unset. Source the Naomi KOS environ.sh first)
endif
ifeq ($(VF3_RECOMP_DIR),)
$(error VF3_RECOMP_DIR is unset. Point it at the pinned vf3-recomp checkout)
endif
SOFTFLOAT_DIR ?= $(abspath ../../softfloat)
SOFTFLOAT_BUILD = $(SOFTFLOAT_DIR)/build/Linux-x86_64-GCC

TARGET = vf3_ppc_interpreter_smoke.elf
ROM = vf3_ppc_interpreter_smoke-$(BUILD_VERSION).bin
OBJS = main.o ppc.o
NAOMIBINTOOL ?= $(KOS_BASE)/utils/naomibintool/naomibintool

CXXFLAGS += -std=gnu++17 -O2
CPPFLAGS += -I$(VF3_RECOMP_DIR)/src/ppc
CPPFLAGS += -I$(SOFTFLOAT_DIR)/source/include -I$(SOFTFLOAT_DIR)/source/8086-SSE

all: $(TARGET)
rom: $(ROM)

# Compile the real upstream Model 3 board core for SH-4 without linking or
# allocating its large emulated memory regions. This isolates target portability.
model3-compile: model3.o

model3.o: $(VF3_RECOMP_DIR)/src/machine/model3.cpp $(VF3_RECOMP_DIR)/src/machine/model3.h
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -I$(VF3_RECOMP_DIR)/src/machine -c $< -o $@

ppc.o: $(VF3_RECOMP_DIR)/src/ppc/ppc.cpp $(VF3_RECOMP_DIR)/src/ppc/ppc.h patch_ppc_softfloat.py
	python3 patch_ppc_softfloat.py $(VF3_RECOMP_DIR)/src/ppc/ppc.cpp
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(SOFTFLOAT_BUILD)/softfloat.a
	kos-c++ -o $@ $(OBJS) $(SOFTFLOAT_BUILD)/softfloat.a -lm

$(NAOMIBINTOOL):
	$(MAKE) -C $(KOS_BASE)/utils/naomibintool CC=cc

$(ROM): $(TARGET) $(NAOMIBINTOOL)
	$(NAOMIBINTOOL) build $(ROM) -d "CDMN" -t "VF3 PPC Smoke $(BUILD_VERSION)" -e $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(TARGET) $(ROM) $(OBJS) model3.o

.PHONY: all rom model3-compile clean
