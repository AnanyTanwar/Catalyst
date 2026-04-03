# Catalyst

> A UCI-compliant chess engine written in C++20, featuring NNUE evaluation and advanced search.

Catalyst uses a `(768 → 256)×2 → 8` neural network with incremental accumulator updates and SIMD-accelerated inference (SSE4.1 / AVX2 / AVX-512), combined with a robust search implementation including PVS, Lazy SMP, and an extensive suite of pruning, extension, and reduction techniques.

[![License][license-badge]][license-link]
[![GitHub release (latest by date)][release-badge]][release-link]
[![Commits since latest release][commits-badge]][commits-link]

---

## Strength

Catalyst has not yet been rated by CCRL. The following ratings are based on internal testing and should be considered approximate.

| Version | Estimated Elo | Notes |
|:--------|:-------------:|:------|
| v1.0.0  | ~2900         | Initial NNUE implementation |
| v2.0.0  | ~3058         | +158 Elo vs v1 — new NNUE architecture, major search improvements |
| v2.1.0  | ~3130         | +72 Elo vs v2.0 — TT rewrite: 16-byte entries, `uint128` index, huge page support |
| v2.2.0  | ~3170         | +40 Elo vs v2.1 — threat-based move scoring, dynamic SEE thresholds |

> Ratings are approximate and based on self-play at 10+0.1. They may vary depending on hardware, time control, and testing methodology.  
> Official CCRL ratings will be added once Catalyst is submitted.

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
  - Late move pruning (LMP)
  - SEE pruning (quiets and noisy moves)
  - History pruning
  - ProbCut
- **Extensions**
  - Singular extensions
  - Double and triple extensions
  - Negative extensions
  - Check extensions
- **Reductions**
  - Late move reductions (LMR) with history-based adjustments
  - Internal iterative reduction (IIR)
- **Move Ordering**
  - TT move
  - Staged move picker (good captures → killers → countermove → quiets → bad captures)
  - MVV-LVA for captures
  - Threat-based quiet move scoring (escape bonus / step-in penalty)
  - Dynamic SEE threshold for good captures
  - Bad captures ordered by SEE loss (least-losing first)
  - Killer move heuristic (2 per ply)
  - Countermove heuristic
  - Butterfly history
  - Capture history
  - Pawn history
  - 1-ply, 2-ply, and 4-ply continuation history
- **History**
  - Butterfly history
  - Capture history
  - Pawn history
  - Continuation history (1-ply, 2-ply, 4-ply)
  - Correction history (main, pawn, non-pawn white, non-pawn black, continuation)
- Mate distance pruning
- Fifty-move rule eval scaling
- Draw score randomization (anti-repetition)
- Hindsight depth adjustment
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
| `x86-64` | x86-64 + POPCNT | Widest compatibility, slowest |

> **AMD Zen 1 / Zen 2 users**: use the `avx2` build even if your CPU supports BMI2. These CPUs implement `pext`/`pdep` in microcode, making them very slow for Catalyst's purposes.

---

## Building from Source

Requires `make`, a C++20 compiler (GCC ≥ 13 or Clang ≥ 16), and `objcopy`. The Makefile will automatically download the NNUE file and embed it into the binary.

```bash
# Clone the repository
git clone https://github.com/AnanyTanwar/Catalyst
cd Catalyst

# Build for your native CPU (recommended for local use)
make ARCH=native

# Build a specific architecture
make linux-avx2
make linux-bmi2
make linux-avx512

# Build all Linux release binaries
make release-linux

# Build all Windows release binaries (requires MinGW cross-compiler)
make release-win

# Build with PGO (profile-guided optimisation)
make pgo

# Clean build artifacts
make clean
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
- [Integral](https://github.com/aronpetko/integral)
- [Obsidian](https://github.com/gab8192/Obsidian)
- [bullet](https://github.com/jw1912/bullet) — NNUE trainer

---

[license-badge]: https://img.shields.io/github/license/AnanyTanwar/Catalyst?style=for-the-badge
[release-badge]: https://img.shields.io/github/v/release/AnanyTanwar/Catalyst?style=for-the-badge
[commits-badge]: https://img.shields.io/github/commits-since/AnanyTanwar/Catalyst/latest?style=for-the-badge

[license-link]: https://github.com/AnanyTanwar/Catalyst/blob/main/LICENSE
[release-link]: https://github.com/AnanyTanwar/Catalyst/releases/latest
[commits-link]: https://github.com/AnanyTanwar/Catalyst/commits/main
