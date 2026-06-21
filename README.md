# Catalyst

Catalyst is a strong UCI chess engine written in C++20. It combines a neural network trained entirely on self-generated games with a deeply optimized search, featuring PVS with iterative deepening, Lazy SMP multi-threaded search, and a wide range of pruning, reduction, and extension techniques. The network is embedded directly into the binary, updated incrementally during search, and accelerated with SIMD instructions for fast inference.

## Strength

| Version | CCRL 40/15 | CCRL 2+1 |
| :------ | :--------: | :------: |
| v1.0.0  |     —      |   3080   |
| v2.0.0  |     —      |    —     |
| v2.1.0  |    3161    |    —     |
| v2.2.0  |    3222    |    —     |
| v3.0.0  |    3275    |   3324   |

---

# How to use

Download the latest release from [the releases page](https://github.com/AnanyTanwar/Catalyst/releases). Catalyst implements the UCI protocol and works with any UCI-compatible GUI such as [CuteChess](https://cutechess.com/), [En Croissant](https://encroissant.org/), or [Banksia](https://banksiagui.com/).

Choose the binary that matches your CPU:

| Binary       | Requirements                            |
| :----------- | :-------------------------------------- |
| `avx512vnni` | AVX-512 + VNNI (Cascade Lake, Zen 4+)   |
| `avx512`     | AVX-512 + BMI2 (Ice Lake, Rocket Lake+) |
| `bmi2`       | AVX2 + BMI2 (Haswell+, Zen 3+)          |
| `avx2`       | AVX2 (Broadwell+, Excavator+)           |
| `sse41`      | SSE4.1 (Core 2 Penryn+, Phenom II+)     |
| `x86-64`     | x86-64 + POPCNT                         |

> **AMD Zen 1 / Zen 2 users:** use `avx2` even if your CPU supports BMI2 — `pext`/`pdep` are microcoded and very slow on these chips.

---

# Building from source

Requires GCC ≥ 13 or Clang ≥ 16, `objcopy`, and CMake ≥ 3.16 (for CMake builds). The NNUE weights are downloaded and embedded automatically at build time.

```bash
git clone https://github.com/AnanyTanwar/Catalyst
cd Catalyst
```

## Make

```bash
make -j native              # recommended — optimised for your CPU

# specific Linux architectures
make -j linux-avx512vnni
make -j linux-avx512
make -j linux-bmi2
make -j linux-avx2
make -j linux-sse41
make -j linux-x86-64

# Windows (requires MinGW cross-compiler)
make -j win-avx512vnni
make -j win-avx512
make -j win-bmi2
make -j win-avx2
make -j win-sse41
make -j win-x86-64

# release bundles
make -j release-linux       # all Linux binaries
make -j release-win         # all Windows binaries
make -j release             # both

# PGO (profile-guided optimisation)
make pgo                    # native CPU
make pgo ARCH=avx2          # specific arch

make -j datagen

# misc
make debug                  # O0 + debug symbols
make sanitize               # ASan + UBSan
make install                # installs to /usr/local/bin
make install PREFIX=/usr
make clean
make distclean               # also removes the NNUE file
```

## CMake

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build
```

To build specific architectures, pass the relevant flags:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_NATIVE=OFF -DBUILD_AVX2=ON -DBUILD_BMI2=ON
cmake --build build -j
```

```bash
cmake -B build-datagen -DCMAKE_BUILD_TYPE=Release -DCATALYST_DATAGEN=ON
cmake --build build-datagen -j
```

For PGO, run two passes:

```bash
# pass 1 — instrument
cmake -B build-pgo-gen -DCMAKE_BUILD_TYPE=Release -DCATALYST_PGO_GEN=ON
cmake --build build-pgo-gen -j
./build-pgo-gen/catalyst-native bench

# pass 2 — optimise
cmake -B build-pgo -DCMAKE_BUILD_TYPE=Release -DCATALYST_PGO_USE=ON
cmake --build build-pgo -j
```

---

# Credits

Special thanks to the Stockfish Discord community for their invaluable help with debugging, NNUE guidance, and overall support during Catalyst's development.

Catalyst would not exist without the broader chess programming community. In no particular order, these engines and projects were notable sources of ideas and inspiration:

- [Stockfish](https://github.com/official-stockfish/Stockfish)
- [Stormphrax](https://github.com/Ciekce/Stormphrax)
- [Alexandria](https://github.com/PGG106/Alexandria)
- [Obsidian](https://github.com/gab8192/Obsidian)
- [bullet](https://github.com/jw1912/bullet) — NNUE trainer
