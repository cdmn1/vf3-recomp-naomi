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
INTEGRATION_TARGET = naomi_model3_vrom_integration.elf
INTEGRATION_ROM = naomi_model3_vrom_integration-$(BUILD_VERSION).bin
INTEGRATION_OBJS = integration_main.o r3d_scene.o cart_vrom_bridge.o vf3_image.o vf3_image_runtime.o cart_dma.o cart_image_format.o cart_image.o cart_rom.o cart_vrom.o
OBJS = main.o ppc.o
NAOMIBINTOOL ?= $(KOS_BASE)/utils/naomibintool/naomibintool

CXXFLAGS += -std=gnu++17 -O2
CPPFLAGS += -I$(VF3_RECOMP_DIR)/src/ppc
CPPFLAGS += -I$(VF3_RECOMP_DIR)/src/machine
CPPFLAGS += -I$(VF3_RECOMP_DIR)/src/video
CPPFLAGS += -I../../platform/naomi
CPPFLAGS += -I$(SOFTFLOAT_DIR)/source/include -I$(SOFTFLOAT_DIR)/source/8086-SSE

all: $(TARGET)
rom: $(ROM)

# Compile the Naomi board core, complete NetDIMM ROM path, and scene preparation.
# Raster geometry preparation runs without CPU colour/depth/stencil buffers.
model3-compile: model3.o cart_vrom_bridge.o vf3_image.o vf3_image_runtime.o cart_dma.o cart_image_format.o cart_image.o cart_rom.o cart_vrom.o r3d_scene.o r3d_raster.o

# Link the real Naomi cartridge/image bridge and the patched upstream SceneBuilder.
# The probe uses a synthetic NetDIMM image, not commercial VF3 ROM data.
model3-integration: $(INTEGRATION_TARGET)
integration-rom: $(INTEGRATION_ROM)

model3.o: $(VF3_RECOMP_DIR)/src/machine/model3.cpp $(VF3_RECOMP_DIR)/src/machine/model3.h patch_model3_naomi.py ../../platform/naomi/cart_vrom_bridge.h ../../platform/naomi/vf3_image.h ../../platform/naomi/naomi_pvr_vector.h
	python3 patch_model3_naomi.py $(VF3_RECOMP_DIR)/src/machine/model3.cpp
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -DVF3_NAOMI -c $< -o $@

cart_vrom_bridge.o: ../../platform/naomi/cart_vrom_bridge.cpp ../../platform/naomi/cart_vrom_bridge.h ../../platform/naomi/cart_vrom.h ../../platform/naomi/cart_rom.h
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

vf3_image.o: ../../platform/naomi/vf3_image.c ../../platform/naomi/vf3_image.h ../../platform/naomi/cart_vrom_bridge.h
	kos-cc -O2 -c $< -o $@

vf3_image_runtime.o: ../../platform/naomi/vf3_image_runtime.cpp ../../platform/naomi/vf3_image.h
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

cart_dma.o: ../../platform/naomi/cart_dma.c ../../platform/naomi/cart_dma.h
	kos-cc -O2 -c $< -o $@

cart_image_format.o: ../../platform/naomi/cart_image_format.c ../../platform/naomi/cart_image_format.h
	kos-cc -O2 -c $< -o $@

cart_image.o: ../../platform/naomi/cart_image.c ../../platform/naomi/cart_image.h ../../platform/naomi/cart_dma.h ../../platform/naomi/cart_image_format.h
	kos-cc -O2 -c $< -o $@

cart_rom.o: ../../platform/naomi/cart_rom.c ../../platform/naomi/cart_rom.h ../../platform/naomi/cart_image.h
	kos-cc -O2 -c $< -o $@

cart_vrom.o: ../../platform/naomi/cart_vrom.c ../../platform/naomi/cart_vrom.h ../../platform/naomi/cart_image.h
	kos-cc -O2 -c $< -o $@

r3d_scene.o: $(VF3_RECOMP_DIR)/src/video/r3d_scene.cpp $(VF3_RECOMP_DIR)/src/video/r3d_scene.h $(VF3_RECOMP_DIR)/src/video/parallel.h ../../platform/naomi/cart_vrom_bridge.h ../../platform/naomi/cart_vrom.h model3.o
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -DVF3_NAOMI -c $< -o $@

r3d_raster.o: $(VF3_RECOMP_DIR)/src/video/r3d_raster.cpp $(VF3_RECOMP_DIR)/src/video/r3d_raster.h $(VF3_RECOMP_DIR)/src/video/r3d_scene.h $(VF3_RECOMP_DIR)/src/video/parallel.h model3.o
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -DVF3_NAOMI -c $< -o $@

integration_main.o: integration_main.cpp $(VF3_RECOMP_DIR)/src/video/r3d_scene.h $(VF3_RECOMP_DIR)/src/machine/model3.h ../../platform/naomi/vf3_image.h
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -DVF3_NAOMI -c $< -o $@

$(INTEGRATION_TARGET): $(INTEGRATION_OBJS)
	kos-c++ -o $@ $(INTEGRATION_OBJS) -lm

$(INTEGRATION_ROM): $(INTEGRATION_TARGET) $(NAOMIBINTOOL)
	$(NAOMIBINTOOL) build $(INTEGRATION_ROM) -d "CDMN" -t "VF3 VROM Integration $(BUILD_VERSION)" -e $(INTEGRATION_TARGET)

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
	-rm -f $(TARGET) $(ROM) $(OBJS) $(INTEGRATION_TARGET) $(INTEGRATION_ROM) integration_main.o model3.o cart_vrom_bridge.o vf3_image.o vf3_image_runtime.o cart_dma.o cart_image_format.o cart_image.o cart_rom.o cart_vrom.o r3d_scene.o r3d_raster.o

.PHONY: all rom model3-compile model3-integration integration-rom clean
