// Catalyst is a UCI compliant chess engine
// Copyright (C) 2026 Anany Tanwar
//
// Catalyst is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Catalyst is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#include "datagen.h"

#include <atomic>
#include <cassert>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "board.h"
#include "movegen.h"
#include "search.h"
#include "timeman.h"
#include "tt.h"
#include "types.h"

namespace Catalyst {
namespace Datagen {

    static std::atomic<bool>     g_stop { false };
    static std::atomic<uint64_t> g_positions { 0 };
    static std::atomic<uint64_t> g_games { 0 };

    static void install_sigint_handler() {
        std::signal(SIGINT, [](int) { g_stop.store(true, std::memory_order_seq_cst); });
    }

    static constexpr int WIN_ADJ_SCORE    = 1000;
    static constexpr int WIN_ADJ_PLIES    = 5;
    static constexpr int DRAW_ADJ_SCORE   = 10;
    static constexpr int DRAW_ADJ_MIN_PLY = 60;
    static constexpr int DRAW_ADJ_PLIES   = 10;
    static constexpr int MAX_GAME_PLY     = 400;
    static constexpr int MAX_POSITIONS    = 50;
    static constexpr int SCORE_CLAMP      = 3000;
    static constexpr int FLUSH_EVERY      = 100;

    // clang-format off
    static constexpr std::array<int, 64> kCenterBonus = {
        1, 1, 1, 1, 1, 1, 1, 1,
        1, 2, 2, 2, 2, 2, 2, 1,
        2, 3, 3, 3, 3, 3, 3, 2,
        3, 5, 5, 5, 5, 5, 5, 3,
        4, 6, 7, 9, 9, 7, 6, 4,
        4, 6, 8, 8, 8, 8, 6, 4,
        3, 5, 6, 6, 6, 6, 5, 3,
        1, 1, 4, 4, 4, 4, 1, 1
    };
    // clang-format on

    struct PosEntry {
        std::string fen;
        int         score;
    };

    static Move pick_weighted_move(const MoveList &moves, Color stm, std::mt19937_64 &rng) {
        std::vector<int> weights;
        weights.reserve(moves.size());
        for (int i = 0; i < (int)moves.size(); ++i)
        {
            int sq = int(to_sq(moves.moves[i]));
            if (stm == BLACK)
                sq ^= 56;
            weights.push_back(kCenterBonus[sq]);
        }
        std::discrete_distribution<int> dist(weights.begin(), weights.end());
        return moves.moves[dist(rng)];
    }

    static std::vector<std::string> load_book(const std::string &path) {
        std::vector<std::string> fens;
        if (path.empty())
            return fens;

        std::ifstream f(path);
        if (!f)
        {
            std::cerr << "Datagen: could not open book " << path << "\n";
            return fens;
        }

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty())
                continue;
            std::istringstream       iss(line);
            std::vector<std::string> parts;
            std::string              p;
            while (iss >> p)
                parts.push_back(p);

