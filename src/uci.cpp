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

#include "benchmark.h"
#include "board.h"
#include "datagen.h"
#include "movegen.h"
#include "nnue.h"
#include "thread.h"
#include "timeman.h"
#include "tt.h"
#include "types.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace Catalyst {

namespace {

    [[nodiscard]] uint64_t perft(Board &board, int depth)
    {
        if (depth == 0)
            return 1ULL;
        MoveList moves = generate_legal(board);
        if (depth == 1)
            return uint64_t(moves.size());
        uint64_t nodes = 0;
        for (Move m : moves)
        {
            StateInfo si;
            board.make_move(m, si);
            nodes += perft(board, depth - 1);
            board.unmake_move(m);
        }
        return nodes;
    }

}  // namespace

bool UCI::try_parse_int(const std::string &value, int &out)
{
    if (value.empty())
        return false;

    size_t i        = 0;
    bool   negative = false;
    if (value[0] == '+' || value[0] == '-')
    {
        negative = (value[0] == '-');
        i        = 1;
    }
    if (i == value.size())
        return false;  // just a sign, no digits

    int64_t result = 0;
    for (; i < value.size(); ++i)
    {
        char c = value[i];
        if (c < '0' || c > '9')
            return false;  // non-digit, e.g. "64abc"
        result = result * 10 + (c - '0');
        if (result > INT64_C(4611686018427387904))  // generous overflow guard, well above INT_MAX
            return false;
    }

    if (negative)
        result = -result;
    if (result < INT32_MIN || result > INT32_MAX)
        return false;

    out = int(result);
    return true;
}

UCI::UCI()
    : pool_(std::make_unique<ThreadPool>(1))
    , moveHistory_(std::make_unique<StateInfo[]>(kMaxMoveHistory))
{
    board.set_startpos();
    register_commands();
}

UCI::~UCI()
{
    join_search();
}

void UCI::register_commands()
{
    commands_["uci"]     = [this](auto &iss) { cmd_uci(iss); };
    commands_["isready"] = [this](auto &iss) {
        join_search();
        cmd_isready(iss);
    };
    commands_["ucinewgame"] = [this](auto &iss) {
        join_search();
        cmd_ucinewgame(iss);
    };
    commands_["position"] = [this](auto &iss) {
        join_search();
        cmd_position(iss);
    };
    commands_["go"]        = [this](auto &iss) { cmd_go(iss); };
    commands_["stop"]      = [this](auto &iss) { cmd_stop(iss); };
    commands_["ponderhit"] = [this](auto &iss) { cmd_ponderhit(iss); };
    commands_["setoption"] = [this](auto &iss) { cmd_setoption(iss); };
    commands_["bench"]     = [this](auto &iss) {
        join_search();
        cmd_bench(iss);
    };
    commands_["d"] = [this](auto &iss) {
        join_search();
        cmd_display(iss);
    };
    commands_["perft"] = [this](auto &iss) {
        join_search();
        cmd_perft(iss);
    };
    commands_["eval"] = [this](auto &iss) {
        join_search();
        cmd_eval(iss);
    };
    commands_["datagen"] = [this](auto &iss) {
        join_search();
        cmd_datagen(iss);
    };
    // "quit" is intentionally NOT registered here. It's handled directly
    // in loop() because it needs to terminate the read loop itself, not
    // just execute a side effect like every other command.
}

void UCI::join_search()
{
    if (searchThread_.joinable())
        searchThread_.join();
}

void UCI::loop()
{
    std::string line, token;
    std::cout.setf(std::ios::unitbuf);

    while (std::getline(std::cin, line))
    {
        std::istringstream iss(line);
        if (!(iss >> token))
            continue;

        if (token == "quit")
        {
            cmd_stop(iss);
            join_search();
            break;
        }

        auto it = commands_.find(token);
        if (it != commands_.end())
            it->second(iss);
        // Unknown tokens are silently ignored per the UCI spec.
    }
}

void UCI::cmd_uci(std::istringstream &)
{
    int hwThreads = std::max(1, int(std::thread::hardware_concurrency()));

    std::cout << "id name " << ENGINE_NAME << " " << ENGINE_VERSION << "\n"
              << "id author " << ENGINE_AUTHOR << "\n"
              << "\n"
              << "option name Hash type spin default 64 min 1 max 65536\n"
              << "option name Clear Hash type button\n"
              << "option name Threads type spin default 1 min 1 max " << hwThreads << "\n"
              << "option name Move Overhead type spin default 50 min 0 max 5000\n"
              << "option name Ponder type check default false\n"
              << "option name EvalFile type string default catalyst.nnue\n"
              << "\n"
              << "uciok\n";
    std::cout.flush();
}

