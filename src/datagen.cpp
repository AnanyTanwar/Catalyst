// Catalyst is a UCI compliant chess engine
// Copyright (C) 2026 Anany Tanwar

// Catalyst is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Catalyst is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#include "datagen.h"

#include "board.h"
#include "movegen.h"
#include "search.h"
#include "timeman.h"
#include "tt.h"
#include "types.h"

#include <atomic>
#include <cassert>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace Catalyst {
namespace Datagen {

    static std::atomic<bool>     g_stop { false };
    static std::atomic<uint64_t> g_games { 0 };
    static std::atomic<uint64_t> g_positions { 0 };

    static void install_sigint_handler()
    {
        std::signal(SIGINT, [](int) { g_stop.store(true, std::memory_order_seq_cst); });
    }

    PackedBoard PackedBoard::pack(const Board &board, int16_t score, Outcome outcome)
    {
        PackedBoard packed { };
        std::memset(packed.pieces, 0, sizeof(packed.pieces));

        uint64_t occ_bb = board.pieces();
        packed.occ      = occ_bb;

        uint64_t unmoved_rook_bb = 0;
        {
            const int cr = board.castling_rights();
            if (cr & WHITE_OO)
                unmoved_rook_bb |= square_bb(board.castling_rook_square(WHITE_OO));
            if (cr & WHITE_OOO)
                unmoved_rook_bb |= square_bb(board.castling_rook_square(WHITE_OOO));
            if (cr & BLACK_OO)
                unmoved_rook_bb |= square_bb(board.castling_rook_square(BLACK_OO));
            if (cr & BLACK_OOO)
                unmoved_rook_bb |= square_bb(board.castling_rook_square(BLACK_OOO));
        }

        int      nibble_idx = 0;
        uint64_t tmp        = occ_bb;
        while (tmp)
        {
            int sq_int = __builtin_ctzll(tmp);
            tmp &= tmp - 1;
            Square sq = Square(sq_int);

            Piece p   = board.piece_on(sq);
            int   pt  = static_cast<int>(piece_type(p));
            int   col = (piece_color(p) == BLACK) ? 1 : 0;

            if (pt == static_cast<int>(ROOK) && (unmoved_rook_bb & square_bb(sq)))
                pt = 6;

            uint8_t nibble = static_cast<uint8_t>((col << 3) | pt);
            assert(nibble <= 0x0F);
            assert(nibble_idx < 32);

            int byte_idx = nibble_idx / 2;
            if (nibble_idx % 2 == 0)
                packed.pieces[byte_idx] = nibble;
            else
                packed.pieces[byte_idx] |= uint8_t(nibble << 4);

            nibble_idx++;
        }

        uint8_t ep_raw = static_cast<uint8_t>(board.ep_square());
        packed.stm_ep
            = static_cast<uint8_t>((board.side_to_move() == BLACK ? 0x80u : 0x00u) | ep_raw);
        packed.halfmoves = static_cast<uint8_t>(board.rule50_count());
        packed.fullmoves = static_cast<uint16_t>(board.game_ply() / 2 + 1);
        packed.eval      = score;
        packed.outcome   = outcome;
        packed.pad       = 0;

        return packed;
    }

    ViriMove ViriMove::from_move(Move m, int score)
    {
        ViriMove vm { };

        int from  = from_sq(m);
        int to    = to_sq(m);
        int mtype = move_type(m);

        static constexpr uint16_t vf_type[4] = {
            0x0000,
            0x8000,
            0x4000,
            0xC000,
        };

        int promo = 0;

        if (mtype == MT_PROMOTION)
        {
            promo = static_cast<int>(promo_piece(m)) - static_cast<int>(KNIGHT);
            promo = std::clamp(promo, 0, 3);
        }

        vm.move = static_cast<uint16_t>(
            (from & 0x3F) | ((to & 0x3F) << 6) | ((promo & 0x3) << 12) | vf_type[mtype & 0x3]);

        vm.score = static_cast<int16_t>(
            std::clamp(score, static_cast<int>(INT16_MIN), static_cast<int>(INT16_MAX)));

        return vm;
    }

    static std::vector<std::string> load_book(const std::string &path)
    {
        std::vector<std::string> fens;
        if (path.empty())
            return fens;

        std::ifstream f(path);
        if (!f)
        {
            std::cerr << "Datagen: could not open book '" << path << "'\n";
            return fens;
        }

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty())
                continue;

            std::istringstream       iss(line);
            std::vector<std::string> parts;
            std::string              tok;
            while (iss >> tok)
                parts.push_back(tok);

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

    struct GameRecord {
        PackedBoard           root;
        std::vector<ViriMove> moves;
        Outcome               outcome = Outcome::Invalid;
    };

