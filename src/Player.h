#ifndef PLAYER_H
#define PLAYER_H
#include <cstdint>

struct Player {
    uint32_t player_id;
    std::string txid;     // Player's entry fee
    std::string name;
    int startingStack;    // Money at the start of the hand
    int committed;        // What they have bet in this hand
    int winnings;         // what money they won
    int hand;
};

#endif  // PLAYER_H
