#include <boost/asio.hpp>
#include <google/protobuf/message.h>
#include <boost/program_options.hpp>
#include <gsasl.h>
#include <memory>
#include <vector>
#include <array>
#include <unordered_map>
#include <iostream>
#include <third_party/protobuf/pokerth.pb.h>
#include <net/netpacket.h>

using boost::asio::ip::tcp;
namespace po = boost::program_options;

#include <array>
#include <string>
#include <optional>
#include <functional>

//#include "fluxsign.h"
#include "Table.h"
#include "TableManager.h"
#include "WatcherBot.h"
#include "TournamentDirector.h"

TableManager tourneyManager;

std::string printableSessionId(const std::string& sessionId) {
    std::ostringstream oss;
    for (unsigned char c : sessionId) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    return oss.str();
}

std::string printableErrorReason(ErrorMessage_ErrorReason cause) {
    switch (cause) {
        case ErrorMessage_ErrorReason_custReserved:
            return "custReserved";
        case ErrorMessage_ErrorReason_initVersionNotSupported:
            return "initVersionNotSupported";
        case ErrorMessage_ErrorReason_initServerFull:
            return "initServerFull";
        case ErrorMessage_ErrorReason_initAuthFailure:
            return "initAuthFailure";
        case ErrorMessage_ErrorReason_initPlayerNameInUse:
            return "initPlayerNameInUse";
        case ErrorMessage_ErrorReason_initInvalidPlayerName:
            return "initInvalidPlayerName";
        case ErrorMessage_ErrorReason_initServerMaintenance:
            return "initServerMaintenance";
        case ErrorMessage_ErrorReason_initBlocked:
            return "initBlocked";
        case ErrorMessage_ErrorReason_avatarTooLarge:
            return "avatarTooLarge";
        case ErrorMessage_ErrorReason_invalidPacket:
            return "invalidPacket";
        case ErrorMessage_ErrorReason_invalidState:
            return "invalidState";
        case ErrorMessage_ErrorReason_kickedFromServer:
            return "kickedFromServer";
        case ErrorMessage_ErrorReason_bannedFromServer:
            return "bannedFromServer";
        case ErrorMessage_ErrorReason_blockedByServer:
            return "blockedByServer";
        case ErrorMessage_ErrorReason_sessionTimeout:
            return "sessionTimeout";
        default:
            return "Unknown Reason " + std::to_string(cause);
    }
}

int safe_stoi(const std::string& str, int error_val) {
    try {
        return std::stoi(str);
    } catch (const std::invalid_argument& e) {
        return error_val;
    } catch (const std::out_of_range& e) {
        return error_val;
    }
}

#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>

std::string format_pot(uint64_t total_pot) {
    // Convert to double with 8 decimal places
    double value = static_cast<double>(total_pot) / 1e8;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8) << value;
    std::string str = oss.str();

    // Remove trailing zeros
    str.erase(str.find_last_not_of('0') + 1);

    // If it ends with '.', remove that too
    if (!str.empty() && str.back() == '.') {
        str.pop_back();
    }

    return str;
}