    static void datagen_thread(int      thread_id,
        const DatagenConfig            &cfg,
        const std::vector<std::string> &book)
    {
        const std::string filename = cfg.output_path + ".t" + std::to_string(thread_id);
        std::ofstream     out(filename, std::ios::binary | std::ios::app);
        if (!out)
        {
            std::cerr << "Datagen: failed to open output file '" << filename << "'\n";
            return;
        }

        std::mt19937_64 rng(
            std::random_device { }() ^ (static_cast<uint64_t>(thread_id) * 0x9e3779b97f4a7c15ULL));

        auto searcher_white      = std::make_unique<Search>();
        searcher_white->isSilent = true;
        auto searcher_black      = std::make_unique<Search>();
        searcher_black->isSilent = true;

        constexpr int STATE_POOL_SIZE = 4096;
        auto          statePool       = std::make_unique<StateInfo[]>(STATE_POOL_SIZE);

        const int     target_games  = cfg.games;
        int           flush_counter = 0;
        constexpr int FLUSH_EVERY   = 64;

        while (!g_stop.load(std::memory_order_relaxed))
        {
            if (target_games > 0
                && static_cast<int>(g_games.load(std::memory_order_relaxed)) >= target_games)
                break;

            Board board;
            int   sp = 0;

            searcher_white->clear_tables();
            searcher_black->clear_tables();

            if (!book.empty())
            {
                std::uniform_int_distribution<size_t> pick(0, book.size() - 1);
                board.set_fen(book[pick(rng)]);
            }
            else
            {
                board.set_startpos();
            }

            {
                std::uniform_int_distribution<int> ply_dist(cfg.random_plies_min,
                    cfg.random_plies_max);
                const int                          open_plies = ply_dist(rng);

                bool valid = true;
                for (int i = 0; i < open_plies && sp < STATE_POOL_SIZE - 2; ++i)
                {
                    MoveList moves = generate_legal(board);
                    if (moves.empty())
                    {
                        valid = false;
                        break;
                    }

                    std::uniform_int_distribution<int> mpick(0, static_cast<int>(moves.size()) - 1);
                    board.make_move(moves.moves[mpick(rng)], statePool[sp++]);
                }

                if (!valid || generate_legal(board).empty())
                    continue;
            }

            {
                SearchLimits vl;
                vl.depth    = cfg.verify_depth;
                vl.infinite = false;

                TimeManager vtm;
                vtm.init(vl, board.side_to_move(), 0);
                vtm.start_clock();

                searcher_white->best_move(board, vtm);
                const int vscore = searcher_white->last_score();

                if (std::abs(vscore) > cfg.verify_limit)
                    continue;
            }

            GameRecord rec;
            rec.root = PackedBoard::pack(board, 0, Outcome::Invalid);
            rec.moves.reserve(256);

            int     win_plies  = 0;
            int     loss_plies = 0;
            int     draw_plies = 0;
            Outcome outcome    = Outcome::Invalid;

            for (int ply = 0; sp < STATE_POOL_SIZE - 2; ++ply)
            {
                if (board.rule50_count() >= 100 || board.is_draw(ply))
                {
                    outcome = Outcome::Draw;
                    break;
                }

                MoveList moves = generate_legal(board);
                if (moves.empty())
                {
                    outcome = board.in_check() ? (board.side_to_move() == WHITE ? Outcome::BlackWin
                                                                                : Outcome::WhiteWin)
                                               : Outcome::Draw;
                    break;
                }

                SearchLimits sl;
                sl.depth     = 64;
                sl.softNodes = cfg.soft_nodes;
                sl.hardNodes = cfg.hard_nodes;

                TimeManager tm;
                tm.init(sl, board.side_to_move(), 0);
                tm.start_clock();

                Search *cur
                    = (board.side_to_move() == WHITE) ? searcher_white.get() : searcher_black.get();
                Move best  = cur->best_move(board, tm);
                int  score = cur->last_score();

                if (best == MOVE_NONE)
                    break;

                const int white_score = (board.side_to_move() == WHITE) ? score : -score;

                if (std::abs(score) >= SCORE_MATE_IN_MAX_PLY)
                {
                    outcome           = (white_score > 0) ? Outcome::WhiteWin : Outcome::BlackWin;
                    const int mate_cp = (white_score > 0) ? cfg.score_clamp : -cfg.score_clamp;
                    rec.moves.push_back(ViriMove::from_move(best, mate_cp));
                    break;
                }

                const int clamped  = std::clamp(white_score, -cfg.score_clamp, cfg.score_clamp);
                bool      filtered = board.in_check() || board.is_capture(best);
                if (!filtered)
                    rec.moves.push_back(ViriMove::from_move(best, clamped));

                const int abs_score = std::abs(white_score);

                if (white_score > cfg.win_adj_score)
                {
                    win_plies++;
                    loss_plies = draw_plies = 0;
                }
                else if (white_score < -cfg.win_adj_score)
                {
                    loss_plies++;
                    win_plies = draw_plies = 0;
                }
                else if (ply >= cfg.draw_adj_min_ply && abs_score < cfg.draw_adj_score)
                {
                    draw_plies++;
                    win_plies = loss_plies = 0;
                }
                else
                {
                    win_plies = loss_plies = draw_plies = 0;
                }

                if (win_plies >= cfg.win_adj_plies)
                {
                    outcome = Outcome::WhiteWin;
                    break;
                }
                if (loss_plies >= cfg.win_adj_plies)
                {
                    outcome = Outcome::BlackWin;
                    break;
                }
                if (draw_plies >= cfg.draw_adj_plies)
                {
                    outcome = Outcome::Draw;
                    break;
                }

                board.make_move(best, statePool[sp++]);
            }

            if (outcome == Outcome::Invalid)
                continue;

            rec.root.outcome = outcome;

            out.write(reinterpret_cast<const char *>(&rec.root), sizeof(PackedBoard));
            out.write(reinterpret_cast<const char *>(rec.moves.data()),
                sizeof(ViriMove) * rec.moves.size());
            const ViriMove sentinel = ViriMove::sentinel();
            out.write(reinterpret_cast<const char *>(&sentinel), sizeof(ViriMove));

            const uint64_t gc = g_games.fetch_add(1, std::memory_order_relaxed) + 1;
            g_positions.fetch_add(rec.moves.size(), std::memory_order_relaxed);

            if (++flush_counter >= FLUSH_EVERY)
            {
                out.flush();
                flush_counter = 0;
            }

            if (thread_id == 0 && (gc % 512) == 0)
            {
                std::cerr << "Datagen: " << gc << " games, "
                          << g_positions.load(std::memory_order_relaxed) << " positions\n";
            }
        }

        out.flush();
        out.close();
    }

