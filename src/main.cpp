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
#include <string>

using namespace Catalyst;

static void *uci_thread(void *)
{
    UCI uci;
    uci.loop();
    return nullptr;
}

int main(int argc, char *argv[])
{
    init_bitboards();
    Zobrist::init();
    NNUE::load("catalyst.nnue");

    std::cerr << ENGINE_NAME << " " << ENGINE_VERSION << " by " << ENGINE_AUTHOR << "\n";
    std::cerr.flush();

    if (argc > 1)
    {
        std::string command;
        for (int i = 1; i < argc; ++i)
            command += (i > 1 ? " " : "") + std::string(argv[i]);

        UCI uci;
        uci.run_command_line(command);
        return 0;
    }

    pthread_t      thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024 * 1024);
    pthread_create(&thread, &attr, uci_thread, nullptr);
    pthread_attr_destroy(&attr);
    pthread_join(thread, nullptr);

    return 0;
}