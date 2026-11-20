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

TARGET = vf3_ppc_interpreter_smoke.elf
ROM = vf3_ppc_interpreter_smoke-$(BUILD_VERSION).bin
OBJS = main.o ppc.o
NAOMIBINTOOL ?= $(KOS_BASE)/utils/naomibintool/naomibintool

CXXFLAGS += -std=gnu++17 -O2
CPPFLAGS += -I$(VF3_RECOMP_DIR)/src/ppc

all: $(TARGET)
rom: $(ROM)

ppc.o: $(VF3_RECOMP_DIR)/src/ppc/ppc.cpp $(VF3_RECOMP_DIR)/src/ppc/ppc.h
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	kos-c++ -o $@ $(OBJS) -lm

$(NAOMIBINTOOL):
	$(MAKE) -C $(KOS_BASE)/utils/naomibintool CC=cc

$(ROM): $(TARGET) $(NAOMIBINTOOL)
	$(NAOMIBINTOOL) build $(ROM) -d "CDMN" -t "VF3 PPC Smoke $(BUILD_VERSION)" -e $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(TARGET) $(ROM) $(OBJS)

.PHONY: all rom clean
