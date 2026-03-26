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

#include "tt.h"

#include <algorithm>
#include <climits>
#include <iostream>
#include <thread>
#include <vector>

namespace Catalyst {

TT tt;

TT::TT() { resize(64); }

TT::~TT() {
#if defined(_WIN32)
  _aligned_free(table);
#else
  free(table);
#endif
}

void TT::resize(size_t mb) {
  mb = std::max(mb, size_t(1));

  size_t bytes = mb * 1024 * 1024;
  size_t count = bytes / sizeof(TTCluster);
  size_t power2 = 1;
  while (power2 * 2 <= count)
    power2 *= 2;

  numClusters = power2;
  clusterMask = power2 - 1;

  if (table) {
#if defined(_WIN32)
    _aligned_free(table);
#else
    free(table);
#endif
  }

#if defined(_WIN32)
  table = reinterpret_cast<TTCluster *>(
      _aligned_malloc(numClusters * sizeof(TTCluster), 64));
#else
  if (posix_memalign(reinterpret_cast<void **>(&table), 64,
                     numClusters * sizeof(TTCluster)) != 0)
    table = nullptr;
#endif

  if (!table) {
    std::cerr << "TT allocation failed, retrying with half size\n";
    resize(mb / 2);
    return;
  }

  clear();
}

void TT::clear() {
  if (!table)
    return;

  size_t numThreads =
      std::min(size_t(std::thread::hardware_concurrency()), size_t(8));
  if (numThreads == 0)
    numThreads = 1;

  size_t perThread = numClusters / numThreads;
  std::vector<std::thread> threads;
  threads.reserve(numThreads);

  for (size_t t = 0; t < numThreads; ++t) {
    size_t start = t * perThread;
    size_t end = (t == numThreads - 1) ? numClusters : start + perThread;
    threads.emplace_back([this, start, end]() {
      std::memset(&table[start], 0, (end - start) * sizeof(TTCluster));
    });
  }
  for (auto &t : threads)
    t.join();

  currentGen = 1;
}

void TT::new_search() {
  ++currentGen;
  if (currentGen == 0)
    currentGen = 1;
}

void TT::prefetch(Key key) const {
  if (table)
    PREFETCH(&table[index(key)]);
}

static inline int replacement_score(uint8_t currentGen, const TTEntry &e) {
  int age = int(uint8_t(currentGen - e.generation));
  int score = age * 8 - int(e.depth) * 4;
  if (e.flag == TT_EXACT)
    score -= 64;
  return score;
}

TTEntry *TT::probe(Key key, bool &found) {
  TTCluster *cluster = &table[index(key)];
  uint16_t k16 = uint16_t(key >> 48);
  uint16_t k16b = uint16_t(key >> 32);

  TTEntry *replace = nullptr;
  int bestEvict = INT_MIN;

  for (int i = 0; i < 4; ++i) {
    TTEntry &e = cluster->entries[i];

    if (e.generation == 0) {
      found = false;
      return &e;
    }

    if (e.key16 == k16 && e.key16b == k16b) {
      found = true;
      e.generation = currentGen;
      return &e;
    }

    int rs = replacement_score(currentGen, e);
    if (rs > bestEvict) {
      bestEvict = rs;
      replace = &e;
    }
  }

  found = false;
  return replace;
}

void TT::store(Key key, int score, int depth, TTFlag flag, Move move,
               int eval) {
  TTCluster *cluster = &table[index(key)];
  uint16_t k16 = uint16_t(key >> 48);
  uint16_t k16b = uint16_t(key >> 32);

  TTEntry *replace = nullptr;
  int bestEvict = INT_MIN;

  for (int i = 0; i < 4; ++i) {
    TTEntry &e = cluster->entries[i];

    if (e.key16 == k16 && e.key16b == k16b && e.generation != 0) {
      // Same position — update in place, protect deeper exact entries
      if (e.generation == currentGen && e.depth > depth + 2 &&
          e.flag == TT_EXACT && flag != TT_EXACT) {
        if (move != MOVE_NONE)
          e.move = move;
        return;
      }
      replace = &e;
      break;
    }

    if (e.generation == 0) {
      replace = &e;
      break;
    }

    int rs = replacement_score(currentGen, e);
    if (rs > bestEvict) {
      bestEvict = rs;
      replace = &e;
    }
  }

  // Preserve existing move if we have no new one
  if (replace && move == MOVE_NONE && replace->key16 == k16 &&
      replace->key16b == k16b && replace->generation != 0)
    move = replace->move;

  replace->key16 = k16;
  replace->key16b = k16b;
  replace->move = move;
  replace->score = int16_t(score);
  replace->eval = int16_t(eval);
  replace->depth = int8_t(depth);
  replace->flag = flag;
  replace->generation = currentGen;
}

int TT::hashfull() const {
  size_t totalEntries = numClusters * 4;
  size_t step = std::max(totalEntries / 1000, size_t(1));
  size_t filled = 0, sample = 0;

  for (size_t flat = 0; flat < totalEntries; flat += step) {
    size_t c = flat / 4, e = flat % 4;
    if (c >= numClusters)
      break;
    if (table[c].entries[e].generation != 0 &&
        uint8_t(currentGen - table[c].entries[e].generation) <= 2)
      ++filled;
    ++sample;
  }

  return sample > 0 ? int(filled * 1000 / sample) : 0;
}

} // namespace Catalyst
