#pragma once

#include "PokerClient.h"
#include "WatcherBot.h"
#include "transaction_fetcher.hpp"
#include <regex>
#include <format>

class TournamentDirector : public PokerClient {
public:
    TournamentDirector(boost::asio::io_context& io, const boost::program_options::variables_map& vm)
        : PokerClient(io, vm),
          ssl_ctx_(boost::asio::ssl::context::sslv23_client) {
        ssl_ctx_.set_verify_mode(boost::asio::ssl::verify_peer);
        ssl_ctx_.set_default_verify_paths();  // Or load specific CA bundle if needed
    }

    void validateFluxFee(uint32_t playerid, std::string txid, Txn txn) {
        std::string botadr = "t1PQKd6qpVyzrrN8ggwQGuAZz6LiLMsbZKu";
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
                        std::string player_adr; // vin.address
                        uint32_t vin_value, vout_value; // Player vin / vout; fee paid is vin - vout
                        uint32_t pot_fee; // May have gas fee deducted
                        const auto vin_entry = data["vin"].as_array();
                        const auto vout_array = data["vout"].as_array();
                        if (confirmations > 2) {
                            if (vin_entry.size() == 1 && vout_array.size() == 2) {
                                const auto& vin_obj = vin_entry.at(0).as_object();
                                vin_value = get_val(vin_obj, "valueSat");
                                player_adr = get_str(vin_obj, "address");
                                // Now vout
                                for (const auto& vout_entry : vout_array) {
                                    const auto& vout_obj = vout_entry.as_object();
                                    const auto value = get_val(vout_obj, "valueSat");
                                    const auto& addresses = vout_obj.at("scriptPubKey").at("addresses").as_array();
                                    if (addresses.size() == 1) {
                                        std::string addr = addresses[0].as_string().c_str();
                                        if (addr == player_adr) {
                                            vout_value = value;
                                        }
                                        if (addr == botadr) {
                                            pot_fee = value;
                                        }
                                    }
                                }
                                const auto fee_paid = vin_value - vout_value;
                                const double paid = static_cast<double>(fee_paid)/1e8;
                                const double pot = static_cast<double>(pot_fee)/1e8;
                                msg = "Accepted! Confirmations " + std::to_string(confirmations) + " Paid " + std::format("Paid: {:.8f} Flux", paid) + " Add to Pot " + std::format("Paid: {:.8f} Flux", pot);
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
                            //create_and_run_watcher_bot(io_context_, vm_, table);
                            if (table->type == GameTypes::Final) {
                                // The Final game was just created, now invite all the winners (first and second)
                                for (const Player& player : table->Invite) {
                                    inviteGame(gameid, player.player_id);
                                }
                            }
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
                    std::cout << "TD Player for " << table->name << " " << table->game_id << " has joined " << std::to_string(table->num_players) << " Players" << std::endl;
                    if (table->num_players > 1 && !table->watch_started) {
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
                            std::string url = "/daemon/getrawtransaction?verbose=1&txid=";

                            // Start async fetch, capturing `self` and `fetcher` to keep them alive
                            fetcher->async_fetch(txid, url,
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

    void sendTell(uint32_t playerid, std::string tell) {
        PokerTHMessage chat;
        chat.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_ChatRequestMessage);
        ChatRequestMessage* ChatReq = chat.mutable_chatrequestmessage();
        ChatReq->set_chattext(tell);
        ChatReq->set_targetplayerid(playerid);
        send_message(chat);
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

    void createGame(std::string name, std::string password, NetGameInfo_NetGameType gameType) {
        // Send create game
        PokerTHMessage msg;
        msg.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_JoinNewGameMessage);
        JoinNewGameMessage *joinNew = msg.mutable_joinnewgamemessage();
        joinNew->set_autoleave(true);
        NetGameInfo *tmpGameInfo = joinNew->mutable_gameinfo();
        tmpGameInfo->set_netgametype(NetGameInfo_NetGameType_normalGame);
        tmpGameInfo->set_maxnumplayers(10);
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

private:
    std::unordered_map<int, std::shared_ptr<WatcherBot>> watchers_;
    uint32_t myGame_id = 0;
    boost::asio::ssl::context ssl_ctx_;

    std::string get_str(const boost::json::object& obj, const std::string& key) {
        if (obj.contains(key)) {
            if (obj.at(key).is_string())
                return obj.at(key).as_string().c_str();
        }
        return "";
    }

    int get_val(const boost::json::object& obj, const std::string& key) {
        if (obj.contains(key))
            if (obj.at(key).is_number())
                return obj.at(key).as_int64();
        return -1;
    }
};