void UCI::cmd_isready(std::istringstream &)
{
    std::cout << "readyok\n";
    std::cout.flush();
}

void UCI::cmd_ucinewgame(std::istringstream &)
{
    board.set_startpos();
    tt.clear();
    pool_->clear_all();
    moveHistoryCount_ = 0;
    ponderMove_.store(MOVE_NONE, std::memory_order_relaxed);
    isPondering_.store(false, std::memory_order_relaxed);
}

void UCI::cmd_position(std::istringstream &iss)
{
    pool_->stop_search();
    join_search();
    moveHistoryCount_ = 0;
    std::string token;
    iss >> token;

    if (token == "startpos")
    {
        board.set_startpos();
        iss >> token;
    }
    else if (token == "fen")
    {
        std::string fen, part;
        for (int i = 0; i < 6 && iss >> part; ++i)
            fen += (i ? " " : "") + part;
        board.set_fen(fen);
        iss >> token;
    }
    else
    {
        board.set_startpos();
    }

    if (token == "moves")
    {
        if (!apply_moves(iss))
            std::cerr << "info string warning: stopped at first illegal/unparseable move\n";
    }
}

void UCI::cmd_go(std::istringstream &iss)
{
    pool_->stop_search();
    join_search();

    SearchLimits limits;
    std::string  token;

    while (iss >> token)
    {
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
        else if (token == "mate")
        {
            int mateN = 0;
            iss >> mateN;
            limits.mate  = mateN;
            limits.depth = mateN * 2;
        }
        else if (token == "infinite")
            limits.infinite = true;
        else if (token == "ponder")
            limits.ponder = true;
    }

    bool startingPonder   = limits.ponder && options.ponder;
    Move storedPonderMove = ponderMove_.load(std::memory_order_relaxed);

    isPondering_.store(startingPonder, std::memory_order_relaxed);
    ponderStm_ = board.side_to_move();

    bool appliedPonder = false;
    if (startingPonder && storedPonderMove != MOVE_NONE && board.is_legal(storedPonderMove))
    {
        board.make_move(storedPonderMove, ponderState_);
        ponderStm_    = board.side_to_move();
        appliedPonder = true;
    }
    else if (startingPonder)
    {
        limits.ponder = false;
        isPondering_.store(false, std::memory_order_relaxed);
        startingPonder = false;
    }

    timeman.init(limits, board.side_to_move(), options.moveOverhead);
    timeman.start_clock();

    Move capturedPonderMove = storedPonderMove;
    bool capturedApplied    = appliedPonder;

    searchThread_ = std::thread([this, capturedPonderMove, capturedApplied]() {
        Move best = pool_->search(board, timeman);

        if (best == MOVE_NONE)
        {
            MoveList legal = generate_legal(board);
            if (!legal.empty())
                best = *legal.begin();
        }

        if (capturedApplied)
            board.unmake_move(capturedPonderMove);

        isPondering_.store(false, std::memory_order_relaxed);

        Move ponder = pool_->ponder_move();
        if (ponder != MOVE_NONE)
        {
            if (board.is_legal(best))
            {
                StateInfo tmpSt;
                board.make_move(best, tmpSt);
                if (!board.is_legal(ponder))
                    ponder = MOVE_NONE;
                board.unmake_move(best);
            }
            else
            {
                ponder = MOVE_NONE;
            }
        }
        ponderMove_.store(ponder, std::memory_order_relaxed);

        std::cout << "bestmove " << move_to_uci(best);
        if (ponder != MOVE_NONE && options.ponder)
            std::cout << " ponder " << move_to_uci(ponder);
        std::cout << "\n";
        std::cout.flush();
    });
}

void UCI::cmd_ponderhit(std::istringstream &iss)
{
    if (!isPondering_.load(std::memory_order_relaxed))
    {
        cmd_stop(iss);
        return;
    }
    timeman.ponderhit(ponderStm_, options.moveOverhead);
}

void UCI::cmd_stop(std::istringstream &)
{
    pool_->stop_search();
    join_search();
}

