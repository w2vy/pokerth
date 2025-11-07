#include <boost/asio.hpp>
#include <google/protobuf/message.h>
#include <boost/program_options.hpp>
#include <gsasl.h>
#include <memory>
#include <vector>
#include <array>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <third_party/protobuf/pokerth.pb.h>
#include <net/netpacket.h>
#include <tinyxml.h>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <curl/curl.h>

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

size_t CurlWriteToString(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* buffer = reinterpret_cast<std::string*>(userp);
    const size_t total = size * nmemb;
    buffer->append(static_cast<char*>(contents), total);
    return total;
}

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

PokerTHMessage tourneyCreateGame(std::string name, std::string password, NetGameInfo_NetGameType gameType, int nPlayers) {
    if (nPlayers < 3) nPlayers = 3; // Minimum of 3 players for now, until we get polling Invites
    // Send create game
    PokerTHMessage msg;
    msg.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_JoinNewGameMessage);
    JoinNewGameMessage *joinNew = msg.mutable_joinnewgamemessage();
    joinNew->set_autoleave(true);
    NetGameInfo *tmpGameInfo = joinNew->mutable_gameinfo();
    tmpGameInfo->set_netgametype(NetGameInfo_NetGameType_normalGame);
    tmpGameInfo->set_maxnumplayers(nPlayers);
#if 1
    if (nPlayers < 6) {
        tmpGameInfo->set_raiseintervalmode(NetGameInfo_RaiseIntervalMode_raiseOnMinutes);
        tmpGameInfo->set_endraisemode(NetGameInfo_EndRaiseMode_doubleBlinds);
        tmpGameInfo->set_raiseeveryminutes(10);
    } else {
        tmpGameInfo->set_raiseintervalmode(NetGameInfo_RaiseIntervalMode_raiseOnMinutes);
        tmpGameInfo->set_endraisemode(NetGameInfo_EndRaiseMode_doubleBlinds);
        tmpGameInfo->set_raiseeveryminutes(20);
    }
#else
    tmpGameInfo->set_raiseintervalmode(NetGameInfo_RaiseIntervalMode_raiseOnHandNum);
    tmpGameInfo->set_raiseeveryhands(5);
#endif
    tmpGameInfo->set_endraisemode(NetGameInfo_EndRaiseMode_keepLastBlind);
    tmpGameInfo->set_proposedguispeed(5);
    tmpGameInfo->set_delaybetweenhands(6);
    tmpGameInfo->set_playeractiontimeout(15);
    tmpGameInfo->set_endraisesmallblindvalue(0);
    tmpGameInfo->set_firstsmallblind(50);
    tmpGameInfo->set_startmoney(5000);
    //start_money = tmpGameInfo->startmoney();
    tmpGameInfo->set_gamename(name);
    tmpGameInfo->set_netgametype(gameType);
    if (!password.empty()) {
        joinNew->set_password(password);
    }
    return msg;
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
    std::string server_file;
    std::string server_url;
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
        ("server", po::value<std::string>(&server_file)->value_name("server.xml.z"), "local server description file")
        ("serverurl", po::value<std::string>(&server_url)->value_name("https://example/server.xml.z"), "remote server description URL")
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
    auto parse_server_profile = [&](const std::string& xml_content, const std::string& source_label) -> bool {
        TiXmlDocument doc;
        doc.Parse(xml_content.c_str());
        if (doc.Error()) {
            std::cerr << "Failed to parse server profile '" << source_label << "': " << doc.ErrorDesc() << std::endl;
            return false;
        }

        TiXmlHandle docHandle(&doc);
        const TiXmlElement* server_node = docHandle.FirstChild("ServerList").FirstChild("Server").ToElement();
        if (!server_node) {
            std::cerr << "Server profile '" << source_label << "' does not contain a <ServerList>/<Server> element." << std::endl;
            return false;
        }

        const TiXmlElement* ipv4 = server_node->FirstChildElement("IPv4Address");
        const TiXmlElement* protobuf_port = server_node->FirstChildElement("ProtobufPort");

        if (!ipv4 || !ipv4->Attribute("value") || !*ipv4->Attribute("value")) {
            std::cerr << "Server profile '" << source_label << "' is missing an IPv4Address value." << std::endl;
            return false;
        }
        if (!protobuf_port || !protobuf_port->Attribute("value") || !*protobuf_port->Attribute("value")) {
            std::cerr << "Server profile '" << source_label << "' is missing a ProtobufPort value." << std::endl;
            return false;
        }

        host = ipv4->Attribute("value");
        port = safe_stoi(protobuf_port->Attribute("value"), port);
        return true;
    };

    auto load_server_profile_from_stream = [&](std::istream& input, bool looks_compressed, const std::string& source_label) -> bool {
        std::ostringstream xml_data;
        try {
            if (looks_compressed) {
                boost::iostreams::filtering_streambuf<boost::iostreams::input> in;
                in.push(boost::iostreams::zlib_decompressor());
                in.push(input);
                boost::iostreams::copy(in, xml_data);
            } else {
                xml_data << input.rdbuf();
            }
        } catch (const boost::iostreams::zlib_error& e) {
            std::cerr << "Failed to decompress server profile '" << source_label << "': " << e.what() << std::endl;
            return false;
        } catch (const std::exception& e) {
            std::cerr << "Error reading server profile '" << source_label << "': " << e.what() << std::endl;
            return false;
        }
        return parse_server_profile(xml_data.str(), source_label);
    };

    auto looks_compressed = [](const std::string& path) {
        return path.size() >= 2 && path.compare(path.size() - 2, 2, ".z") == 0;
    };

    if (vm.count("server")) {
        std::ifstream server_stream(server_file, std::ios_base::in | std::ios_base::binary);
        if (!server_stream) {
            std::cerr << "Unable to open server file: " << server_file << std::endl;
            return -1;
        }
        if (!load_server_profile_from_stream(server_stream, looks_compressed(server_file), server_file)) {
            return -1;
        }
    }

    if (vm.count("serverurl")) {
        CURLcode curl_init = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (curl_init != CURLE_OK) {
            std::cerr << "curl initialization failed (" << curl_easy_strerror(curl_init) << ")" << std::endl;
            return -1;
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << "curl_easy_init failed" << std::endl;
            curl_global_cleanup();
            return -1;
        }

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, server_url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        curl_global_cleanup();

        if (res != CURLE_OK) {
            std::cerr << "Failed to download server profile from '" << server_url << "': " << curl_easy_strerror(res) << std::endl;
            return -1;
        }

        std::istringstream response_stream(response);
        if (!load_server_profile_from_stream(response_stream, looks_compressed(server_url), server_url)) {
            return -1;
        }
    }

    boost::asio::io_context io;

    // if (privKey.size() == 0) {
    //     std::cerr << "No private key defined --privKey missing!" << std::endl;
    //     return -1;
    // }
    //fluxsignStart();
    //fluxsignAddKey(privKey);

    tourneyManager.setStateChangeCallback(TourneyStateMachine);
    tourneyManager.createGame = tourneyCreateGame;

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
