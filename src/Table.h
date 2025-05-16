#pragma once

#include <string>
#include <cstdint>

class TournamentDirector;  // Forward declaration

enum TableState {
    Idle = 0,
    Registration = 1,
    Playing = 2,
    Finished = 3
};

enum class GameTypes {
    Qualifier = 1,
    Final = 2,
    Solo = 3
};

struct Table {
    TournamentDirector* mytd = nullptr;
    TableState state = Idle;
    uint32_t game_id = 0;
    std::string name;
    std::string watcher;
    bool watch_started = false;
    bool left_table = false;
    std::string Winner;
    std::string RunnerUp;
    int num_players = 0;
    GameTypes type = GameTypes::Qualifier;
};

