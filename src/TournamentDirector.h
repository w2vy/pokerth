#ifndef TOURNAMENTDIRECTOR_H
#define TOURNAMENTDIRECTOR_H

#include "transaction_fetcher.hpp"
#include <regex>
#include <functional>
#include <string>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <deque>
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
    TournamentDirector(boost::asio::io_context& io, const boost::program_options::variables_map& vm, TableManager& tourneyManager);
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
    std::vector<std::pair<uint32_t, FluxResult>> registeredPlayers;
    FluxResult* findFluxResult(uint32_t player_id);

private:
    TableManager& tourneyManager_;
    std::unordered_map<int, std::shared_ptr<WatcherBot>> watchers_;
    std::deque<std::shared_ptr<WatcherBot>> bots_;
    std::mutex bots_mutex_;
    boost::asio::ssl::context ssl_ctx_;
    TourneyType activeTourney = NoTourney;
    bool registrationOpen = false;
    int MaxTablePlayers = 10;
    uint32_t maxRegistration;
    Table *activeTable;
    std::string prizeTxid = "";
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

    int64_t get_val64_from_fixed8(const boost::json::object& obj, const std::string& key) {
        if (!obj.contains(key))
            return -1;

        const auto& val = obj.at(key);

        // Case 1: JSON number (e.g., 10.12345678)
        if (val.is_number()) {
            double d = val.as_double();
            // Round to nearest satoshi
            return static_cast<int64_t>(std::llround(d * 100000000.0));
        }

        // Case 2: JSON string (e.g., "10.12345678")
        if (val.is_string()) {
            std::string s = val.as_string().c_str();

            // Strip whitespace
            s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
            if (s.empty()) return -1;

            // Optional sign
            bool neg = false;
            if (s[0] == '-') { neg = true; s.erase(0, 1); }

            // Split into whole + fraction
            size_t dot = s.find('.');
            std::string whole = (dot == std::string::npos) ? s : s.substr(0, dot);
            std::string frac  = (dot == std::string::npos) ? "" : s.substr(dot + 1);

            // Pad or truncate fractional part to exactly 8 digits
            if (frac.size() < 8) frac.append(8 - frac.size(), '0');
            else if (frac.size() > 8) frac = frac.substr(0, 8);

            std::string combined = whole + frac;
            if (combined.empty()) return -1;

            try {
                int64_t val = std::stoll(combined);
                return neg ? -val : val;
            } catch (...) {
                return -1;
            }
        }

        return -1;
    }

};
#endif