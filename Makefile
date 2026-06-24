CXX       ?= g++
CXX_WIN   ?= x86_64-w64-mingw32-g++-posix
STRIP     ?= strip
STRIP_WIN ?= x86_64-w64-mingw32-strip
EXE        = catalyst
VERSION    = 3.0.0

NNUE_FILE   = catalyst-v2.nnue
NNUE_DL_URL = https://github.com/AnanyTanwar/CatalystNet/releases/latest/download/$(NNUE_FILE)

ifeq ($(OS),Windows_NT)
	EXT     = .exe
	RM      = cmd /C del /Q /F 2>nul
	RMDIR   = cmd /C rmdir /S /Q 2>nul
	MKDIR   = cmd /C mkdir
	SRCS    = $(shell dir /S /B src\*.cpp 2>nul)
	OBJ_FMT = pe-x86-64
else
	EXT     =
	RM      = rm -f
	RMDIR   = rm -rf
	MKDIR   = mkdir -p
	SRCS    = $(shell find src -name '*.cpp')
	OBJ_FMT = elf64-x86-64
endif

BUILD_DIR = build
BIN_DIR   = bin
PGO_DIR   = $(BUILD_DIR)/pgo-gen

NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

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

# DATAGEN=1 appends -DDATAGEN on top of whatever target is being built
ifeq ($(DATAGEN),1)
	BASE_FLAGS += -DDATAGEN
endif

LDFLAGS_LINUX = -pthread -flto=auto \
                -static-libgcc -static-libstdc++ \
                -Wl,--gc-sections \
                -Wl,--no-as-needed

ifeq ($(OS),Windows_NT)
else
	ifneq ($(shell ld.lld --version 2>/dev/null),)
		LDFLAGS_LINUX += -fuse-ld=lld
	endif
endif

LDFLAGS_WIN = -pthread -flto=auto \
              -static -static-libgcc -static-libstdc++ \
              -Wl,--stack,8388608 \
              -Wl,--gc-sections

ARCH ?= native

ifeq ($(ARCH),native)
	ARCH_FLAGS = -march=native
	PROPS      = $(shell echo | $(CXX) -march=native -E -dM - 2>/dev/null)
	ifneq ($(findstring __BMI2__, $(PROPS)),)
		ifeq ($(findstring __znver1__, $(PROPS)),)
			ifeq ($(findstring __znver2__, $(PROPS)),)
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
else ifeq ($(ARCH),sse41)
	ARCH_FLAGS = $(MSSE41)
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

NNUE_OBJ = $(BUILD_DIR)/nnue_embed_$(OBJ_FMT).o

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
	objcopy \
		--input-target=binary \
		--output-target=$(OBJ_FMT) \
		--binary-architecture=i386:x86-64 \
		$(NNUE_FILE) $(NNUE_OBJ)

OBJS    = $(patsubst src/%.cpp,$(BUILD_DIR)/$(SUFFIX)/%.o,$(SRCS))
DEPENDS = $(OBJS:.o=.d)

.DELETE_ON_ERROR:

$(BUILD_DIR)/$(SUFFIX)/%.o: src/%.cpp | $(NNUE_OBJ)
	@$(MKDIR) $(dir $@) 2>/dev/null || true
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

_build: $(OBJS)
	@$(MKDIR) $(BIN_DIR) 2>/dev/null || true
	$(CXX) $(CXXFLAGS) $(OBJS) $(NNUE_OBJ) $(LDFLAGS) -o $(BIN_DIR)/$(EXE)-$(SUFFIX)$(EXT)
	@$(STRIP_BIN) $(BIN_DIR)/$(EXE)-$(SUFFIX)$(EXT) 2>/dev/null || true

-include $(DEPENDS)

# macro: $(1)=ARCH $(2)=SUFFIX $(3)=CXX $(4)=LDFLAGS $(5)=EXT $(6)=OBJ_FMT_OVERRIDE $(7)=STRIP_BIN
define BUILD_TARGET
$(2): net
	@$(MAKE) -j$(NPROC) $(NNUE_OBJ) OBJ_FMT=$(if $(6),$(6),$(OBJ_FMT))
	@$(MAKE) -j$(NPROC) _build ARCH=$(1) CXX=$(3) LDFLAGS="$(4)" SUFFIX=$(2) EXT=$(5) OBJ_FMT=$(if $(6),$(6),$(OBJ_FMT)) STRIP_BIN=$(7)