            if (parts.size() >= 4)
            {
                std::string fen = parts[0] + " " + parts[1] + " " + parts[2] + " " + parts[3];
                fen += (parts.size() >= 6) ? (" " + parts[4] + " " + parts[5]) : " 0 1";
                fens.push_back(fen);
            }
        }
        std::cerr << "Datagen: loaded " << fens.size() << " book positions\n";
        return fens;
    }

    static void sample_positions(
        std::vector<PosEntry> &positions, int max_count, std::mt19937_64 &rng) {
        const int n = (int)positions.size();
        if (n <= max_count)
            return;

        std::vector<PosEntry> sampled;
        sampled.reserve(max_count);
        const double step   = double(n) / double(max_count);
        const double offset = std::uniform_real_distribution<double>(0.0, step)(rng);
        for (int i = 0; i < max_count; ++i)
        {
            int idx = std::min(int(offset + i * step), n - 1);
            sampled.push_back(std::move(positions[idx]));
        }
        positions = std::move(sampled);
    }

    static void datagen_thread(
        int thread_id, const DatagenConfig &cfg, const std::vector<std::string> &book) {
        const std::string thread_file = cfg.output_path + ".t" + std::to_string(thread_id);
        std::ofstream     out(thread_file, std::ios::app);
        if (!out)
        {
            std::cerr << "Datagen: failed to open " << thread_file << "\n";
            return;
        }

        std::mt19937_64 rng(std::random_device { }() + uint64_t(thread_id) * 0x9e3779b97f4a7c15ULL);

        auto searcher      = std::make_unique<Search>();
        searcher->isSilent = true;

        auto statePool = std::make_unique<StateInfo[]>(4096);

        const int target_games      = cfg.games;
        int       games_since_flush = 0;

        for (int game = 0; !g_stop.load(std::memory_order_relaxed)
            && (target_games == 0 || int(g_games.load()) < target_games);
            ++game)
        {
            Board board;
            if (!book.empty())
            {
                std::uniform_int_distribution<size_t> pick(0, book.size() - 1);
                board.set_fen(book[pick(rng)]);
            }
            else
            {
                board.set_startpos();
            }

            std::uniform_int_distribution<int> ply_dist(cfg.random_plies_min, cfg.random_plies_max);
            int                                open_plies = ply_dist(rng);

            int  sp    = 0;
            bool valid = true;

            for (int i = 0; i < open_plies && sp < 4090; ++i)
            {
                MoveList moves = generate_legal(board);
                if (moves.empty())
                {
                    valid = false;
                    break;
                }
                Move m = pick_weighted_move(moves, board.side_to_move(), rng);
                board.make_move(m, statePool[sp++]);
            }

            if (!valid || generate_legal(board).empty())
                continue;

            {
                SearchLimits vl;
                vl.depth     = 8;
                vl.hardNodes = 10000;
                vl.infinite  = false;

                TimeManager vtm;
                vtm.init(vl, board.side_to_move(), 0);
                vtm.start_clock();
                searcher->best_move(board, vtm);
                int verify_score = searcher->last_score();
                tt.clear();

                if (std::abs(verify_score) > cfg.verify_limit)
                    continue;
            }

            std::vector<PosEntry> positions;
            positions.reserve(80);

            int win_plies  = 0;
            int loss_plies = 0;
            int draw_plies = 0;
            int result     = -1;

            for (int ply = 0; ply < MAX_GAME_PLY && sp < 4090; ++ply)
            {
                if (board.is_draw(ply) || board.rule50_count() >= 100)
                {
                    result = 1;
                    break;
                }

                MoveList moves = generate_legal(board);
                if (moves.empty())
                {
                    if (board.in_check())
                        result = (board.side_to_move() == WHITE) ? 0 : 2;
                    else
                        result = 1;
                    break;
                }

                SearchLimits sl;
                sl.depth     = 64;
                sl.softNodes = cfg.soft_nodes;
                sl.hardNodes = cfg.hard_nodes;

                TimeManager tm;
                tm.init(sl, board.side_to_move(), 0);
                tm.start_clock();

                Move best  = searcher->best_move(board, tm);
                int  score = searcher->last_score();

                if (best == MOVE_NONE)
                    break;

                int white_score = (board.side_to_move() == WHITE) ? score : -score;

                bool in_check = board.in_check();
                bool is_noisy = board.is_capture(best) || is_promotion(best);

                if (!in_check && !is_noisy)
                {
                    const int clamped = std::clamp(white_score, -SCORE_CLAMP, SCORE_CLAMP);
                    positions.push_back({ board.get_fen(), clamped });
                }

                if (score > WIN_ADJ_SCORE)
                {
                    ++win_plies;
                    loss_plies = draw_plies = 0;
                }
                else if (score < -WIN_ADJ_SCORE)
                {
                    ++loss_plies;
                    win_plies = draw_plies = 0;
                }
                else if (ply >= DRAW_ADJ_MIN_PLY && std::abs(score) < DRAW_ADJ_SCORE)
                {
                    ++draw_plies;
                    win_plies = loss_plies = 0;
                }
                else
                {
                    win_plies = loss_plies = draw_plies = 0;
                }

                if (win_plies >= WIN_ADJ_PLIES)
                {
                    result = (board.side_to_move() == WHITE) ? 2 : 0;
                    break;
                }
                if (loss_plies >= WIN_ADJ_PLIES)
                {
                    result = (board.side_to_move() == WHITE) ? 0 : 2;
                    break;
                }
                if (draw_plies >= DRAW_ADJ_PLIES)
                {
                    result = 1;
                    break;
                }

                board.make_move(best, statePool[sp++]);
            }

            if (result == -1)
                result = 1;

            sample_positions(positions, MAX_POSITIONS, rng);

            const char *wdl = (result == 2) ? "1.0" : (result == 0) ? "0.0" : "0.5";

            for (const auto &p : positions)
                out << p.fen << " | " << p.score << " | " << wdl << "\n";

            uint64_t pos_count = g_positions.fetch_add(positions.size()) + positions.size();
            uint64_t gm_count  = g_games.fetch_add(1) + 1;

            if (++games_since_flush >= FLUSH_EVERY)
            {
                out.flush();
                games_since_flush = 0;
            }

            if (thread_id == 0 && (gm_count % 256) == 0)
                std::cerr << "Datagen: " << gm_count << " games, " << pos_count << " positions\n";
        }

        out.flush();
        out.close();
    }

    static void merge_files(const DatagenConfig &cfg) {
        std::ofstream merged(cfg.output_path, std::ios::app);
        if (!merged)
        {
            std::cerr << "Datagen: failed to open merged output " << cfg.output_path << "\n";
            return;
        }

        for (int i = 0; i < cfg.threads; ++i)
        {
            const std::string tp = cfg.output_path + ".t" + std::to_string(i);
            std::ifstream     tf(tp);
            if (tf)
                merged << tf.rdbuf();
            std::remove(tp.c_str());
        }
    }

    void run(const DatagenConfig &cfg) {
        std::cerr << "Datagen: " << cfg.threads << " threads"
                  << ", soft nodes=" << cfg.soft_nodes << ", hard nodes=" << cfg.hard_nodes
                  << ", games=" << (cfg.games == 0 ? "unlimited" : std::to_string(cfg.games))
                  << "\nOutput: " << cfg.output_path << "\n";

        install_sigint_handler();

        std::vector<std::string> book = load_book(cfg.book_path);
        if (book.empty())
            std::cerr << "Datagen: no book — using startpos + " << cfg.random_plies_min << "-"
                      << cfg.random_plies_max << " random plies\n";

        tt.resize(128);
        g_positions.store(0);
        g_games.store(0);
        g_stop.store(false);

        std::vector<std::thread> threads;
        threads.reserve(cfg.threads);

        for (int i = 0; i < cfg.threads; ++i)
            threads.emplace_back(datagen_thread, i, std::cref(cfg), std::cref(book));

        for (auto &t : threads)
            t.join();

        merge_files(cfg);

        std::cerr << "Datagen done: " << g_games.load() << " games, " << g_positions.load()
                  << " positions\n";
    }

    DatagenConfig parse_config(const std::string &args) {
        DatagenConfig      cfg;
        std::istringstream iss(args);
        std::string        token;
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
            else if (token == "book")
                iss >> cfg.book_path;
            else if (token == "nodes")
            {
                iss >> cfg.soft_nodes;
                cfg.hard_nodes = cfg.soft_nodes * 4;
            }
        }
        return cfg;
    }

}  // namespace Datagen
}  // namespace Catalyst