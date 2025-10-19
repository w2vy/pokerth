#ifndef PLAYER_H
#define PLAYER_H
#include <cstdint>

struct FluxResult {
    std::string vin_address;
    int64_t paid_flux;
    int64_t pot_flux;
    std::string txid;
    int vout_index;
};


struct Player {
    uint32_t player_id;
    FluxResult entryFee;    // Player's entry fee
    std::string name = "";
    int startingStack;    // Money at the start of the hand
    int committed;        // What they have bet in this hand
    int winnings;         // what money they won
    int hand;
};

#endif  // PLAYER_H
