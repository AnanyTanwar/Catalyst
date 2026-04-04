CXX     ?= g++
CXX_WIN ?= x86_64-w64-mingw32-g++-posix
STRIP   ?= strip
STRIP_WIN ?= x86_64-w64-mingw32-strip
EXE      = catalyst
VERSION  = 2.2.0

NNUE_FILE   = catalyst-v2.nnue
NNUE_OBJ    = $(BUILD_DIR)/nnue_embed.o
NNUE_DL_URL = https://github.com/AnanyTanwar/CatalystNet/releases/latest/download/$(NNUE_FILE)

ifeq ($(OS),Windows_NT)
	EXT      = .exe
	RM       = cmd /C del /Q /F 2>nul
	RMDIR    = cmd /C rmdir /S /Q 2>nul
	MKDIR    = cmd /C mkdir
	SRCS     = $(shell dir /S /B src\*.cpp 2>nul)
	OBJ_FMT  = pe-x86-64
else
	EXT      =
	RM       = rm -f
	RMDIR    = rm -rf
	MKDIR    = mkdir -p
	SRCS     = $(shell find src -name '*.cpp')
	OBJ_FMT  = elf64-x86-64
endif

BUILD_DIR = build
BIN_DIR   = bin

M64         = -m64 -mpopcnt
MSSE41      = $(M64) -msse -msse2 -mssse3 -msse4.1
MAVX2       = $(MSSE41) -mbmi -mfma -mavx2
MBMI2       = $(MAVX2) -mbmi2 -DUSE_PEXT
MAVX512     = $(MAVX2) -mavx512f -mavx512bw -mbmi2 -DUSE_PEXT
MAVX512VNNI = $(MAVX512) -mavx512vnni -mavx512dq -mavx512vl -DUSE_VNNI

BASE_FLAGS = \
	-std=c++20 -Wall -Wextra -Wshadow -Wcast-qual \
	-DNDEBUG -DNNUE_EMBEDDED -pthread \
	-fno-exceptions -fno-rtti \
	-fomit-frame-pointer -funroll-loops -falign-functions=32 \
	-ffunction-sections -fdata-sections \
	-O3 -flto=auto -Isrc

LDFLAGS_LINUX = -pthread -flto=auto \
                -static-libgcc -static-libstdc++ \
                -Wl,--gc-sections \
                -Wl,--no-as-needed

ifeq ($(OS),Windows_NT)
else
  LLD_VERSION_STR := $(shell ld.lld --version 2>/dev/null)
  ifneq ($(LLD_VERSION_STR),)
    CXX_MAJOR := $(shell $(CXX) -dumpversion 2>/dev/null | cut -d. -f1)
    LLD_MAJOR := $(shell ld.lld --version 2>/dev/null | grep -oE '[0-9]+' | head -n1)
    ifeq ($(CXX_MAJOR),$(LLD_MAJOR))
      LDFLAGS_LINUX += -fuse-ld=lld
    else
      LDFLAGS_LINUX += -fuse-ld=lld
    endif
  endif
endif

LDFLAGS_WIN = -pthread -flto=auto \
              -static -static-libgcc -static-libstdc++ \
              -Wl,--stack,8388608 \
              -Wl,--gc-sections

ifeq ($(ARCH),)
	ARCH = native
endif

ifeq ($(ARCH),native)
	ARCH_FLAGS = -march=native
	PROPS      = $(shell echo | $(CXX) -march=native -E -dM - 2>/dev/null)
	ifneq ($(findstring __BMI2__, $(PROPS)),)
		ifeq ($(findstring __znver1, $(PROPS)),)
			ifeq ($(findstring __znver2, $(PROPS)),)
				ARCH_FLAGS += -DUSE_PEXT
			endif
		endif
	endif
	ifneq ($(findstring __AVX512BW__, $(PROPS)),)
		ifneq ($(findstring __AVX512F__, $(PROPS)),)
			ARCH_FLAGS += -DUSE_AVX512
		endif
	endif
else ifeq ($(ARCH),x86-64)
	ARCH_FLAGS = $(M64)
