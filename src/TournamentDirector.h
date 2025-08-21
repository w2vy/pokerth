#ifndef TOURNAMENTDIRECTOR_H
#define TOURNAMENTDIRECTOR_H

#include "transaction_fetcher.hpp"
#include <regex>
#include <functional>
#include <string>
#include <iomanip>
#include <sstream>
#include "PokerClient.h"

enum TourneyType {
    NoTourney = 0,
    OneRound = 1,
    TwoRounds = 2
};

class TableManager; // Forward references
class WatcherBot;
struct Table;

class TournamentDirector : public PokerClient {
    using FluxResultCallback = std::function<void(FluxResult)>;

public:
    TournamentDirector(boost::asio::io_context& io, const boost::program_options::variables_map& vm, TableManager tourneyManager);
    void validateFluxFee(uint32_t playerid, const std::string& txid, const Txn& txn, FluxResultCallback on_result);
    void handle_message(const std::vector<char>& data) override;
    void create_and_run_watcher_bot(boost::asio::io_context& io, const po::variables_map& vm, Table& wtable);
    void create_and_run_watcher_bot(Table& wtable);
    void run_watcher_bots();
    void endTourney(void);
    void startGame(uint32_t gameid);
    void leaveGame(uint32_t gameid);
    void inviteGame(uint32_t gameid, uint32_t player_id);
    void createGame(std::string name, std::string password, NetGameInfo_NetGameType gameType, int nPlayers);
    void createGame(std::string name, std::string password, NetGameInfo_NetGameType gameType);

private:
    TableManager tourneyManager_;
    std::unordered_map<int, std::shared_ptr<WatcherBot>> watchers_;
    std::vector<std::shared_ptr<WatcherBot>> bots_;
    boost::asio::ssl::context ssl_ctx_;
    std::string botadr = "t1KbvgXPrJ1RuCzBr5FjsPZk7XUrswu99zu";
    TourneyType activeTourney = NoTourney;
    bool registrationOpen = false;
    uint32_t maxRegistration;
    Table *activeTable;
    std::string prizeTxid = "";
    std::vector<std::pair<uint32_t, FluxResult>> registeredPlayers;
    int64_t prizeFlux = 0;
    int64_t entryFee = 0;
    std::string gameName = "";

    std::string player_txid = "";
    std::string player_adr = "";
    int player_vout;
    double player_pot;
    std::string player_script;

    std::string get_str(const boost::json::object& obj, const std::string& key) {
        if (obj.contains(key)) {
            if (obj.at(key).is_string())
                return obj.at(key).as_string().c_str();
        }
        return "";
    }

    int get_val(const boost::json::object& obj, const std::string& key) {
        if (obj.contains(key))
            if (obj.at(key).is_number()) {
                //std::cout << "get_val " << key << " " << obj.at(key) << std::endl;
                return obj.at(key).as_int64();
            }
        return -1;
    }

    int64_t get_val64(const boost::json::object& obj, const std::string& key) {
        if (obj.contains(key))
            if (obj.at(key).is_number()) {
                //std::cout << "get_val " << key << " " << obj.at(key) << std::endl;
                return obj.at(key).as_int64();
            }
        return -1;
    }
};
#endif