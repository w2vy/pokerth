#ifndef WATCHERBOT_H
#define WATCHERBOT_H

#include "PokerClient.h"
#include "Table.h"
#include "TableManager.h"
#include "TournamentDirector.h"

#include <optional>

class WatcherBot : public PokerClient {
    
public:
    WatcherBot(boost::asio::io_context& io, const po::variables_map& vm, Table& table, TournamentDirector* td, TableManager& tourneyManager);
    void start(void) override;
    void handle_message(const std::vector<char>& data) override;
    std::string getNetPlayerState(uint32_t state);
    std::string getNetGameState(NetGameState state);
    void addPlayer(uint32_t player_id);
    std::string getPlayerName(uint32_t player_id);
    uint32_t getPlayerByName(std::string name);
    void setPlayerName(uint32_t player_id, std::string name);
    void setPlayerStack(uint32_t player_id, int stackLeft);
    void setPlayerStackWon(uint32_t player_id, int winnings);
    void setPlayersStartingStack(void);
    void setPlayerHand(uint32_t player_id, int hand);
    void startGame(uint32_t gameid);
    void leaveGame(uint32_t gameid);
    void watchGame(uint32_t gameid);
    void inviteGame(uint32_t gameid, uint32_t player_id);
    void createGame(std::string name, std::string password, NetGameInfo_NetGameType gameType, int nPlayers);

private:
    boost::asio::io_context& io_;
    const po::variables_map& vm_;
    Table& watchTable_;
    TournamentDirector* mytd_;
    TableManager& tourneyManager_;
    uint32_t watcherBotID;
    std::unordered_map<int, Player> Players;
    std::int32_t start_money;

    struct Pot {
        int amount;
        std::vector<Player*> eligible_players;
    };
    
    // ------------------ Functions ------------------
    void invitePlayers() {
        std::cout << "invitePlayers to game " << watchTable_.info.name << " (" << watchTable_.info.game_id << ")" << std::endl;

        std::thread([this]() {
            std::cout << "Invite " << watchTable_.invite.size() << " players one at a time" << std::endl;
            while (watchTable_.state == TableState::Inviting && watchTable_.invite.size() > 0) {
                for (auto& p : watchTable_.invite) {
                    inviteGame(watchTable_.info.game_id, p.player_id);
                    sleep(2);
                }
                sleep(15);
            }
            std::cout << "Invite " << watchTable_.invite.size() << " Done" << std::endl;
        }).detach();
    }

    // Returns the player with the best hand in the group
    Player* GetBestPlayer(const std::vector<Player*>& players) {
        return *std::max_element(players.begin(), players.end(),
            [](Player* a, Player* b) {
                return a->hand < b->hand;
            });
    }
    
    std::vector<Pot> BuildPotsFromCommits(std::vector<Player*>& player_list) {
        // 1) filter out any players who folded (committed == 0)
        std::vector<Player*> inHand;
        for (auto *p : player_list) {
            if (p->committed > 0) inHand.push_back(p);
        }
    
        // 2) sort ascending by committed amount
        std::sort(inHand.begin(), inHand.end(),
                  [](Player* a, Player* b) { return a->committed < b->committed; });
    
        std::vector<Pot> pots;
        int prevLevel = 0;
    
        while (!inHand.empty()) {
            int level = inHand.front()->committed;
            int delta = level - prevLevel;
    
            Pot pot;
            pot.amount = delta * static_cast<int>(inHand.size());
            pot.eligible_players = inHand;
            pots.push_back(pot);
    
            // remove those who only committed up to this level
            inHand.erase(
                std::remove_if(inHand.begin(), inHand.end(),
                               [level](Player* p){ return p->committed == level; }),
                inHand.end()
            );
    
            prevLevel = level;
        }
    
        return pots;
    }
    
    // Determines first and second place
    std::pair<Player*, Player*> DetermineTopTwoPlayers(std::vector<Player*>& players,
                                                       std::vector<Pot>& pots)
    {
        if (players.size() < 2) {
            std::cout << "Not enough players in players list, found " << players.size() << std::endl;
            return {nullptr, nullptr};
        }
        if (players.size() == 2) {
            Player* first = GetBestPlayer(players);
            Player* second = (first == players[0]) ? players[1] : players[0];
            return {first, second};
        }
    
        std::sort(pots.begin(), pots.end(), [](const Pot& a, const Pot& b) {
            return a.amount > b.amount;
        });
    
        Player* first = GetBestPlayer(pots[0].eligible_players);
    
        for (size_t i = 1; i < pots.size(); ++i) {
            if (std::find(pots[i].eligible_players.begin(), pots[i].eligible_players.end(), first)
                == pots[i].eligible_players.end()) {
                Player* second = GetBestPlayer(pots[i].eligible_players);
                return {first, second};
            }
        }
    
        std::vector<Player*> remaining;
        for (Player* p : players) {
            if (p != first) remaining.push_back(p);
        }
    
        Player* second = GetBestPlayer(remaining);
        return {first, second};
    }

    std::pair<Player*, Player*> find_winners(void) {
        std::vector<Player*> players_list;
        players_list.reserve(Players.size());   // avoid reallocations

        for (auto& [id, player] : Players) {
            players_list.push_back(&player);
        }

        // 1) build pots from their committed amounts
        auto pots = BuildPotsFromCommits(players_list);

        // 2) determine 1st & 2nd
        auto [first, second] = DetermineTopTwoPlayers(players_list, pots);

        if (first) std::cout << "1st: "  << first->name;
        if (second) std::cout << " 2nd: "  << second->name;
        if (!first && !second) std::cout << "No valid players found??";
        std::cout << std::endl;
        return {first, second};
    }
};
#endif