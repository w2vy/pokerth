#pragma once

#include <string>
#include <cstdint>

class TournamentDirector;  // Forward declaration

enum TableState {
    Idle = 0,
    Connecting = 1,
    Registration = 2,
    Playing = 3,
    Finished = 4,
    Closed = 5
};

const char* stateName(TableState state) {
    switch (state) {
        case Idle:         return "Idle";
        case Connecting:   return "Connecting";
        case Registration: return "Registration";
        case Playing:      return "Playing";
        case Finished:     return "Finished";
        case Closed:       return "Closed";
        default:           return "Unknown";
    }
}

enum GameTypes {
    Qualifier = 1,
    Final = 2,
    Solo = 3
};

struct Player {
    uint32_t player_id;
    std::string txid;     // Player's entry fee
    std::string name;
    int startingStack;    // Money at the start of the hand
    int committed;        // What they have bet in this hand
    int winnings;         // what money they won
    int hand;
};

struct Table {
    TournamentDirector* mytd = nullptr;
    //WatchBot* watchBot = nullptr;
    TableState state = Idle;
    uint32_t game_id = 0;
    std::string name;
    std::string watcher;
    bool watch_started = false;
    bool left_table = false;
    std::vector<Player> Invite;
    int num_players = 0;
    GameTypes type = Qualifier;
};