endef

.PHONY: all net native \
        linux-x86-64 linux-sse41 linux-avx2 linux-bmi2 linux-avx512 linux-avx512vnni \
        win-x86-64 win-sse41 win-avx2 win-bmi2 win-avx512 win-avx512vnni \
        release release-linux release-win \
        pgo debug sanitize datagen install clean distclean format help

all: net linux-x86-64

native: net
	@$(MAKE) -j$(NPROC) $(NNUE_OBJ)
	@$(MAKE) -j$(NPROC) _build ARCH=native CXX=$(CXX) LDFLAGS="$(LDFLAGS_LINUX)" SUFFIX=native EXT= STRIP_BIN=$(STRIP)

$(eval $(call BUILD_TARGET,x86-64,linux-x86-64,$(CXX),$(LDFLAGS_LINUX),,,$(STRIP)))
$(eval $(call BUILD_TARGET,sse41,linux-sse41,$(CXX),$(LDFLAGS_LINUX),,,$(STRIP)))
$(eval $(call BUILD_TARGET,avx2,linux-avx2,$(CXX),$(LDFLAGS_LINUX),,,$(STRIP)))
$(eval $(call BUILD_TARGET,bmi2,linux-bmi2,$(CXX),$(LDFLAGS_LINUX),,,$(STRIP)))
$(eval $(call BUILD_TARGET,avx512,linux-avx512,$(CXX),$(LDFLAGS_LINUX),,,$(STRIP)))
$(eval $(call BUILD_TARGET,avx512vnni,linux-avx512vnni,$(CXX),$(LDFLAGS_LINUX),,,$(STRIP)))
$(eval $(call BUILD_TARGET,x86-64,win-x86-64,$(CXX_WIN),$(LDFLAGS_WIN),.exe,pe-x86-64,$(STRIP_WIN)))
$(eval $(call BUILD_TARGET,sse41,win-sse41,$(CXX_WIN),$(LDFLAGS_WIN),.exe,pe-x86-64,$(STRIP_WIN)))
$(eval $(call BUILD_TARGET,avx2,win-avx2,$(CXX_WIN),$(LDFLAGS_WIN),.exe,pe-x86-64,$(STRIP_WIN)))
$(eval $(call BUILD_TARGET,bmi2,win-bmi2,$(CXX_WIN),$(LDFLAGS_WIN),.exe,pe-x86-64,$(STRIP_WIN)))
$(eval $(call BUILD_TARGET,avx512,win-avx512,$(CXX_WIN),$(LDFLAGS_WIN),.exe,pe-x86-64,$(STRIP_WIN)))
$(eval $(call BUILD_TARGET,avx512vnni,win-avx512vnni,$(CXX_WIN),$(LDFLAGS_WIN),.exe,pe-x86-64,$(STRIP_WIN)))

release-linux: linux-x86-64 linux-sse41 linux-avx2 linux-bmi2 linux-avx512 linux-avx512vnni

release-win: win-x86-64 win-sse41 win-avx2 win-bmi2 win-avx512 win-avx512vnni

release: release-linux release-win

DEBUG_FLAGS = -std=c++20 -O0 -g3 -Wall -Wextra -Wshadow -Wcast-qual \
              -pthread -DDEBUG -DNNUE_EMBEDDED -Isrc $(SANITIZE)

debug: net
	@$(MAKE) -j$(NPROC) $(NNUE_OBJ)
	@$(MAKE) -j$(NPROC) _build ARCH=x86-64 CXX=$(CXX) \
		CXXFLAGS="$(DEBUG_FLAGS)" \
		LDFLAGS="-pthread $(SANITIZE)" \
		SUFFIX=debug EXT= STRIP_BIN=true

sanitize:
	$(MAKE) debug SANITIZE="-fsanitize=address,undefined"