else ifeq ($(ARCH),avx2)
	ARCH_FLAGS = $(MAVX2)
else ifeq ($(ARCH),bmi2)
	ARCH_FLAGS = $(MBMI2)
else ifeq ($(ARCH),avx512)
	ARCH_FLAGS = $(MAVX512)
else ifeq ($(ARCH),avx512vnni)
	ARCH_FLAGS = $(MAVX512VNNI)
else
	ARCH_FLAGS = -march=$(ARCH)
endif

CXXFLAGS = $(BASE_FLAGS) $(ARCH_FLAGS)

OBJS    = $(patsubst src/%.cpp,$(BUILD_DIR)/$(SUFFIX)/%.o,$(SRCS))
DEPENDS = $(OBJS:.o=.d)

PGO_DIR     = $(BUILD_DIR)/pgo
PGO_GEN_DIR = $(BUILD_DIR)/pgo-gen

.PHONY: all net clean distclean \
        native \
        linux-x86-64 linux-avx2 linux-bmi2 linux-avx512 linux-avx512vnni \
        win-x86-64 win-avx2 win-bmi2 win-avx512 win-avx512vnni \
        release release-linux release-win pgo debug help

all: net linux-x86-64

$(NNUE_FILE):
	@if command -v curl >/dev/null 2>&1; then \
		curl -fskL "$(NNUE_DL_URL)" -o "$(NNUE_FILE)"; \
	elif command -v wget >/dev/null 2>&1; then \
		wget -q "$(NNUE_DL_URL)" -O "$(NNUE_FILE)"; \
	else \
		echo "ERROR: install curl or wget"; exit 1; \
	fi

net: $(NNUE_FILE)

$(NNUE_OBJ): $(NNUE_FILE)
	@$(MKDIR) $(BUILD_DIR) 2>/dev/null || true
	cd $(dir $(NNUE_FILE)) && objcopy \
		--input-target=binary \
		--output-target=$(OBJ_FMT) \
		--binary-architecture=i386:x86-64 \
		$(notdir $(NNUE_FILE)) $(abspath $(NNUE_OBJ))

$(BUILD_DIR)/$(SUFFIX)/%.o: src/%.cpp | $(NNUE_OBJ)
	@$(MKDIR) $(dir $@) 2>/dev/null || true
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

_build: $(OBJS)
	@$(MKDIR) $(BIN_DIR) 2>/dev/null || true
	$(CXX) $(CXXFLAGS) $(OBJS) $(NNUE_OBJ) $(LDFLAGS) -o $(BIN_DIR)/$(EXE)-$(SUFFIX)$(EXT)
	@$(STRIP_BIN) $(BIN_DIR)/$(EXE)-$(SUFFIX)$(EXT) 2>/dev/null || true

native: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=native CXX=$(CXX) LDFLAGS="$(LDFLAGS_LINUX)" SUFFIX=native EXT= STRIP_BIN=$(STRIP)

linux-x86-64: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=x86-64 CXX=$(CXX) LDFLAGS="$(LDFLAGS_LINUX)" SUFFIX=linux-x86-64 EXT= STRIP_BIN=$(STRIP)

linux-avx2: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=avx2 CXX=$(CXX) LDFLAGS="$(LDFLAGS_LINUX)" SUFFIX=linux-avx2 EXT= STRIP_BIN=$(STRIP)

linux-bmi2: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=bmi2 CXX=$(CXX) LDFLAGS="$(LDFLAGS_LINUX)" SUFFIX=linux-bmi2 EXT= STRIP_BIN=$(STRIP)

linux-avx512: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=avx512 CXX=$(CXX) LDFLAGS="$(LDFLAGS_LINUX)" SUFFIX=linux-avx512 EXT= STRIP_BIN=$(STRIP)

linux-avx512vnni: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=avx512vnni CXX=$(CXX) LDFLAGS="$(LDFLAGS_LINUX)" SUFFIX=linux-avx512vnni EXT= STRIP_BIN=$(STRIP)

