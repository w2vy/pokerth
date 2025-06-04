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

#include "Table.h"
#include "TableManager.h"
#include "TournamentDirector.h"
#include "WatcherBot.h"

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

void TourneyStateMachine(Table *table, TableState oldState, TableState newState) {
    std::cout << "Table[" << table->name << ", " << table->type << "] changed state from " << stateName(oldState) << " to " << stateName(newState) << std::endl;
    if (table->type == Qualifier) {
        std::optional<size_t> table_num = TourneyManager.findTableByType(Final);
        Table *finalTable = TourneyManager.getTable(table_num);
        if (!finalTable) { // No final table created yet
            table_num = TourneyManager.allocateTable(table->mytd, "Final");
            finalTable = TourneyManager.getTable(table_num);
            if (finalTable) {
                size_t tnum = (*table_num)+1;
                finalTable->name = "Flux Final " + std::to_string(tnum);
                finalTable->watcher = "WatchBot" + std::to_string(tnum);
                finalTable->type = Final;
            } else {
                std::cout << "Create Final Game Table failed" << std::endl;
                return;
            }
        }
        if (newState == Playing) {
            table->Invite.clear(); // We're playing, no more invites
        }
        if (newState == Finished) {
            // Append winner and runner up to finalTable->Invites
            finalTable->Invite.insert(finalTable->Invite.end(), table->Invite.begin(), table->Invite.end());
            std::cout << finalTable->name << " now has " << finalTable->Invite.size() << " Players" << std::endl;
            // Check to see if all Qualifier games are finished and then start Final
            std::vector<Table*> tables = TourneyManager.activeTables();
            bool create_final = true;
            for (const auto& table : tables) {
                // Read-only access
                if (table->type != Qualifier) continue;
                if (table->state <= Playing) {
                    create_final = false;
                    break; // Still playing Qualifiers
                }
            }
            if (create_final) {
                // create game, invite players, etc (InvitePlayerToGameMessage)
                std::cout << "Create Final Game" << std::endl;
                finalTable->mytd->createGame(finalTable->name, "", NetGameInfo_NetGameType_inviteOnlyGame);
            }
        }
        if (newState == Closed) {
            if (table->Invite.size() == 2) {
                std::string shout = "Congratulations to the winners of " + table->name + ": #1 - " + table->Invite.at(0).name + " #2 - " + table->Invite.at(1).name;
                std::cout << shout << std::endl;
                table->mytd->sendLobby(shout);
            }
            table->Invite.clear();
            TourneyManager.freeTable(table);

            std::vector<Table*> tables = TourneyManager.activeTables();
            bool start_final = true;
            std::cout << "Maybe start final match " << std::endl;
            for (const auto& table : tables) {
                // Start final game when all qualifiers are closed
                if (table->type == Qualifier) {
                    start_final = false;
                    break;
                }
            }
            if (start_final) { // Time to Invite all players and play!
                std::cout << "Start Final " << finalTable->name << std::endl;
                for (const Player& player : finalTable->Invite) {
                    std::cout << "Invite " << player.name << std::endl;
                    finalTable->mytd->inviteGame(finalTable->game_id, player.player_id);
                }
            }
        }
    }
    if (table->type == Final) {
        if (newState == Playing) {
            table->Invite.clear(); // We're playing, no more invites
        }
        if (newState == Closed) {
            if (table->Invite.size() == 2) {
                std::string shout = "Congratulations to the winners of " + table->name + ": #1 - " + table->Invite.at(0).name + " #2 - " + table->Invite.at(1).name;
                std::cout << shout << std::endl;
                table->mytd->sendLobby(shout);
            }
            table->Invite.clear();
            TourneyManager.freeTable(table);
        }
    }
}

int main(int argc, char* argv[]) {
    std::string host;
    int port;
    std::string username, password;
    std::string watcher_password;
    std::string server_password;
    std::string game_name;

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("host", po::value<std::string>(&host)->default_value("127.0.0.1"), "server host")
        ("port", po::value<int>(&port)->default_value(7234), "server port")
        ("username", po::value<std::string>(&username)->default_value("TD"), "username")
        ("password", po::value<std::string>(&password)->default_value(""), "user password")
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

    TourneyManager.setStateChangeCallback(TourneyStateMachine);

    auto td = std::make_shared<TournamentDirector>(io, vm);
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
    return 0;
}