    static void merge_files(const DatagenConfig &cfg)
    {
        std::ofstream merged(cfg.output_path, std::ios::binary | std::ios::app);
        if (!merged)
        {
            std::cerr << "Datagen: failed to open merged output '" << cfg.output_path << "'\n";
            return;
        }

        for (int i = 0; i < cfg.threads; ++i)
        {
            const std::string tp = cfg.output_path + ".t" + std::to_string(i);
            {
                std::ifstream tf(tp, std::ios::binary);
                if (tf)
                    merged << tf.rdbuf();
            }
            std::remove(tp.c_str());
        }
    }

    void run(const DatagenConfig &cfg)
    {
        std::cerr << "Datagen: " << cfg.threads << " thread(s)"
                  << "  soft=" << cfg.soft_nodes << " hard=" << cfg.hard_nodes
                  << "  games=" << (cfg.games == 0 ? "unlimited" : std::to_string(cfg.games))
                  << "\n  output: " << cfg.output_path << "\n";

        install_sigint_handler();

        const std::vector<std::string> book = load_book(cfg.book_path);
        if (book.empty())
            std::cerr << "Datagen: no book — startpos + " << cfg.random_plies_min << "-"
                      << cfg.random_plies_max << " random plies\n";

        tt.resize(128);

        g_games.store(0);
        g_positions.store(0);
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

    DatagenConfig parse_config(const std::string &args)
    {
        DatagenConfig      cfg;
        std::istringstream iss(args);
        std::string        tok;

        while (iss >> tok)
        {
            if (tok == "output")
                iss >> cfg.output_path;
            else if (tok == "threads")
                iss >> cfg.threads;
            else if (tok == "games")
                iss >> cfg.games;
            else if (tok == "softnodes")
                iss >> cfg.soft_nodes;
            else if (tok == "hardnodes")
                iss >> cfg.hard_nodes;
            else if (tok == "book")
                iss >> cfg.book_path;
            else if (tok == "dfrc")
                cfg.dfrc = true;
            else if (tok == "verifydepth")
                iss >> cfg.verify_depth;
            else if (tok == "verifylimit")
                iss >> cfg.verify_limit;
            else if (tok == "minply")
                iss >> cfg.random_plies_min;
            else if (tok == "maxply")
                iss >> cfg.random_plies_max;
            else if (tok == "winadj")
                iss >> cfg.win_adj_score;
            else if (tok == "winadjplies")
                iss >> cfg.win_adj_plies;
            else if (tok == "drawadjscore")
                iss >> cfg.draw_adj_score;
            else if (tok == "drawadjplies")
                iss >> cfg.draw_adj_plies;
            else if (tok == "drawadjminply")
                iss >> cfg.draw_adj_min_ply;
            else if (tok == "scoreclamp")
                iss >> cfg.score_clamp;
            else if (tok == "nodes")
            {
                iss >> cfg.soft_nodes;
                cfg.hard_nodes = cfg.soft_nodes * 4;
            }
        }

        return cfg;
    }

}  // namespace Datagen
}  // namespace Catalyst