# Datagen build: native-optimised binary compiled with -DDATAGEN, which
# datagen.cpp/.h gate self-play data generation behind. Separate SUFFIX
# keeps it from clobbering normal native builds in build/bin.
datagen: net
	@$(MAKE) -j$(NPROC) $(NNUE_OBJ)
	@$(MAKE) -j$(NPROC) _build ARCH=native CXX=$(CXX) \
		CXXFLAGS="$(BASE_FLAGS) $(ARCH_FLAGS) -DDATAGEN" \
		LDFLAGS="$(LDFLAGS_LINUX)" \
		SUFFIX=datagen EXT= STRIP_BIN=$(STRIP)

pgo: net
	@$(MAKE) -j$(NPROC) $(NNUE_OBJ)
	@$(MAKE) -j$(NPROC) _build ARCH=$(or $(ARCH),native) CXX=$(CXX) \
		CXXFLAGS="$(CXXFLAGS) -fprofile-generate=$(PGO_DIR)" \
		LDFLAGS="$(LDFLAGS_LINUX) -fprofile-generate=$(PGO_DIR)" \
		SUFFIX=pgo-gen EXT= STRIP_BIN=true
	printf "bench\nquit\n"             | ./$(BIN_DIR)/$(EXE)-pgo-gen
	printf "perft 6\nquit\n"           | ./$(BIN_DIR)/$(EXE)-pgo-gen
	printf "go movetime 1000\nquit\n"  | ./$(BIN_DIR)/$(EXE)-pgo-gen
	printf "go movetime 5000\nquit\n"  | ./$(BIN_DIR)/$(EXE)-pgo-gen
	printf "go movetime 10000\nquit\n" | ./$(BIN_DIR)/$(EXE)-pgo-gen
	printf "go depth 12\nquit\n"       | ./$(BIN_DIR)/$(EXE)-pgo-gen
	printf "go depth 16\nquit\n"       | ./$(BIN_DIR)/$(EXE)-pgo-gen
	@$(MAKE) -j$(NPROC) _build ARCH=$(or $(ARCH),native) CXX=$(CXX) \
		CXXFLAGS="$(CXXFLAGS) -fprofile-use=$(PGO_DIR) -Wno-missing-profile" \
		LDFLAGS="$(LDFLAGS_LINUX) -fprofile-use=$(PGO_DIR)" \
		SUFFIX=pgo EXT= STRIP_BIN=$(STRIP)
	@$(RMDIR) $(PGO_DIR)
	@$(RM) $(BIN_DIR)/$(EXE)-pgo-gen

PREFIX ?= /usr/local

install: native
	install -Dm755 $(BIN_DIR)/$(EXE)-native $(DESTDIR)$(PREFIX)/bin/$(EXE)

clean:
	$(RMDIR) $(BUILD_DIR)
	$(RMDIR) $(BIN_DIR)

distclean: clean
	$(RM) $(NNUE_FILE)

format:
	find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i

help:
	@echo ""
	@echo "Catalyst v$(VERSION)"
	@echo ""
	@echo "Usage:  make [target] [ARCH=arch] [CXX=compiler]"
	@echo ""
	@echo "Targets:"
	@echo "  all              net + linux-x86-64 (default)"
	@echo "  net              download NNUE weights"
	@echo "  native           build optimised for the host CPU"
	@echo "  release          all Linux + Windows release binaries"
	@echo "  release-linux    linux-x86-64 sse41 avx2 bmi2 avx512 avx512vnni"
	@echo "  release-win      win-x86-64 sse41 avx2 bmi2 avx512 avx512vnni"
	@echo "  pgo [ARCH=...]   two-pass PGO build (default: native)"
	@echo "  debug            O0 + g3, no release flags"
	@echo "  sanitize         debug + ASan + UBSan"
	@echo "  datagen          native build with -DDATAGEN (self-play data generation)"
	@echo "  install          install native binary to PREFIX"
	@echo "  clean            remove build/ and bin/"
	@echo "  distclean        clean + remove NNUE file"
	@echo "  format           run clang-format on all source files"
	@echo ""
	@echo "Architectures:  native x86-64 sse41 avx2 bmi2 avx512 avx512vnni"
	@echo ""
	@echo "Examples:"
	@echo "  make -j native"
	@echo "  make -j pgo ARCH=bmi2"
	@echo "  make -j release-linux"
	@echo "  make -j datagen"
	@echo "  make debug SANITIZE=-fsanitize=address,undefined"
	@echo "  make install PREFIX=/usr"
	@echo ""
