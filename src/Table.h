#ifndef TABLE_H
#define TABLE_H

#include "TableState.h"
#include "Player.h"
#include <vector>

//class TournamentDirector; // Forward declaration

enum TableType {
    Final = 1,
    Qualifier = 2
};

struct TableInfo {
    uint32_t game_id = 0;
    std::string name;
    std::string watcher;
    TableType type;
};

struct Table {
    TableState state = TableState::Idle;
    int num_players = 0;
    bool watch_started = false;
    bool left_table = false;
    TableInfo info;
    std::vector<Player> invite;
//    TournamentDirector* mytd = nullptr;
};

#endif  // TABLE_H
