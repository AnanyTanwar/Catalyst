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

#include "datagen.h"
#include "board.h"
#include "movegen.h"
#include "search.h"
#include "timeman.h"
#include "tt.h"
#include "types.h"

#include <atomic>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

namespace Catalyst {
namespace Datagen {
static std::atomic<uint64_t> g_positions{0};
static std::atomic<uint64_t> g_games{0};
static std::mutex g_io_mutex;

static std::vector<std::string> load_epd_book(const std::string &path) {
  std::vector<std::string> fens;
  if (path.empty())
    return fens;

  std::ifstream f(path);
  if (!f) {
    std::cerr << "Datagen: could not open book " << path << "\n";
    return fens;
  }

  std::string line;
  while (std::getline(f, line)) {
    if (line.empty())
      continue;
    std::istringstream iss(line);
    std::string part;
    std::vector<std::string> parts;
    while (iss >> part)
      parts.push_back(part);
    if (parts.size() >= 4) {
      std::string fen =
          parts[0] + " " + parts[1] + " " + parts[2] + " " + parts[3];

      if (parts.size() >= 6)
        fen += " " + parts[4] + " " + parts[5];
      else
        fen += " 0 1";
      fens.push_back(fen);
    }
  }
  std::cerr << "Datagen: loaded " << fens.size() << " book positions\n";
  return fens;
}

static void datagen_thread(const std::string &output_path, int nodes_per_move,
                           int target_games, int thread_id,
                           const std::vector<std::string> &book) {
  std::mt19937 rng(std::random_device{}() + thread_id * 12345);

  std::string thread_path = output_path + ".t" + std::to_string(thread_id);
  std::ofstream out(thread_path);
  if (!out) {
    std::cerr << "Datagen: failed to open " << thread_path << "\n";
    return;
  }

  auto search = std::make_unique<Search>();
  search->isSilent = true;
  auto statePool = std::make_unique<StateInfo[]>(2048);

  while ((int)g_games.load() < target_games) {
    Board board;
    int sp = 0;

    if (!book.empty()) {
      std::uniform_int_distribution<size_t> bookDist(0, book.size() - 1);
      board.set_fen(book[bookDist(rng)]);
    } else {
      board.set_startpos();
    }

    std::uniform_int_distribution<int> openDist(8, 9);
    int openPlies = openDist(rng);
    bool valid = true;

    for (int i = 0; i < openPlies && sp < 2040; ++i) {
      MoveList moves = generate_legal(board);
      if (moves.empty()) {
        valid = false;
        break;
      }
      std::uniform_int_distribution<int> md(0, (int)moves.size() - 1);
      board.make_move(moves.moves[md(rng)], statePool[sp++]);
    }

    if (!valid)
      continue;
    if (generate_legal(board).empty())
      continue;

    {
      SearchLimits ql;
      ql.nodes = 1000;
      ql.depth = 64;
      TimeManager qtm;
      qtm.init(ql, board.side_to_move(), 0);
      qtm.start_clock();
      search->best_move(board, qtm);
      int openScore = search->last_score();
      if (std::abs(openScore) > 1000)
        continue;
    }

    struct PosEntry {
      std::string fen;
      int score;
    };
    std::vector<PosEntry> positions;
    positions.reserve(80);
    int result = 1;

    for (int ply = 0; ply < 400 && sp < 2040; ++ply) {
      if (board.is_draw(ply)) {
        result = 1;
        break;
      }

      MoveList moves = generate_legal(board);
      if (moves.empty()) {
        result = board.in_check() ? (board.side_to_move() == WHITE ? 0 : 2) : 1;
        break;
      }

      SearchLimits limits;
      limits.nodes = nodes_per_move;
      limits.depth = 64;
      TimeManager tm;
      tm.init(limits, board.side_to_move(), 0);
      tm.start_clock();

      Move best = search->best_move(board, tm);
      if (best == MOVE_NONE)
        break;

      int score = search->last_score();

      if (!board.in_check() && std::abs(score) < 1000)
        positions.push_back({board.get_fen(), score});

      if (std::abs(score) >= 1000) {
        result = score > 0 ? (board.side_to_move() == WHITE ? 2 : 0)
                           : (board.side_to_move() == WHITE ? 0 : 2);
        break;
      }

      board.make_move(best, statePool[sp++]);
    }

    for (auto &p : positions) {
      std::string wdl_str = (result == 2)   ? "1.0"
                            : (result == 0) ? "0.0"
                                            : "0.5";
      out << p.fen << " | " << p.score << " | " << wdl_str << "\n";
    }

    g_positions.fetch_add(positions.size());
    g_games.fetch_add(1);

    if (g_games % 50 == 0) {
      std::lock_guard<std::mutex> lock(g_io_mutex);
      std::cerr << "Datagen: " << g_games.load() << " games, "
                << g_positions.load() << " positions\n";
    }
  }
  out.close();
}

void run(const std::string &output_path, int threads, int nodes_per_move,
         int games, const std::string &book_path) {
  std::cerr << "Datagen: " << threads << " threads, " << games << " games, "
            << nodes_per_move << " nodes/move\n";
  std::cerr << "Output: " << output_path << "\n";

  std::vector<std::string> book = load_epd_book(book_path);
  if (book.empty())
    std::cerr
        << "Datagen: no book loaded, starting from startpos + random plies\n";

  tt.resize(16);
  g_positions = 0;
  g_games = 0;

  std::vector<std::thread> pool;
  for (int i = 0; i < threads; ++i)
    pool.emplace_back(datagen_thread, output_path, nodes_per_move, games, i,
                      std::ref(book));
  for (auto &t : pool)
    t.join();

  std::ofstream merged(output_path);
  for (int i = 0; i < threads; ++i) {
    std::string tp = output_path + ".t" + std::to_string(i);
    std::ifstream tf(tp);
    merged << tf.rdbuf();
    std::remove(tp.c_str());
  }

  std::cerr << "Datagen done! " << g_games.load() << " games, "
            << g_positions.load() << " positions\n";
}
} // namespace Datagen
} // namespace Catalyst