void TourneyStateMachine(TournamentDirector* mytd, Table& table, TableState newState) {
    std::cout << "Table" << table.info.name << " (" << table.info.game_id << ") " << table.info.type << "] changed state from " << stateName(table.state) << " to " << stateName(newState) << std::endl;
    if (table.info.type == Qualifier) {
        Table* finalTable = tourneyManager.pFindTableByType(Final);
        if (!finalTable && table.state == TableState::Playing) { // No final table created yet, create once we have a game Playing
            std::cout << "Created Final Table" << std::endl;
            Table& fTable = tourneyManager.addTable("Flux Final", "WatchBot");
            fTable.info.type = Final;
            finalTable = tourneyManager.pFindTableByType(Final);
        }
        if (newState == TableState::Playing) {
            // Refund fee offered by players who never joined - or maybe better to have an external cron that refunds stale txid in wallet
            for (auto& p : table.invite) {
                std::cout << "Refund " << p.entryFee.pot_flux << " to " << p.entryFee.vin_address << std::endl;
                uint32_t player_id = p.player_id;
                mytd->registeredPlayers.erase(
                    std::remove_if(
                        mytd->registeredPlayers.begin(),
                        mytd->registeredPlayers.end(),
                        [player_id](const auto& entry) {
                            return entry.first == player_id; // match by player_id
                        }),
                    mytd->registeredPlayers.end());
            }
            table.invite.clear(); // We're playing, no more invites
        }
        if (finalTable) std::cout << "Has Final Table" << std::endl;
        if (finalTable && newState == TableState::Finished) {
            // Append winner and runner up to finalTable->Invites
            finalTable->invite.insert(finalTable->invite.end(), table.winners.begin(), table.winners.end());
            std::cout << finalTable->info.name << " now has " << finalTable->invite.size() << " Players" << std::endl;
        }
        if (newState == TableState::Closed) {
            if (table.winners.size() == 2) {
                std::string shout = "Congratulations to the winners of " + table.info.name + ": #1 - " + table.winners.at(0).name + " #2 - " + table.winners.at(1).name;
                std::cout << shout << std::endl;
                mytd->sendLobby(shout);
            }
            table.winners.clear();
            tourneyManager.removeTable(table);
        }

        if (finalTable) {
            std::vector<Table> tables = tourneyManager.getTables();
            bool start_final = true;
            std::cout << "Maybe start final match " << std::endl;
            Table *t = tourneyManager.pFindTableByType(Qualifier);
            if (t) start_final = false;
            if (start_final) { // Time to Invite all players and play!
                Table& fTable = tourneyManager.findTableByType(Final);
                std::cout << "Start Final " << fTable.info.name << " Bot " << fTable.info.watcher << std::endl;
                fTable.info.max_players = fTable.invite.size();
                mytd->create_and_run_watcher_bot(fTable);
                mytd->run_watcher_bots();
            }
        }
    }
    if (table.info.type == Final || table.info.type == Solo) {
        if (newState == TableState::Playing) {
            if (table.info.type == Solo) {
                // Refund fee offered by players who never joined
                for (auto& p : table.invite) {
                    std::cout << "Refund " << p.entryFee.pot_flux << " to " << p.entryFee.vin_address << std::endl;
                    uint32_t player_id = p.player_id;
                    mytd->registeredPlayers.erase(
                        std::remove_if(
                            mytd->registeredPlayers.begin(),
                            mytd->registeredPlayers.end(),
                            [player_id](const auto& entry) {
                                return entry.first == player_id; // match by player_id
                            }),
                        mytd->registeredPlayers.end());
                }
            }
            table.invite.clear(); // We're playing, no more invites.
        }
        if (newState == TableState::Closed) {
            uint64_t total_pot = 0;
            std::vector<std::string> txids = {};
            for (auto& [pid, p] : mytd->registeredPlayers) {
                total_pot += p.pot_flux;
                txids.push_back(p.txid);
            }
            std::string totalpot = "";
            if (total_pot > 0) {
                totalpot = " " + format_pot(total_pot) + " Flux";
                std::string shout = "The total pot is" + totalpot;
                std::cout << shout << std::endl;
                mytd->sendLobby(shout);
            }
            if (table.info.type == Solo) {
                std::string shout = "Congratulations to the winner of " + table.info.name + ": " + table.winners.at(0).name +totalpot;
                std::cout << shout << std::endl;
                std::cout << "Pay Winner " << table.winners.at(0).entryFee.vin_address << " " << format_pot(total_pot) << " Flux" << std::endl;
                mytd->sendLobby(shout);
            }
            if (table.info.type == Final) {
                std::string first_pot = "";
                std::string second_pot = "";
                if (total_pot > 0) {
                    uint64_t second = (total_pot*40)/100;
                    uint64_t first = total_pot - second;
                    first_pot = " " + format_pot(first) + " Flux";
                    second_pot = " " + format_pot(second) + " Flux";
                }
                std::string shout = "Congratulations to the winners of " + table.info.name + ": #1 - " + table.winners.at(0).name + first_pot + " #2 - " + table.winners.at(1).name + second_pot;
                std::cout << shout << std::endl;
                std::cout << "Pay Winner 1 " << table.winners.at(0).entryFee.vin_address << first_pot << std::endl;
                std::cout << "Pay Winner 2 " << table.winners.at(1).entryFee.vin_address << second_pot << std::endl;
                mytd->sendLobby(shout);
            }
            // Send winner(s) prize here?
            table.winners.clear();
            tourneyManager.removeTable(table);
            mytd->endTourney();
        }
    }
//    if (table.info.type == Solo) {
//    }
}

int main(int argc, char* argv[]) {
    std::string host;
    int port;
    std::string username, password;
    std::string watcher_password;
    std::string server_password;
    std::string game_name;
    std::string privKey;

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "help message")
        ("host", po::value<std::string>(&host)->default_value("127.0.0.1"), "server host")
        ("port", po::value<int>(&port)->default_value(7234), "server port")
        ("username", po::value<std::string>(&username)->default_value("TD"), "username")
        ("password", po::value<std::string>(&password)->default_value(""), "user password")
        ("privKey", po::value<std::string>(&privKey)->default_value(""), "WifKey for bot wallet")
        ("watcher-password", po::value<std::string>(&watcher_password)->default_value(""), "watcher bot password")
        ("server-password", po::value<std::string>(&server_password)->default_value(""), "server password")
        ("game-name", po::value<std::string>(&game_name)->default_value(""), "Game Name to watch (used by watcher bots)");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 0;
    }

    boost::asio::io_context io;

    // if (privKey.size() == 0) {
    //     std::cerr << "No private key defined --privKey missing!" << std::endl;
    //     return -1;
    // }
    //fluxsignStart();
    //fluxsignAddKey(privKey);

    tourneyManager.setStateChangeCallback(TourneyStateMachine);

    auto td = std::make_shared<TournamentDirector>(io, vm, tourneyManager);
    tcp::resolver resolver(io);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    boost::asio::async_connect(td->socket(), endpoints,
    [&io, td, username, password, server_password](boost::system::error_code ec, const tcp::endpoint&) {
        if (!ec) {
            std::cout << "TD connected successfully." << std::endl;
            td->start();
        } else {
            std::cerr << "TD connection failed: " << ec.message() << std::endl;
        }
    });

    io.run();
    //fluxsignStop();
    return 0;
}