void UCI::cmd_setoption(std::istringstream &iss)
{
    std::string token, name, value;

    iss >> token;
    while (iss >> token && token != "value")
        name += (name.empty() ? "" : " ") + token;
    while (iss >> token)
        value += (value.empty() ? "" : " ") + token;

    if (name == "Hash")
    {
        int mb = 0;
        if (!try_parse_int(value, mb))
        {
            std::cerr << "info string warning: invalid Hash value '" << value << "'\n";
            return;
        }
        mb = std::clamp(mb, 1, 65536);
        if (mb != options.hashSizeMB)
        {
            options.hashSizeMB = mb;
            tt.resize(size_t(mb));
        }
    }
    else if (name == "Clear Hash")
    {
        tt.clear();
        pool_->clear_all();
    }
    else if (name == "Move Overhead")
    {
        int overhead = 0;
        if (!try_parse_int(value, overhead))
        {
            std::cerr << "info string warning: invalid Move Overhead value '" << value << "'\n";
            return;
        }
        options.moveOverhead = std::max(0, overhead);
    }
    else if (name == "Ponder")
    {
        options.ponder = (value == "true");
    }
    else if (name == "Threads")
    {
        int n = 0;
        if (!try_parse_int(value, n))
        {
            std::cerr << "info string warning: invalid Threads value '" << value << "'\n";
            return;
        }
        int hw = std::max(1, int(std::thread::hardware_concurrency()));
        n      = std::clamp(n, 1, hw);
        if (n != options.threads)
        {
            options.threads = n;
            pool_->set_threads(n);
        }
    }
    else if (name == "EvalFile")
    {
        if (!value.empty() && value != "<empty>")
            NNUE::load(value);
    }
}

void UCI::cmd_bench(std::istringstream &iss)
{
    int         benchDepth = kDefaultBenchDepth;
    int         threads    = options.threads;
    std::string token;
    while (iss >> token)
    {
        if (token == "depth")
        {
            int d;
            if (iss >> d)
                benchDepth = d;
        }
        else if (token == "threads")
        {
            int t;
            if (iss >> t)
                threads = t;
        }
    }

    auto result = Benchmark::run(benchDepth, threads);
    Benchmark::print_results(result);
    board.set_startpos();
}

void UCI::cmd_display(std::istringstream &)
{
    board.display();
}

void UCI::cmd_eval(std::istringstream &)
{
    int score = NNUE::evaluate(board);
    std::cout << "NNUE eval (STM): " << score << " cp\n";
    std::cout << "Side to move: " << (board.side_to_move() == WHITE ? "white" : "black") << "\n";
    std::cout.flush();
}

void UCI::cmd_perft(std::istringstream &iss)
{
    int depth = 1;
    iss >> depth;

    MoveList moves = generate_legal(board);

    if (depth == 1)
    {
        for (Move m : moves)
            std::cout << move_to_uci(m) << "\n";
        std::cout << "Nodes: " << moves.size() << "\n";
    }
    else
    {
        uint64_t total = 0;
        for (Move m : moves)
        {
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

bool UCI::apply_moves(std::istringstream &iss)
{
    std::string moveStr;
    while (iss >> moveStr)
    {
        if (moveHistoryCount_ >= kMaxMoveHistory)
        {
            std::cerr << "info string warning: move history overflow (limit " << kMaxMoveHistory
                      << ")\n";
            return false;
        }
        MoveList moves = generate_legal(board);
        bool     found = false;
        for (Move m : moves)
        {
            if (move_to_uci(m) == moveStr)
            {
                board.make_move(m, moveHistory_[moveHistoryCount_++]);
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

void UCI::cmd_datagen(std::istringstream &iss)
{
    Datagen::DatagenConfig cfg;
    std::string            token;
    while (iss >> token)
    {
        if (token == "output")
            iss >> cfg.output_path;
        else if (token == "threads")
            iss >> cfg.threads;
        else if (token == "games")
            iss >> cfg.games;
        else if (token == "softnodes")
            iss >> cfg.soft_nodes;
        else if (token == "hardnodes")
            iss >> cfg.hard_nodes;
        else if (token == "nodes")
        {
            iss >> cfg.soft_nodes;
            cfg.hard_nodes = cfg.soft_nodes * 4;
        }
        else if (token == "book")
            iss >> cfg.book_path;
    }
    Datagen::run(cfg);
}

}  // namespace Catalyst
