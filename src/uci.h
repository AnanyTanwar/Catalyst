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

#pragma once

#include <atomic>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "board.h"
#include "thread.h"
#include "timeman.h"

namespace Catalyst {

struct UCIOptions {
  int hashSizeMB = 64;
  int moveOverhead = 50;
  int threads = 1;
  bool ponder = false;
};

class UCI {
public:
  UCI();
  ~UCI();
  void loop();

private:
  Board board;

  // ThreadPool owns all Search objects (main + helpers).
  // Heap-allocated because Search is ~4 MB per instance.
  std::unique_ptr<ThreadPool> pool_;

  std::unique_ptr<StateInfo[]>
      moveHistory; 
  TimeManager timeman;
  UCIOptions options;
  int moveHistoryCount = 0;

  std::thread searchThread_;

  // ── Pondering state ───────────────────────────────────────────────────────
  Move ponderMove_ = MOVE_NONE;
  StateInfo ponderState_;
  bool isPondering_ = false;
  Color ponderStm_ = WHITE;

  void join_search();

  void cmd_uci();
  void cmd_isready();
  void cmd_ucinewgame();
  void cmd_position(std::istringstream &iss);
  void cmd_go(std::istringstream &iss);
  void cmd_ponderhit();
  void cmd_stop();
  void cmd_setoption(std::istringstream &iss);
  void cmd_bench(std::istringstream &iss); 
  void cmd_eval();
  void cmd_display();
  void cmd_perft(std::istringstream &iss);
  void apply_moves(std::istringstream &iss);
  void cmd_datagen(std::istringstream &iss);
};

} // namespace Catalyst