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

#pragma once

#include <cstdint>
#include <string>

namespace Catalyst {
namespace Datagen {

    struct DatagenConfig {
        std::string output_path      = "catalyst_data.txt";
        std::string book_path        = "";
        int         threads          = 4;
        int         games            = 0;
        uint64_t    soft_nodes       = 5000;
        uint64_t    hard_nodes       = 20000;
        int         verify_depth     = 10;
        int         verify_limit     = 500;
        int         random_plies_min = 8;
        int         random_plies_max = 9;
    };

    DatagenConfig parse_config(const std::string &args);
    void          run(const DatagenConfig &cfg);

}  // namespace Datagen
}  // namespace Catalyst