win-x86-64: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=x86-64 CXX=$(CXX_WIN) LDFLAGS="$(LDFLAGS_WIN)" SUFFIX=win-x86-64 EXT=.exe OBJ_FMT=pe-x86-64 STRIP_BIN=$(STRIP_WIN)

win-avx2: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=avx2 CXX=$(CXX_WIN) LDFLAGS="$(LDFLAGS_WIN)" SUFFIX=win-avx2 EXT=.exe OBJ_FMT=pe-x86-64 STRIP_BIN=$(STRIP_WIN)

win-bmi2: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=bmi2 CXX=$(CXX_WIN) LDFLAGS="$(LDFLAGS_WIN)" SUFFIX=win-bmi2 EXT=.exe OBJ_FMT=pe-x86-64 STRIP_BIN=$(STRIP_WIN)

win-avx512: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=avx512 CXX=$(CXX_WIN) LDFLAGS="$(LDFLAGS_WIN)" SUFFIX=win-avx512 EXT=.exe OBJ_FMT=pe-x86-64 STRIP_BIN=$(STRIP_WIN)

win-avx512vnni: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=avx512vnni CXX=$(CXX_WIN) LDFLAGS="$(LDFLAGS_WIN)" SUFFIX=win-avx512vnni EXT=.exe OBJ_FMT=pe-x86-64 STRIP_BIN=$(STRIP_WIN)

release-linux: net $(NNUE_OBJ) linux-x86-64 linux-avx2 linux-bmi2 linux-avx512 linux-avx512vnni

release-win: net $(NNUE_OBJ) win-x86-64 win-avx2 win-bmi2 win-avx512 win-avx512vnni

release: release-linux release-win

debug: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=native CXX=$(CXX) \
		CXXFLAGS="-std=c++20 -O0 -g3 -Wall -Wextra -Wshadow -Wcast-qual -pthread -DDEBUG -DNNUE_EMBEDDED -Isrc $(SANITIZE)" \
		LDFLAGS="-pthread $(SANITIZE)" SUFFIX=debug EXT= STRIP_BIN=true

pgo: net $(NNUE_OBJ)
	$(MAKE) _build ARCH=$(or $(ARCH),native) CXX=$(CXX) \
		CXXFLAGS="$(CXXFLAGS) -fprofile-generate=$(PGO_GEN_DIR)" \
		LDFLAGS="$(LDFLAGS_LINUX) -fprofile-generate=$(PGO_GEN_DIR)" SUFFIX=pgo-gen EXT= STRIP_BIN=true
	./$(BIN_DIR)/$(EXE)-pgo-gen bench
	./$(BIN_DIR)/$(EXE)-pgo-gen perft 6
	./$(BIN_DIR)/$(EXE)-pgo-gen go movetime 5000
	$(MAKE) _build ARCH=$(or $(ARCH),native) CXX=$(CXX) \
		CXXFLAGS="$(CXXFLAGS) -fprofile-use=$(PGO_GEN_DIR) -Wno-missing-profile" \
		LDFLAGS="$(LDFLAGS_LINUX) -fprofile-use=$(PGO_GEN_DIR)" SUFFIX=pgo EXT= STRIP_BIN=$(STRIP)
	$(RMDIR) $(PGO_GEN_DIR)
	$(RM) $(BIN_DIR)/$(EXE)-pgo-gen 2>/dev/null || true

clean:
	$(RMDIR) $(BUILD_DIR)
	$(RMDIR) $(BIN_DIR)

distclean: clean
	$(RM) $(NNUE_FILE)

help:
	@echo "Targets:  all net native release release-linux release-win pgo debug clean distclean"
	@echo "Linux:    linux-x86-64 linux-avx2 linux-bmi2 linux-avx512 linux-avx512vnni"
	@echo "Windows:  win-x86-64 win-avx2 win-bmi2 win-avx512 win-avx512vnni"
	@echo "ARCH:     native x86-64 avx2 bmi2 avx512 avx512vnni"
	@echo "PGO:      make pgo [ARCH=bmi2]"
	@echo "Debug:    make debug [SANITIZE=-fsanitize=address,undefined]"

-include $(DEPENDS)
