#ifndef TABLE_H
#define TABLE_H

#include "TableState.h"
#include "Player.h"
#include "TournamentDirector.h"
#include <vector>

struct TableInfo {
    uint32_t game_id;
    std::string name;
    std::string watcher;
    bool watch_started;
    bool left_table;
};

struct TableInfoState {
    TableInfo info;
    std::vector<Player> invite;
    TableInfoState() : info(), invite() {}
};

struct Table {
    TableInfoState state;
    TournamentDirector* mytd = nullptr;
};

#endif  // TABLE_H
