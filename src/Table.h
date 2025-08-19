#ifndef TABLE_H
#define TABLE_H

#include "TableState.h"
#include "Player.h"
#include <vector>

enum TableType {
    Final = 1,
    Qualifier = 2
};

struct TableInfo {
    uint32_t game_id = 0;
    std::string name = "";
    std::string watcher = "";
    TableType type = Qualifier;
    int max_players = 10;
};

struct Table {
    TableState state = TableState::Idle;
    int num_players = 0;
    bool watch_started = false;
    bool left_table = false;
    TableInfo info;
    std::vector<Player> invite;
};

#endif  // TABLE_H
