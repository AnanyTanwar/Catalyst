<p align="center">
  <img src="assets/logo.png" width="160">
</p>

<h1 align="center">Catalyst</h1>

<p align="center">
  Strong UCI chess engine written in C++20<br>
  with NNUE evaluation and advanced search
</p>

<p align="center">
  <a href="https://github.com/AnanyTanwar/Catalyst/actions/workflows/catalyst.yml">
    <img src="https://img.shields.io/github/actions/workflow/status/AnanyTanwar/Catalyst/catalyst.yml?style=for-the-badge&logo=github&label=Build">
  </a>

  <a href="https://github.com/AnanyTanwar/Catalyst/releases/latest">
    <img src="https://img.shields.io/github/v/release/AnanyTanwar/Catalyst?style=for-the-badge&logo=github&label=Release&color=3b82f6">
  </a>

  <a href="https://github.com/AnanyTanwar/Catalyst/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/License-GPL--3.0-2563eb?style=for-the-badge">
  </a>
</p>

---

Catalyst uses a `(768 → 256)×2 → 8` neural network with incremental accumulator updates and SIMD-accelerated inference (SSE4.1 / AVX2 / AVX-512), combined with a robust search implementation including PVS, Lazy SMP, and an extensive suite of pruning, extension, and reduction techniques.

## Strength

Official CCRL ratings for each release are listed below.

| Version | CCRL 40/15 | CCRL 2+1 |
|:--------|:----------:|:--------:|
| v1.0.0  | -          | 3080     |
| v2.0.0  | -          | -        |
| v2.1.0  | 3161       | -        |
| v2.2.0  | 3222       | -        |
| v3.0.0  | -          | 3332     |

---

## Features

### Search
- Principal Variation Search (PVS) with iterative deepening
- Quiescence search
- Aspiration windows
- Transposition table with aging, rule50 counter, and huge page support
- **Pruning**
  - Reverse futility pruning
  - Null move pruning with verification
  - Razoring
  - Futility pruning
  - Capture futility pruning
  - Late move pruning (LMP)
  - SEE pruning (quiets and noisy moves)
  - History pruning
  - ProbCut
  - Small ProbCut (early TT-based return when fail-high margin is large)
- **Extensions**
  - Singular extensions
  - Double and triple extensions
  - Negative extensions
  - Check extensions
- **Reductions**
  - Late move reductions (LMR) with history-based adjustments
  - Internal iterative reduction (IIR)
  - Alpha-raise depth reduction
- **Move Ordering**
  - TT move
  - Staged move picker (good captures → killers → countermove → quiets → bad captures)
  - MVV-LVA for captures
  - Threat-based quiet move scoring with precomputed opponent attack maps
  - Threat escape bonus/malus for quiet moves (queen, rook, minor pieces)
  - Continuation history–weighted quiet move ordering (1-ply × 2 > 2-ply > 3-ply > 4-ply)
  - Dynamic SEE thresholds for capture classification based on move score
  - Bad captures ordered by SEE loss (least-losing first)
  - Killer move heuristic (2 per ply)
  - Countermove heuristic
  - Butterfly history
  - Capture history
  - Pawn history
  - 1-ply, 2-ply, 3-ply, and 4-ply continuation history
