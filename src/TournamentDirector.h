#pragma once

#include "PokerClient.h"
#include "WatcherBot.h"
#include "transaction_fetcher.hpp"
#include <regex>
#include <fmt/core.h>

#include <string>
#include <iomanip>
#include <sstream>

std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : value) {
        // Keep alphanumeric and some safe characters
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            // Percent-encode everything else
            escaped << '%' << std::uppercase << std::setw(2) << int(c);
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

class TournamentDirector : public PokerClient {
public:
    TournamentDirector(boost::asio::io_context& io, const boost::program_options::variables_map& vm)
        : PokerClient(io, vm),
          ssl_ctx_(boost::asio::ssl::context::sslv23_client) {
        ssl_ctx_.set_verify_mode(boost::asio::ssl::verify_peer);
        ssl_ctx_.set_default_verify_paths();  // Or load specific CA bundle if needed
    }

    void validateFluxFee(uint32_t playerid, std::string txid, Txn txn) {
        std::string botadr = "t1KbvgXPrJ1RuCzBr5FjsPZk7XUrswu99zu"; // Retirement
        std::cout << "Player " << playerid << " Validate " << txid << std::endl;
        if (txn.raw.is_object()) {
            std::string msg;
            auto& obj = txn.raw.as_object();
            if (obj.contains("data")) {
                if (obj.at("data").is_object()) {
                    auto data = obj.at("data").as_object();
                    msg = "Undefined status";
                    if (get_str(obj, "status") == "error") {
                        msg = "Failed: " + get_str(data, "name") + " " + std::to_string(get_val(data, "code")) + ": " + get_str(data, "message");
                    }
                    if (get_str(obj, "status") == "success") {
                        const auto confirmations = get_val(data, "confirmations");
                        std::string short_txid = txid.substr(0, 6) + "..." + txid.substr(txid.size() - 6);
                        msg = "Success: " + std::to_string(confirmations) + " " + get_str(data, "txid");
                        sendTell(playerid, msg);
                        std::string vin_addr; // vin.address
                        int64_t vin_value, vout_value; // Player vin / vout; fee paid is vin - vout
                        int vout_index; // vout number when we spend it
                        int64_t pot_fee; // May have gas fee deducted, capture the amount we can spend
                        const auto vin_entry = data["vin"].as_array();
                        const auto vout_array = data["vout"].as_array();
                        if (confirmations > 2) {
                            if (vin_entry.size() == 1 && vout_array.size() == 2) {
                                const auto& vin_obj = vin_entry.at(0).as_object();
                                vin_value = get_val64(vin_obj, "valueSat");
                                vin_addr = get_str(vin_obj, "address");
                                int vout = 0;
                                for (const auto& vout_entry : vout_array) {
                                    const auto& vout_obj = vout_entry.as_object();
                                    const auto value = get_val64(vout_obj, "valueSat");
                                    const auto script = vout_obj.at("scriptPubKey");
                                    const auto& addresses = vout_obj.at("scriptPubKey").at("addresses").as_array();
                                    if (addresses.size() == 1) {
                                        std::string addr = addresses[0].as_string().c_str();
                                        if (addr == vin_addr) {
                                            vout_value = value;
                                            player_script = script.at("hex").as_string();
                                        }
                                        if (addr == botadr) {
                                            vout_index = vout;
                                            pot_fee = value;
                                        }
                                    }
                                    vout++;
                                }
                                const auto fee_paid = vin_value - vout_value;
                                const double paid = static_cast<double>(fee_paid)/1e8;
                                const double pot = static_cast<double>(pot_fee)/1e8;
                                player_adr = vin_addr;
                                player_pot = pot;
                                player_txid = txid;
                                player_vout = vout_index;
                                std::cout << "Player " + vin_addr + " vout " << vout_index << " txid " + txid << std::endl;
                                msg = fmt::format("Accepted! Confirmations {} Paid: {:.8f} Flux Add to Pot: {:.8f} Flux, vout {}", confirmations, paid, pot, vout_index);
                            } else {
                                msg = "Unexpected transaction format # vin " + std::to_string(vin_entry.size()) + " # vout " + std::to_string(vout_array.size()) + " for " + std::to_string(confirmations) + ") " + short_txid;
                            }
                        } else {
                            msg = "Not confirmed (" + std::to_string(confirmations) + ") " + short_txid;
                        }
                        sendTell(playerid, msg);
                    }
                } else {
                    std::cout << "Data not an object in txn" << std::endl;
                }
            } else {
                std::cout << "Data not found in txn" << std::endl;
            }
        } else {
            std::cout << "Txn is not an object!" << std::endl;
        }
    }

    void handle_message(const std::vector<char>& data) override {
        PokerTHMessage msg;
        if (!msg.ParseFromArray(data.data(), data.size())) {
            std::cerr << "Failed to parse PokerTHMessage" << std::endl;
            return;
        }

        switch (msg.messagetype()) {
            case PokerTHMessage_PokerTHMessageType_Type_AnnounceMessage: {
                const auto& ann = msg.announcemessage();
            
                const auto& protoVer = ann.protocolversion();
                const auto& latestVer = ann.latestgameversion();
                uint32_t betaRev = ann.latestbetarevision();
                auto serverType = ann.servertype();
                uint32_t numPlayers = ann.numplayersonserver();
            
                std::cout << "Received AnnounceMessage:\n"
                        << "  Protocol Version: " << protoVer.majorversion() << "." << protoVer.minorversion() << "\n"
                        << "  Latest Game Version: " << latestVer.majorversion() << "." << latestVer.minorversion() << "\n"
                        << "  Latest Beta Revision: " << betaRev << "\n"
                        << "  Server Type: " << serverType << "\n"
                        << "  Number of Players on Server: " << numPlayers << std::endl;
            
                const std::string& username = vm_["username"].as<std::string>();
                const std::string& password = vm_["watcher-password"].as<std::string>();
                const std::string& server_password = vm_["server-password"].as<std::string>();
                            
                server_auth(username, password, server_password);
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_InitAckMessage: {
                const auto& ack = msg.initackmessage();
                const std::string sessid = printableSessionId(ack.yoursessionid());
                std::cout << "Received InitAckMessage:\n"
                          << "  Session ID: " << sessid << "\n"
                          << "  Player ID: " << ack.yourplayerid() << std::endl;
                if (ack.has_youravatarhash()) {
                    std::cout << "  Avatar Hash: " << ack.youravatarhash() << std::endl;
                }
                if (ack.has_rejoingameid()) {
                    std::cout << "  Rejoin Game ID: " << ack.rejoingameid() << std::endl;
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_ErrorMessage: {
                ErrorMessage_ErrorReason cause = msg.errormessage().errorreason();
                std::cerr << "TD Received error, reason " << printableErrorReason(cause) << std::endl;
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_AuthServerChallengeMessage: {
                const std::string& challenge = msg.authserverchallengemessage().serverchallenge();

                char* tmpOut;
                size_t tmpOutSize;
                std::string response;

                if (gsasl_step(authSession_, challenge.c_str(), challenge.size(), &tmpOut, &tmpOutSize) == GSASL_NEEDS_MORE) {
                    response.assign(tmpOut, tmpOutSize);
                    gsasl_free(tmpOut);

                    PokerTHMessage reply;
                    reply.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_AuthClientResponseMessage);
                    reply.mutable_authclientresponsemessage()->set_clientresponse(response);

                    send_message(reply);
                } else {
                    std::cerr << "GSASL step 2 failed" << std::endl;
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_AuthServerVerificationMessage: {
                std::cout << "Authentication complete!" << std::endl;
                gsasl_finish(authSession_);
                gsasl_done(authCtx_);
                authSession_ = NULL;
                authCtx_ = NULL;
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_JoinGameAckMessage: {
                const JoinGameAckMessage ack = msg.joingameackmessage();
                bool speculate = false;
                if (ack.has_spectateonly()) {
                    speculate = ack.spectateonly();
                }
                if (ack.has_gameid() && ack.areyougameadmin() && !speculate) {
                    int gameid = ack.gameid();
                    std::string game_name = ack.gameinfo().gamename();
                    std::cout << "JoinGame Ack TD " << gameid << " Table " << game_name << std::endl;
                    std::optional<size_t> table_num = TourneyManager.findTableByName(game_name);
                    Table *table = TourneyManager.getTable(table_num);
                    if (table) { // We have a table with that name
                        std::cout << "JoinGameAck TD " << game_name << " id " << gameid << " was " << table->game_id << std::endl;
                        if (table->game_id == 0) { // Game ID not set
                            std::cout << "Game ID set" << std::endl;
                            table->game_id = gameid;
                            myGame_id = gameid; // Just for testing TD commands
                        }
                    }
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_JoinGameFailedMessage: {
                const JoinGameFailedMessage failed = msg.joingamefailedmessage();
                uint32_t cause = failed.joingamefailurereason();
                if (failed.has_gameid()) {
                    std::cout << "Join Game " << failed.gameid() << " " << cause << " Terminated" << std::endl;
                } else {
                    std::cout << "Join Game " << failed.gameid() << " " << cause << " Create Game Failed?" << std::endl;
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_GameListNewMessage: {
                std::cout << "Received GameListNewMessage" << std::endl;
                const GameListNewMessage& newGame = msg.gamelistnewmessage();
                int gameid = newGame.has_gameid() ? newGame.gameid() : 0;
                std::string gname = newGame.gameinfo().gamename();
                std::cout << "Game " << gname << " (" << gameid << ") just started!" << std::endl;
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_EndOfGameMessage: {
                const EndOfGameMessage endGame = msg.endofgamemessage();
                if (endGame.has_gameid()) {
                    std::cout << "Game " << endGame.gameid() << " has ended" << std::endl;
                } else {
                    std::cout << "A Game has ended" << std::endl;
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_GameListPlayerJoinedMessage: {
                const GameListPlayerJoinedMessage joined = msg.gamelistplayerjoinedmessage();
                std::optional<size_t> table_num = TourneyManager.findTableByGameId(joined.gameid());
                Table *table = TourneyManager.getTable(table_num);
                if (table) { // This is a game we care about
                    table->num_players++;
                    std::cout << "TD Player for " << table->name << " (" << table->game_id << ") " + std::to_string(joined.playerid()) + ") has joined " << std::to_string(table->num_players) << " Players" << std::endl;
                    if (!table->watch_started) {
                        table->watch_started = true;
                        create_and_run_watcher_bot(io_context_, vm_, table);
                    }
                    if (table->num_players > 2 && !table->left_table) { // Should be 5
                        table->left_table = true;
                        leaveGame(table->game_id);
                    }
                }
                break;
            }
            // case PokerTHMessage_PokerTHMessageType_Type_GameListPlayerLeftMessage: {
            //     const GameListPlayerLeftMessage left = msg.gamelistplayerleftmessage();
            //     std::optional<size_t> table_num = TourneyManager.findTableByGameId(left.gameid());
            //     Table *table = TourneyManager.getTable(table_num);
            //     if (table) { // This is a game we care about
            //         table->num_players--;
            //         std::cout << "TD Player for " << table->name << " " << table->game_id << " has left " << std::to_string(table->num_players) << " Players" << std::endl;
            //     }
            //     break;
            // }
            case PokerTHMessage_PokerTHMessageType_Type_GameListUpdateMessage: {
                const GameListUpdateMessage update = msg.gamelistupdatemessage();
                uint32_t gameid = update.gameid();
                std::optional<size_t> table_num = TourneyManager.findTableByGameId(gameid);
                std::cout << "Game Update " << gameid << std::endl;
                Table *table = TourneyManager.getTable(table_num);
                if (table) { // This is a game we care about
                    std::cout << table->name << " Game Update " << update.gamemode() << std::endl;
                    if (update.gamemode() == netGameClosed) TourneyManager.updateState(table, Closed);
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_TimeoutWarningMessage: {
                PokerTHMessage reply;
                reply.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_ResetTimeoutMessage);
                send_message(reply);
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_ChatMessage: {
                ChatMessage chat = msg.chatmessage();
                std::cout << "TD Received Chat: " << chat.chattext() << std::endl;
                if (chat.chattype() == ChatMessage_ChatType_chatTypePrivate) {
                    uint32_t playerid = 0;
                    if (chat.has_playerid()) playerid = chat.playerid();

                    if (chat.chattext() == "tables") {
                        std::vector<Table*> tables = TourneyManager.activeTables();
                        for (const auto& table : tables) {
                            std::cout << stateName(table->state) << " " << table->game_id << " " + table->name + " " << table->num_players << " " << table->Invite.size() << std::endl;
                        }
                    }
                    if (chat.chattext() == "table") {
                        std::cout << "Create Game MyTest" << std::endl;
                        std::optional<size_t>table_num = TourneyManager.allocateTable(this, "MyTest");
                        Table *myTable = TourneyManager.getTable(table_num);
                        if (myTable) {
                            size_t tnum = (*table_num)+1;
                            myTable->name = "Flux Table " + std::to_string(tnum);
                            myTable->watcher = "WatchBot" + std::to_string(tnum);
                            createGame(myTable->name, "", NetGameInfo_NetGameType_registeredOnlyGame);
                        } else {
                            std::cout << "Create Game Table failed" << std::endl;
                        }
                    }
                    if (chat.chattext().compare(0, 6, "start ") == 0) {
                        size_t tbl = safe_stoi(chat.chattext().substr(6,chat.chattext().size()-6), -1);
                        std::cout << "Start Table" << std::endl;
                        if (tbl >= 1 && tbl <= 10) {
                            Table *t = TourneyManager.getTable(tbl-1);
                            if (t) {
                                std::cout << "Start game " << t->game_id << " " << t->name << std::endl;
                                startGame(t->game_id);
                            } else {
                                std::cout << "Start Game Table " << tbl << " not found" << std::endl;
                            }
                        } else {
                            std::cout << "Invalid start command: should be 'start #' where # is 1-10, received " + chat.chattext() << std::endl;
                        }
                    }
                    if (chat.chattext().compare(0, 6, "leave ") == 0) {
                        int tbl = safe_stoi(chat.chattext().substr(6,chat.chattext().size()-6), -1);
                        if (tbl >= 1 && tbl <= 10) {
                            Table *t = TourneyManager.getTable(tbl-1);
                            if (t) {
                                leaveGame(t->game_id);
                            } else {
                                std::cout << "Start Game Table " << tbl << " not found" << std::endl;
                            }
                        } else {
                            std::cout << "Invalid start command: should be 'start #' where # is 1-10, received " + chat.chattext() << std::endl;
                        }
                    }
                    if (chat.chattext().compare(0, 7, "invite ") == 0) {
                        int player_id = safe_stoi(chat.chattext().substr(6,chat.chattext().size()-6), -1);
                        std::cout << "Invite player " << player_id << " to game" << myGame_id << std::endl;
                        inviteGame(myGame_id, player_id);
                    }
                    if (chat.chattext() == "join" || chat.chattext().compare(0, 9, "join help") == 0) {
                        if (playerid > 0) {
                            std::string msg = "The fee is 10 Flux sent to xxx or click here: https://coinrequest.io/request/0ThLBEvWkT8Bmg5";
                            sendTell(playerid, msg);
                            msg = "Send the payment, copy the <txid>, wait 3-5 minutes for it to confirm and then re-join";
                            sendTell(playerid, msg);
                            msg = "/msg PokerBot join <txid>";
                            sendTell(playerid, msg);
                        } else std::cout << "Join no playerid found" << std::endl;
                    }
                    if (chat.chattext().compare(0, 5, "join ") == 0) {
                        std::string txid = chat.chattext().substr(5);
                        static const std::regex rx("^[A-Fa-f0-9]{64}$");
                        if (!std::regex_match(txid, rx)) {
                            std::string msg = "Invalid txid " + txid;
                            sendTell(playerid, msg);
                        } else {
                            auto self = std::static_pointer_cast<TournamentDirector>(shared_from_this());

                            // Make a shared pointer to TransactionFetcher
                            auto fetcher = std::make_shared<TransactionFetcher>(io_context_, ssl_ctx_);

                            // Define the target URL
                            std::string url = "/daemon/getrawtransaction?verbose=1&txid="+txid;

                            // Start async fetch, capturing `self` and `fetcher` to keep them alive
                            fetcher->async_fetch(url,
                                [self, fetcher, playerid, txid](boost::system::error_code ec, Txn txn) {
                                    if (ec) {
                                        std::cout << "Failed to fetch transaction: " << ec.message() << std::endl;
                                        self->sendTell(playerid, "Transaction verification failed.");
                                        return;
                                    }
                                    std::cout << "Transaction fetch success for player " << playerid << std::endl;
                                    self->validateFluxFee(playerid, txid, txn);
                                }
                            );
                        }
                    }
                    if (chat.chattext().compare(0, 5, "send ") == 0) {
                        std::string txid = chat.chattext().substr(5);
                        static const std::regex rx("^[A-Fa-f0-9]{64}$");
                        if (!std::regex_match(txid, rx)) {
                            std::string msg = "Invalid txid " + txid;
                            sendTell(playerid, msg);
                        } else if (player_adr.size() == 0) {
                            std::string msg = "No player adr, use join <txid> to capture txid details";
                            sendTell(playerid, msg);
                        } else {
                            std::string pot_str = fmt::format("{:.8f}", player_pot);
                            std::string url_txns = "[{\"txid\":\"" + player_txid + "\",\"vout\":" + std::to_string(player_vout) + "}]";
                            std::string url_adrs = "{\"" + player_adr + "\":" + pot_str +"}";
                            std::string encoded_url = "transactions=" + url_encode(url_txns)+"&"+"addresses=" + url_encode(url_adrs);
                            std::cout << "encoded url " << encoded_url << std::endl;
                            std::cout << url_txns << std::endl;
                            std::cout << url_adrs<< std::endl;
                            std::cout << "script " << player_script << std::endl;
                            auto self = std::static_pointer_cast<TournamentDirector>(shared_from_this());

                            // Make a shared pointer to TransactionFetcher
                            auto fetcher = std::make_shared<TransactionFetcher>(io_context_, ssl_ctx_);

                            // Define the target URL
                            std::string url = "/daemon/createrawtransaction?" + encoded_url;

                            // Start async fetch, capturing `self` and `fetcher` to keep them alive
                            fetcher->async_fetch(url,
                                [self, fetcher, playerid, txid](boost::system::error_code ec, Txn txn) {
                                    if (ec) {
                                        std::cout << "Failed to fetch transaction: " << ec.message() << std::endl;
                                        self->sendTell(playerid, "Transaction verification failed.");
                                        return;
                                    }
                                    std::cout << "Transaction fetch success for player " << playerid << std::endl;
                                    //self->validateFluxFee(playerid, txid, txn);
                                    std::string hexstr = "(not found)";
                                    if (txn.raw.is_object()) {
                                        auto& obj = txn.raw.as_object();
                                        if (obj.contains("data")) {
                                            if (obj.at("data").is_string()) {
                                                hexstr = obj.at("data").as_string().c_str();
                                            }
                                        }
                                    } else std::cout << "create raw - no object" << std::endl;
                                    std::cout << "create raw: " << hexstr << std::endl;
                                }
                            );
                        }
                    }
                    if (chat.chattext() == "exit") {
                        std::cout << "Exiting TD" << std::endl;
                        io_context_.stop();
                    }
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_PlayerListMessage: {
                PlayerListMessage player = msg.playerlistmessage();
                uint32_t player_id = player.playerid();
                std::cout << "Player " << player_id << " Joined " << player.playerlistnotification() << std::endl;
                PokerTHMessage info;
                info.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_PlayerInfoRequestMessage);
                info.mutable_playerinforequestmessage()->add_playerid(player_id);
                send_message(info);
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_PlayerInfoReplyMessage: {
                const PlayerInfoReplyMessage info = msg.playerinforeplymessage();
                uint32_t player_id = info.playerid();
                if (info.has_playerinfodata()) {
                    std::string name = info.playerinfodata().playername();
                    std::cout << "Player " << player_id << " is " << name << std::endl;
                }
                break;
            }

            default:
            std::cerr << "TD Unhandled message type: " << msg.messagetype() << " size: " << data.size() << std::endl;
                break;
        }
    }

    void create_and_run_watcher_bot(boost::asio::io_context& io, const po::variables_map& vm, Table *wtable) {
        const std::string& username = wtable->watcher;
        const std::string& password = vm["watcher-password"].as<std::string>();
        const std::string& server_password = vm["server-password"].as<std::string>();
        const std::string& host = vm["host"].as<std::string>();
        const int& port = vm["port"].as<int>();
        std::string game_name = vm["game-name"].as<std::string>();
        auto bot = std::make_shared<WatcherBot>(io, vm, wtable);

        tcp::resolver resolver(io);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        std::cout << "Start WatcherBot"  << std::endl;
        boost::asio::async_connect(bot->socket(), endpoints,
            [&io, bot, username, password, server_password](boost::system::error_code ec, const tcp::endpoint&) {
                if (!ec) {
                    std::cout << "WatchBot connected successfully." << std::endl;
                    bot->start();
                } else {
                    std::cerr << "WatchBot connection failed: " << ec.message() << std::endl;
                }
            });
    }

    void startGame(uint32_t gameid) {
        PokerTHMessage start;
        start.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_StartEventMessage);
        start.mutable_starteventmessage()->set_gameid(gameid);
        start.mutable_starteventmessage()->set_starteventtype(StartEventMessage_StartEventType_startEvent);
        start.mutable_starteventmessage()->set_fillwithcomputerplayers(false);
        send_message(start);
    }

    void leaveGame(uint32_t gameid) {
        PokerTHMessage leave;
        leave.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_LeaveGameRequestMessage);
        leave.mutable_leavegamerequestmessage()->set_gameid(gameid);
        send_message(leave);
    }

    void inviteGame(uint32_t gameid, uint32_t player_id) {
        PokerTHMessage imsg;
        imsg.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_InvitePlayerToGameMessage);
        InvitePlayerToGameMessage* invite = imsg.mutable_inviteplayertogamemessage();
        invite->set_gameid(gameid);
        invite->set_playerid(player_id);
        send_message(imsg);
    }

    void createGame(std::string name, std::string password, NetGameInfo_NetGameType gameType, int nPlayers) {
        if (nPlayers < 3) nPlayers = 3; // Minimum of 3 players for now, until we get polling Invites
        // Send create game
        PokerTHMessage msg;
        msg.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_JoinNewGameMessage);
        JoinNewGameMessage *joinNew = msg.mutable_joinnewgamemessage();
        joinNew->set_autoleave(true);
        NetGameInfo *tmpGameInfo = joinNew->mutable_gameinfo();
        tmpGameInfo->set_netgametype(NetGameInfo_NetGameType_normalGame);
        tmpGameInfo->set_maxnumplayers(nPlayers);
        tmpGameInfo->set_raiseintervalmode(NetGameInfo_RaiseIntervalMode_raiseOnHandNum);
        tmpGameInfo->set_raiseeveryhands(5);
        tmpGameInfo->set_endraisemode(NetGameInfo_EndRaiseMode_keepLastBlind);
        tmpGameInfo->set_proposedguispeed(5);
        tmpGameInfo->set_delaybetweenhands(6);
        tmpGameInfo->set_playeractiontimeout(15);
        tmpGameInfo->set_endraisesmallblindvalue(0);
        tmpGameInfo->set_firstsmallblind(50);
        tmpGameInfo->set_startmoney(3000);
        tmpGameInfo->set_gamename(name);
        tmpGameInfo->set_netgametype(gameType);
        if (!password.empty()) {
            joinNew->set_password(password);
        }
        send_message(msg);
    }
    void createGame(std::string name, std::string password, NetGameInfo_NetGameType gameType) {
        createGame(name, password, gameType, 10);
    }

private:
    std::unordered_map<int, std::shared_ptr<WatcherBot>> watchers_;
    boost::asio::ssl::context ssl_ctx_;
    uint32_t myGame_id = 0; // for testing
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

