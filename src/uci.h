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

#include "board.h"
#include "thread.h"
#include "timeman.h"

#include <atomic>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Catalyst {

// Maximum moves we can replay from a UCI "position ... moves ..." command.
// 1024 covers any realistic game length (longest recorded tournament games
// are well under 600 plies); kept as a named constant instead of a magic
// number scattered across the implementation.
inline constexpr int kMaxMoveHistory = 1024;

// Default depth used by "bench" when the caller doesn't specify one.
inline constexpr int kDefaultBenchDepth = 13;

struct UCIOptions {
    int  hashSizeMB   = 64;
    int  moveOverhead = 50;
    int  threads      = 1;
    bool ponder       = false;
};

class UCI {
public:
    UCI();
    ~UCI();
    void loop();

private:
    Board board;

    std::unique_ptr<ThreadPool>  pool_;
    std::unique_ptr<StateInfo[]> moveHistory_;
    TimeManager                  timeman;
    UCIOptions                   options;
    int                          moveHistoryCount_ = 0;

    std::thread searchThread_;

    // Pondering state. isPondering_ and ponderMove_ are written from the
    // search thread (in the cmd_go lambda, after search returns) and read
    // from the main thread (cmd_ponderhit, cmd_go's next invocation, cmd_stop).
    // They're atomic so that cross-thread visibility is well-defined instead
    // of relying on incidental timing.
    std::atomic<bool> isPondering_ { false };
    std::atomic<Move> ponderMove_ { MOVE_NONE };
    StateInfo         ponderState_;
    Color             ponderStm_ = WHITE;

    // Command dispatch table: token -> handler. Built once in the
    // constructor. Adding a new UCI command means adding one line to the
    // table instead of another branch in a growing if/else chain.
    using CommandHandler = std::function<void(std::istringstream &)>;
    std::unordered_map<std::string, CommandHandler> commands_;
    void                                            register_commands();

    void join_search();

    // Parses an integer option value safely. Returns false (and leaves out
    // unchanged) if the value isn't a valid integer, instead of throwing.
    [[nodiscard]] static bool try_parse_int(const std::string &value, int &out);

    void cmd_uci(std::istringstream &iss);
    void cmd_isready(std::istringstream &iss);
    void cmd_ucinewgame(std::istringstream &iss);
    void cmd_position(std::istringstream &iss);
    void cmd_go(std::istringstream &iss);
    void cmd_ponderhit(std::istringstream &iss);
    void cmd_stop(std::istringstream &iss);
    void cmd_setoption(std::istringstream &iss);
    void cmd_bench(std::istringstream &iss);
    void cmd_eval(std::istringstream &iss);
    void cmd_display(std::istringstream &iss);
    void cmd_perft(std::istringstream &iss);
    void cmd_datagen(std::istringstream &iss);

    // Returns false if a move failed to parse/apply, true if all moves in
    // the stream were applied successfully.
    [[nodiscard]] bool apply_moves(std::istringstream &iss);
};

}  // namespace Catalyst