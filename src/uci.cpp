// Catalyst is a UCI compliant chess engine
// Copyright (C) 2026 Anany Tanwar

// Catalyst is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Catalyst is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "uci.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "benchmark.h"
#include "board.h"
#include "datagen.h"
#include "movegen.h"
#include "nnue.h"
#include "thread.h"
#include "timeman.h"
#include "tt.h"
#include "types.h"

#ifdef TUNING
#include "tuning.h"
#endif

namespace Catalyst {

[[nodiscard]] static uint64_t perft(Board &board, int depth) {
  if (depth == 0)
    return 1ULL;
  MoveList moves = generate_legal(board);
  if (depth == 1)
    return uint64_t(moves.size());
  uint64_t nodes = 0;
  for (Move m : moves) {
    StateInfo si;
    board.make_move(m, si);
    nodes += perft(board, depth - 1);
    board.unmake_move(m);
  }
  return nodes;
}

UCI::UCI()
    : pool_(std::make_unique<ThreadPool>(1)),
      moveHistory(std::make_unique<StateInfo[]>(1024)) {
  board.set_startpos();
}

UCI::~UCI() { join_search(); }

void UCI::join_search() {
  if (searchThread_.joinable())
    searchThread_.join();
}

void UCI::loop() {
  std::string line, token;
  std::cout.setf(std::ios::unitbuf);

  while (std::getline(std::cin, line)) {
    std::istringstream iss(line);
    if (!(iss >> token))
      continue;

    if (token == "uci")
      cmd_uci();
    else if (token == "isready") {
      join_search();
      cmd_isready();
    } else if (token == "ucinewgame") {
      join_search();
      cmd_ucinewgame();
    } else if (token == "position") {
      join_search();
      cmd_position(iss);
    } else if (token == "go")
      cmd_go(iss);
    else if (token == "stop")
      cmd_stop();
    else if (token == "ponderhit")
      cmd_ponderhit();
    else if (token == "setoption")
      cmd_setoption(iss);
    else if (token == "bench") {
      join_search();
      cmd_bench(iss);
    } else if (token == "d") {
      join_search();
      cmd_display();
    } else if (token == "perft") {
      join_search();
      cmd_perft(iss);
    } else if (token == "eval") {
      join_search();
      cmd_eval();
    }
#ifdef TUNING
    else if (token == "tune") {
      std::string path;
      iss >> path;
      run_texel_tuner(path);
    }
#endif
    else if (token == "datagen") {
      join_search();
      cmd_datagen(iss);
    } else if (token == "quit") {
      cmd_stop();
      join_search();
      break;
    }
  }
}

void UCI::cmd_uci() {
  // Hardware concurrency for max Threads cap
  int hwThreads = std::max(1, int(std::thread::hardware_concurrency()));

  std::cout << "id name " << ENGINE_NAME << " " << ENGINE_VERSION << "\n"
            << "id author " << ENGINE_AUTHOR << "\n"
            << "\n"
            << "option name Hash type spin default 64 min 1 max 65536\n"
            << "option name Clear Hash type button\n"
            << "option name Threads type spin default 1 min 1 max " << hwThreads
            << "\n"
            << "option name Move Overhead type spin default 50 min 0 max 5000\n"
            << "option name Ponder type check default false\n"
            << "option name EvalFile type string default catalyst.nnue\n"
            << "\n"
            << "uciok\n";
  std::cout.flush();
}

void UCI::cmd_isready() {
  std::cout << "readyok\n";
  std::cout.flush();
}

void UCI::cmd_ucinewgame() {
  board.set_startpos();
  tt.clear();
  pool_->clear_all();
  moveHistoryCount = 0;
  ponderMove_ = MOVE_NONE;
  isPondering_ = false;
}

void UCI::cmd_position(std::istringstream &iss) {
  pool_->stop();
  join_search();
  moveHistoryCount = 0;
  std::string token;
  iss >> token;

  if (token == "startpos") {
    board.set_startpos();
    iss >> token;
  } else if (token == "fen") {
    std::string fen, part;
    for (int i = 0; i < 6 && iss >> part; ++i)
      fen += (i ? " " : "") + part;
    board.set_fen(fen);
    iss >> token;
  } else {
    board.set_startpos();
  }

  if (token == "moves")
    apply_moves(iss);
}

void UCI::cmd_go(std::istringstream &iss) {
  pool_->stop();
  join_search();

  SearchLimits limits;
  std::string token;

  while (iss >> token) {
    if (token == "wtime")
      iss >> limits.wtime;
    else if (token == "btime")
      iss >> limits.btime;
    else if (token == "winc")
      iss >> limits.winc;
    else if (token == "binc")
      iss >> limits.binc;
    else if (token == "movetime")
      iss >> limits.movetime;
    else if (token == "movestogo")
      iss >> limits.movestogo;
    else if (token == "depth")
      iss >> limits.depth;
    else if (token == "nodes")
      iss >> limits.nodes;
    else if (token == "mate") {
      int mateN = 0;
      iss >> mateN;
      limits.mate = mateN;
      limits.depth = mateN * 2;
    } else if (token == "infinite")
      limits.infinite = true;
    else if (token == "ponder")
      limits.ponder = true;
  }

  bool startingPonder = limits.ponder && options.ponder;
  isPondering_ = startingPonder;
  ponderStm_ = board.side_to_move();

  bool appliedPonder = false;
  if (startingPonder && ponderMove_ != MOVE_NONE &&
      board.is_legal(ponderMove_)) {
    board.make_move(ponderMove_, ponderState_);
    ponderStm_ = board.side_to_move();
    appliedPonder = true;
  } else if (startingPonder) {
    limits.ponder = false;
    isPondering_ = false;
    startingPonder = false;
  }

  timeman.init(limits, board.side_to_move(), options.moveOverhead);
  timeman.start_clock();

  Move capturedPonderMove = ponderMove_;
  bool capturedApplied = appliedPonder;

  searchThread_ = std::thread([this, capturedPonderMove, capturedApplied]() {
    // pool_->search() launches helpers + runs main thread search.
    // Returns when time is up / depth reached / stopped.
    Move best = pool_->search(board, timeman);

    if (best == MOVE_NONE) {
      MoveList legal = generate_legal(board);
      if (!legal.empty())
        best = *legal.begin();
    }

    if (capturedApplied)
      board.unmake_move(capturedPonderMove);

    isPondering_ = false;

    // Validate ponder move from PV.
    Move ponder = pool_->ponder_move();
    if (ponder != MOVE_NONE) {
      if (board.is_legal(best)) {
        StateInfo tmpSt;
        board.make_move(best, tmpSt);
        if (!board.is_legal(ponder))
          ponder = MOVE_NONE;
        board.unmake_move(best);
      } else {
        ponder = MOVE_NONE;
      }
    }
    ponderMove_ = ponder;

    std::cout << "bestmove " << move_to_uci(best);
    if (ponder != MOVE_NONE && options.ponder)
      std::cout << " ponder " << move_to_uci(ponder);
    std::cout << "\n";
    std::cout.flush();
  });
}

void UCI::cmd_ponderhit() {
  if (!isPondering_) {
    cmd_stop();
    return;
  }
  timeman.ponderhit(ponderStm_, options.moveOverhead);
}

void UCI::cmd_stop() {
  pool_->stop();
  join_search();
}

void UCI::cmd_setoption(std::istringstream &iss) {
  std::string token, name, value;

  iss >> token; // "name"
  while (iss >> token && token != "value")
    name += (name.empty() ? "" : " ") + token;
  while (iss >> token)
    value += (value.empty() ? "" : " ") + token;

  if (name == "Hash") {
    int mb = std::clamp(std::stoi(value), 1, 65536);
    if (mb != options.hashSizeMB) {
      options.hashSizeMB = mb;
      tt.resize(size_t(mb));
    }
  } else if (name == "Clear Hash") {
    tt.clear();
    pool_->clear_all();
  } else if (name == "Move Overhead") {
    options.moveOverhead = std::max(0, std::stoi(value));
  } else if (name == "Ponder") {
    options.ponder = (value == "true");
  } else if (name == "Threads") {
    int hw = std::max(1, int(std::thread::hardware_concurrency()));
    int n = std::clamp(std::stoi(value), 1, hw);
    if (n != options.threads) {
      options.threads = n;
      pool_->set_threads(n);
    }
  } else if (name == "EvalFile") {
    if (!value.empty() && value != "<empty>")
      NNUE::load(value);
  }
}

void UCI::cmd_bench(std::istringstream &iss) {
  int benchDepth = 13; // Default depth
  int threads = 0;     // 0 = auto-detect

  // Parse optional arguments
  std::string token;
  while (iss >> token) {
    if (token == "depth" && (iss >> benchDepth))
      continue;
    if (token == "threads" && (iss >> threads))
      continue;
  }

  // Run benchmark
  auto result = Benchmark::run(benchDepth, threads);

  // Print results
  Benchmark::print_results(result, true);

  // Reset board to start position
  board.set_startpos();
}

void UCI::cmd_display() { board.display(); }

void UCI::cmd_eval() {
  int score = NNUE::evaluate(board);
  std::cout << "NNUE eval: " << score << " cp (white perspective)\n";
  std::cout << "Side to move: "
            << (board.side_to_move() == WHITE ? "white" : "black") << "\n";
  std::cout << "STM eval: " << (board.side_to_move() == WHITE ? score : -score)
            << " cp\n";
  std::cout.flush();
}

void UCI::cmd_perft(std::istringstream &iss) {
  int depth = 1;
  iss >> depth;

  MoveList moves = generate_legal(board);

  if (depth == 1) {
    for (Move m : moves)
      std::cout << move_to_uci(m) << "\n";
    std::cout << "Nodes: " << moves.size() << "\n";
  } else {
    uint64_t total = 0;
    for (Move m : moves) {
      StateInfo si;
      board.make_move(m, si);
      uint64_t n = perft(board, depth - 1);
      board.unmake_move(m);
      std::cout << move_to_uci(m) << ": " << n << "\n";
      total += n;
    }
    std::cout << "Nodes: " << total << "\n";
  }
  std::cout.flush();
}

void UCI::apply_moves(std::istringstream &iss) {
  std::string moveStr;
  while (iss >> moveStr) {
    if (moveHistoryCount >= 1024) {
      std::cerr << "Warning: move history overflow\n";
      break;
    }
    MoveList legal = generate_legal(board);
    bool found = false;
    for (Move m : legal) {
      if (move_to_uci(m) == moveStr) {
        board.make_move(m, moveHistory[moveHistoryCount++]);
        found = true;
        break;
      }
    }
    if (!found)
      break;
  }
}

void UCI::cmd_datagen(std::istringstream &iss) {
  std::string output = "catalyst_data.txt";
  std::string book = "";
  int threads = 4;
  int nodes = 5000;
  int games = 100000;

  std::string token;
  while (iss >> token) {
    if (token == "output")
      iss >> output;
    else if (token == "threads")
      iss >> threads;
    else if (token == "nodes")
      iss >> nodes;
    else if (token == "games")
      iss >> games;
    else if (token == "book")
      iss >> book;
  }

  Datagen::run(output, threads, nodes, games, book);
}

} // namespace Catalyst