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

#include "bitboard.h"
#include "board.h"
#include "nnue.h"
#include "types.h"
#include "uci.h"
#include <iostream>
#include <pthread.h>

using namespace Catalyst;

static void *uci_thread(void *) {
  UCI uci;
  uci.loop();
  return nullptr;
}

int main() {
  init_bitboards();
  Zobrist::init();

  if (!NNUE::load("catalyst.nnue")) {
    std::cerr
        << "NNUE: WARNING — catalyst.bin not found or invalid.\n"
        << "NNUE: Place catalyst.bin in the same directory as the engine.\n"
        << "NNUE: Engine will still run but with degraded strength.\n";
    std::cerr.flush();
  }

  std::cerr << ENGINE_NAME << " " << ENGINE_VERSION << " by " << ENGINE_AUTHOR
            << "\n";
  std::cerr.flush();

  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 256 * 1024 * 1024);
  pthread_create(&thread, &attr, uci_thread, nullptr);
  pthread_attr_destroy(&attr);
  pthread_join(thread, nullptr);

  return 0;
}