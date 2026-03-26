<div align="center">

<img src="https://i.imgur.com/R7wOg9n.png" alt="Catalyst Chess Engine" width="150" height="150"/>

# Catalyst

[![License][license-badge]][license-link]
[![GitHub release (latest by date)][release-badge]][release-link]
[![Commits since latest release][commits-badge]][commits-link]

A UCI-compliant chess engine written in C++20, featuring NNUE evaluation and a fine search.

</div>

---

## Features

### Search
- Principal Variation Search (PVS) with iterative deepening
- Quiescence search
- Aspiration windows
- Transposition table with aging
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
  - Killer move heuristic (2 per ply)
  - Countermove heuristic
  - Butterfly history
  - Capture history
  - Pawn history
  - 1-ply, 2-ply, and 4-ply continuation history
  - SEE move ordering
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
  - Architecture: `(768 → 64)×2 → 1`
  - Incremental accumulator updates
  - SIMD-accelerated inference (SSE4.1 / AVX2 / AVX-512)
  - Embedded network (`catalyst-v1.nnue`) — no external file needed
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

| Name            |  Type   | Default |     Valid values     | Description                                         |
|:----------------|:-------:|:-------:|:--------------------:|:----------------------------------------------------|
| `Hash`          | integer |   64    |     [1, 65536]       | Transposition table size in MiB.                    |
| `Clear Hash`    | button  |   N/A   |        N/A           | Clears the transposition table.                     |
| `Threads`       | integer |    1    |  [1, hardware max]   | Number of search threads (Lazy SMP).                |
| `Move Overhead` | integer |   50    |     [0, 5000]        | Time overhead per move in ms.                       |
| `Ponder`        |  check  |  false  |   `true`, `false`    | Enable pondering.                                   |
| `EvalFile`      | string  | `catalyst.nnue` | any path  | External NNUE file to load (overrides embedded).    |

---

## Non-standard Commands

| Command              | Description                                                    |
|:---------------------|:---------------------------------------------------------------|
| `d`                  | Display the current board position.                            |
| `eval`               | Print NNUE evaluation for the current position.                |
| `perft <depth>`      | Run a perft test from the current position.                    |
| `bench [depth <n>] [threads <n>]` | Run a benchmark. Default depth: 13.             |
| `datagen [output <file>] [threads <n>] [nodes <n>] [games <n>] [book <file>]` | Generate training data. |

---

## Builds

Choose the binary that matches your CPU's highest supported instruction set:

| Binary              | Requirements                                      | Notes                              |
|:--------------------|:--------------------------------------------------|:-----------------------------------|
| `avx512vnni`        | AVX-512 + VNNI (Cascade Lake, Zen 4+)             | Fastest — use if supported         |
| `avx512`            | AVX-512 + BMI2 (Ice Lake, Rocket Lake+)           |                                    |
| `bmi2`              | AVX2 + BMI2 (Intel Haswell+, AMD Zen 3+)          | Use BMI2 on Intel / Zen 3+         |
| `avx2`              | AVX2 (Broadwell+, AMD Excavator+)                 | For AMD Zen 1/2 or older Intel     |
| `x86-64`            | x86-64 + POPCNT                                   | Widest compatibility, slowest      |

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
make avx2
make bmi2
make avx512

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

Catalyst would not exist without the broader chess programming community. In no particular order, these engines and projects were notable sources of ideas and inspiration:

- [Stockfish](https://github.com/official-stockfish/Stockfish)
- [Stormphrax](https://github.com/Ciekce/Stormphrax)
- [Integral](https://github.com/aronpetko/integral)
- [bullet](https://github.com/jw1912/bullet) — NNUE trainer

---

[license-badge]: https://img.shields.io/github/license/AnanyTanwar/Catalyst?style=for-the-badge
[release-badge]: https://img.shields.io/github/v/release/AnanyTanwar/Catalyst?style=for-the-badge
[commits-badge]: https://img.shields.io/github/commits-since/AnanyTanwar/Catalyst/latest?style=for-the-badge

[license-link]: https://github.com/AnanyTanwar/Catalyst/blob/main/LICENSE
[release-link]: https://github.com/AnanyTanwar/Catalyst/releases/latest
[commits-link]: https://github.com/AnanyTanwar/Catalyst/commits/main