- **History**
  - Butterfly history (threat-indexed)
  - Capture history (threat-indexed)
  - Pawn history
  - Continuation history (1-ply, 2-ply, 3-ply, 4-ply)
  - Correction history (main, pawn, non-pawn white, non-pawn black, continuation)
  - Eval history (applies malus to opponent's quiet move when our eval improves)
- **Miscellaneous**
  - Mate distance pruning
  - Hindsight depth adjustment
  - Shuffling detection in singular extension
  - ttPv propagation on fail-low
  - TT move bonus/malus on cutoff and fail-low
  - Fifty-move rule eval scaling
  - Draw score randomization (anti-repetition)
  - Stalemate avoidance at root
  - Lazy SMP (multi-threaded search)

### Evaluation
- **NNUE**
  - Architecture: `(768 → 256)×2 → 8` (8 material-count output buckets)
  - Incremental accumulator updates
  - SIMD-accelerated inference (SSE4.1 / AVX2 / AVX-512)
  - Embedded network (`catalyst-v2.nnue`)
  - SCReLU activation
  - Correction history applied on top of raw NNUE score

### Time Management
- Soft and hard time limits
- Best-move stability scaling (less time when best move is stable)
- Score instability scaling (more time when eval is volatile)
- Node fraction scaling (more time when best-move node fraction is low)
- Complexity estimate scaling
- Pondering support (`go ponder` / `ponderhit`)

---

## UCI Options

| Name | Type | Default | Valid values | Description |
|:-----|:----:|:-------:|:------------:|:------------|
| `Hash` | integer | 64 | [1, 65536] | Transposition table size in MiB. |
| `Clear Hash` | button | — | — | Clears the transposition table. |
| `Threads` | integer | 1 | [1, hardware max] | Number of search threads (Lazy SMP). |
| `Move Overhead` | integer | 50 | [0, 5000] | Time overhead per move in ms. |
| `Ponder` | check | false | `true`, `false` | Enable pondering. |
| `EvalFile` | string | `catalyst.nnue` | any path | External NNUE file to load (overrides embedded). |

---

## Non-standard Commands

| Command | Description |
|:--------|:------------|
| `d` | Display the current board position. |
| `eval` | Print NNUE evaluation for the current position. |
| `perft <depth>` | Run a perft test from the current position. |
| `bench [depth <n>] [threads <n>]` | Run a benchmark. Default depth: 13. |
| `datagen [output <file>] [threads <n>] [nodes <n>] [games <n>] [book <file>]` | Generate training data. |

---

## Builds

Choose the binary that matches your CPU's highest supported instruction set:

| Binary | Requirements | Notes |
|:-------|:-------------|:------|
| `avx512vnni` | AVX-512 + VNNI (Cascade Lake, Zen 4+) | Fastest — use if supported |
| `avx512` | AVX-512 + BMI2 (Ice Lake, Rocket Lake+) | |
| `bmi2` | AVX2 + BMI2 (Intel Haswell+, AMD Zen 3+) | Recommended for Intel Haswell+ and AMD Zen 3+ |
| `avx2` | AVX2 (Broadwell+, AMD Excavator+) | Use for AMD Zen 1/2 or older Intel |
| `sse41` | SSE4.1 (Core 2 Penryn+, Phenom II+) | Fallback for pre-AVX2 CPUs |
| `x86-64` | x86-64 + POPCNT | Widest compatibility, slowest |

> **AMD Zen 1 / Zen 2 users**: use the `avx2` build even if your CPU supports BMI2. These CPUs implement `pext`/`pdep` in microcode, making them very slow for Catalyst's purposes.

---

## Building from Source

Requires `make`, a C++20 compiler (GCC ≥ 13 or Clang ≥ 16), and `objcopy`. The Makefile will automatically download the NNUE file and embed it into the binary.

```bash
# Clone the repository
git clone https://github.com/AnanyTanwar/Catalyst
cd Catalyst

# Build optimised for the host CPU (recommended for local use)
make native

# Build a specific architecture
make linux-x86-64
make linux-sse41
make linux-avx2
make linux-bmi2
make linux-avx512
make linux-avx512vnni

# Build all Linux release binaries
make release-linux

# Build all Windows release binaries (requires MinGW cross-compiler)
make release-win

# Build with PGO (profile-guided optimisation)
make pgo                  # native CPU
make pgo ARCH=avx2
make pgo ARCH=bmi2
make pgo ARCH=sse41
make pgo ARCH=avx512

# Install to system
make install              # installs to /usr/local/bin
make install PREFIX=/usr  # custom prefix

# Debug build
make debug
make sanitize             # debug + ASan + UBSan
make debug SANITIZE=-fsanitize=address,undefined

# Clean build artifacts
make clean

# Clean build artifacts and downloaded NNUE file
make distclean
```

All binaries are placed in `bin/`.

---

## NNUE

Catalyst's network is stored at [CatalystNet](https://github.com/AnanyTanwar/CatalystNet). The Makefile fetches it automatically at build time and embeds it into the binary via `objcopy`, so no external `.nnue` file is needed at runtime.

You can override the embedded network at runtime using the `EvalFile` UCI option.

---

## License

Catalyst is free software distributed under the [GNU General Public License v3.0](LICENSE).

---

## Credits

**Special thanks to the Stockfish Discord community for their invaluable help with debugging, NNUE guidance, and overall support during Catalyst's development.**

Catalyst would not exist without the broader chess programming community. In no particular order, these engines and projects were notable sources of ideas and inspiration:

- [Stockfish](https://github.com/official-stockfish/Stockfish)
- [Stormphrax](https://github.com/Ciekce/Stormphrax)
- [Alexandria](https://github.com/PGG106/Alexandria)
- [Tarnished](https://github.com/Bobingstern/Tarnished/)
- [Integral](https://github.com/aronpetko/integral)
- [Obsidian](https://github.com/gab8192/Obsidian)
- [bullet](https://github.com/jw1912/bullet) — NNUE trainer
