TOPNAME = example
NXDC_FILES ?=
INC_PATH ?=
IMG ?=
DIFF ?=
NEMU_HOME ?= $(abspath ../nemu)
CAPSTONE_HOME ?= $(NEMU_HOME)/tools/capstone/repo

VERILATOR = verilator
BUILD_DIR = $(abspath ./build)
OBJ_DIR = $(BUILD_DIR)/obj_dir
BIN = $(OBJ_DIR)/V$(TOPNAME)

VSRCS = $(shell find $(abspath ./vsrc) -name "*.v" -o -name "*.sv")
CSRCS = $(shell find $(abspath ./csrc) -name "*.c" -o -name "*.cc" -o -name "*.cpp")

CXXFLAGS += -std=c++17 -O2 -Wall -I$(OBJ_DIR) -I$(CAPSTONE_HOME)/include $(addprefix -I,$(INC_PATH))
CXXFLAGS += -DNPC_CAPSTONE_LIB=\\\"$(CAPSTONE_HOME)/libcapstone.so.5\\\"
LDFLAGS += -ldl

VERILATOR_FLAGS = --cc --exe --build --sv --trace --top-module $(TOPNAME) \
	-Mdir $(OBJ_DIR) $(VSRCS) $(CSRCS) \
	-CFLAGS "$(CXXFLAGS)" -LDFLAGS "$(LDFLAGS)"

all: sim

$(BIN): $(VSRCS) $(CSRCS)
	$(VERILATOR) $(VERILATOR_FLAGS)

sim: $(BIN)
	$(call git_commit, "sim RTL") # DO NOT REMOVE THIS LINE!!!
	@if [ -n "$(strip $(IMG))" ]; then \
		$(BIN) -b $(if $(DIFF),--diff=$(DIFF)) $(IMG); \
	else \
		echo "Built $(BIN). Set IMG=... to run a program."; \
	fi

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all sim clean

include ../Makefile
