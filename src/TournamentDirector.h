#pragma once

#include "PokerClient.h"
#include "WatcherBot.h"
#include "transaction_fetcher.hpp"
#include <regex>
#include <fmt/core.h>

#include <string>
#include <iomanip>
#include <sstream>

const int MaxTablePlayers = 10;

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

    enum TourneyType {
        NoTourney = 0,
        OneRound = 1,
        TwoRounds = 2
    };

    struct FluxResult {
        std::string vin_address;
        double paid_flux;
        double pot_flux;
        std::string txid;
        int vout_index;
    };

    void validateFluxFee(uint32_t playerid, const std::string& txid, const Txn& txn,
                        std::function<void(FluxResult)> on_result) {
        std::cout << "Player " << playerid << " Validate " << txid << std::endl;

        if (!txn.raw.is_object()) {
            std::cout << "Txn is not an object!" << std::endl;
            return;
        }

        const auto& obj = txn.raw.as_object();
        if (!obj.contains("data") || !obj.at("data").is_object()) {
            std::cout << "Missing or invalid 'data' field in txn" << std::endl;
            return;
        }

        const auto& data = obj.at("data").as_object();
        std::string msg;
        const std::string status = get_str(obj, "status");
        if (status == "error") {
            msg = "Failed: " + get_str(data, "name") + " " +
                std::to_string(get_val(data, "code")) + ": " +
                get_str(data, "message");
            sendTell(playerid, msg);
            return;
        }

        if (status != "success") {
            sendTell(playerid, "Unknown transaction status: " + status);
            return;
        }

        const auto confirmations = get_val(data, "confirmations");
        std::string short_txid = txid.substr(0, 6) + "..." + txid.substr(txid.size() - 6);
        const auto& vin_array = data.at("vin").as_array();
        const auto& vout_array = data.at("vout").as_array();

        if (confirmations <= 2) {
            msg = "Not confirmed (" + std::to_string(confirmations) + ") " + short_txid;
            sendTell(playerid, msg);
            return;
        }

        if (vin_array.empty() || vout_array.empty()) {
            msg = "Unexpected transaction format: # vin " + std::to_string(vin_array.size()) +
                " # vout " + std::to_string(vout_array.size()) + " for " + short_txid;
            sendTell(playerid, msg);
            return;
        }

        std::string vin_addr;
        int64_t vin_value = 0, vout_value = 0, pot_fee = 0;
        int vout_index = 0;

        for (const auto& vin_entry : vin_array) {
            const auto& vin_obj = vin_entry.as_object();
            vin_value += get_val64(vin_obj, "valueSat");
            std::string addr = get_str(vin_obj, "address");
            if (vin_addr.empty()) vin_addr = addr;
            else if (addr != vin_addr) {
                vin_addr.clear();
                break;
            }
        }

        int vout = 0;
        for (const auto& vout_entry : vout_array) {
            const auto& vout_obj = vout_entry.as_object();
            const auto value = get_val64(vout_obj, "valueSat");
            const auto& script = vout_obj.at("scriptPubKey");
            const auto& addresses = script.at("addresses").as_array();
            if (addresses.size() == 1) {
                std::string addr = addresses[0].as_string().c_str();
                if (addr == vin_addr) {
                    vout_value = value;
                }
                if (addr == botadr) {
                    vout_index = vout;
                    pot_fee = value;
                }
            }
            vout++;
        }

        const auto fee_paid = vin_value - vout_value;
        const double paid = static_cast<double>(fee_paid) / 1e8;
        const double pot = static_cast<double>(pot_fee) / 1e8;

        std::cout << "Player " << vin_addr << " vout " << vout_index << " txid " << txid << std::endl;
        msg = fmt::format("Accepted! Confirmations {} Paid: {:.8f} Flux Add to Pot: {:.8f} Flux, vout {}",
                        confirmations, paid, pot, vout_index);
        sendTell(playerid, msg);

        // ⬇️ Return values via callback
        on_result(FluxResult{vin_addr, paid, pot, txid, vout_index});
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
                    if (chat.chattext() == "solo" || chat.chattext() == "solo help") {
                        sendTell(playerid, "Syntax: solo <entry_fee> [prize_txid|'none'] [Game Name]");
                    }
                    if (chat.chattext().compare(0, 5, "solo ") == 0) {
                        if (activeTourney != NoTourney) {
                            sendTell(playerid, "There is already an active tourney");
                            break;
                        }
                        std::string input = chat.chattext().substr(5);  // strip "solo "
                        std::istringstream iss(input);
                        std::string fee_str, txid_str, rest;
                        
                        // Read first two tokens
                        iss >> fee_str >> txid_str;

                        // Validate fee
                        int64_t game_fee = 0;
                        try {
                            game_fee = std::stoll(fee_str);
                            if (game_fee < 0) throw std::invalid_argument("non-positive Entry fee");
                        } catch (...) {
                            sendTell(playerid, "Invalid Entry fee. Use: solo <entry_fee> [prize_txid|'none'] [Game Name]");
                            break;
                        }

                        std::string txid = "";
                        std::string game_name;

                        if (txid_str == "none") {
                            // Extract game_name if present
                            std::getline(iss >> std::ws, game_name);
                        } else {
                            // Validate txid
                            static const std::regex txid_rx("^[A-Fa-f0-9]{64}$");
                            if (!std::regex_match(txid_str, txid_rx)) {
                                sendTell(playerid, "Invalid txid format.");
                                break;
                            }
                            txid = txid_str;
                            std::getline(iss >> std::ws, game_name);
                        }

                        // ✅ Use: game_fee, txid ("" if none), and game_name (may be empty)
                        std::cout << "Entry Fee: " << game_fee << "\n";
                        std::cout << "TxID: " << txid << "\n";
                        std::cout << "Game Name: " << game_name << "\n";

                        std::string game_prize_txid = "";
                        auto self = std::static_pointer_cast<TournamentDirector>(shared_from_this());

                        auto openGame = [this, playerid, game_fee, game_name]() {
                            std::cout << "Create Solo Game" << std::endl;
                            activeTourney = OneRound;
                            registrationOpen = true;
                            entryFee = game_fee;
                            gameName = game_name;
                            maxRegistration = 10;
                            sendTell(playerid, game_name + " is open! Have players 'join <txid>' or 'join' if free");
                        };

                        if (txid.empty()) {
                            std::cout << "No txid provided. Proceeding without entry fee validation.\n";
                            openGame();  // No validation needed
                        } else {
                            std::cout << "Validating txid: " << txid << "\n";
                            auto fetcher = std::make_shared<TransactionFetcher>(io_context_, ssl_ctx_);
                            std::string url = "/daemon/getrawtransaction?verbose=1&txid=" + txid;

                            fetcher->async_fetch(url,
                                [self, fetcher, playerid, txid, openGame](boost::system::error_code ec, Txn txn) {
                                    if (ec) {
                                        std::cout << "Failed to fetch transaction: " << ec.message() << std::endl;
                                        self->sendTell(playerid, "Transaction verification failed.");
                                        return;
                                    }

                                    std::cout << "Transaction fetch success for prize " << playerid << std::endl;

                                    self->validateFluxFee(playerid, txid, txn, [self, openGame](FluxResult result) {
                                        std::cout << "Received result for player address: " << result.vin_address << std::endl;
                                        self->prizeFlux = result.pot_flux;
                                        self->prizeTxid = result.txid;
                                        openGame();
                                    });
                                }
                            );
                        }
                    }
                    if (chat.chattext() == "start") {
                        if (playerid == 0) {
                            std::cout << "join: no playerid for: " << chat.chattext() << std::endl;
                            break;
                        }
                        if (activeTourney == NoTourney) {
                            sendTell(playerid, "No tourney active, can't start!");
                            break;
                        }
                        if (!registrationOpen) {
                            sendTell(playerid, "Tourney is not open");
                            break;
                        }
                        registrationOpen = false;
                        int nextRegistered = 0;
                        int nTables = registeredPlayers.size()/MaxTablePlayers;
                        int nExtra = registeredPlayers.size() - (nTables*MaxTablePlayers);
                        if (nExtra > 0) {
                            nTables++;
                        }
                        int nPlayers = registeredPlayers.size()/nTables;
                        // Create nTables with nPlayers and nExtra tables have +1
                        int n = 0;
                        while (n < nTables) {
                            int np = nPlayers;
                            if (nExtra > 0 && nTables > 1) {
                                np = nPlayers + 1;
                                nExtra--;
                            }
                            n++;
                            std::cout << "Create " << gameName << std::endl;
                            std::optional<size_t> table_num = TourneyManager.allocateTable(this, "Temp Name");
                            Table* myTable = TourneyManager.getTable(table_num);
                            if (myTable) {
                                size_t tnum = (*table_num) + 1;
                                myTable->name = gameName + "_" + std::to_string(tnum);
                                myTable->watcher = "WatchBot" + std::to_string(tnum);
                                if (activeTourney == OneRound) myTable->type = Solo;
                                std::cout << "Create table " << myTable->name << " with " << np << " Players" << std::endl;
                                createGame(myTable->name, "", NetGameInfo_NetGameType_inviteOnlyGame, np);
                                int processed = 0;
                                for (auto it = registeredPlayers.begin(); it != registeredPlayers.end(); ++it) {
                                    if (it->first >= nextRegistered) {
                                        uint32_t pid = it->first;
                                        Player rPlayer = {pid, it->second, "", 0, 0, 0, 0};
                                        myTable->Invite.push_back(rPlayer);
                                        if (++processed >= np) break;
                                    }
                                }
                                nextRegistered += np;
                            } else {
                                std::cout << "Create Game Table failed" << std::endl;
                                break;
                            }
                        }
                    }
                    if (chat.chattext() == "join help") {
                        if (playerid == 0) {
                            std::cout << "join: no playerid for: " << chat.chattext() << std::endl;
                            break;
                        }
                        if (activeTourney == NoTourney) {
                            sendTell(playerid, "No tourney active. Join syntax: `join [txid]`");
                            sendTell(playerid, "Where txid is only needed for tourneys with an entry fee.");
                        } else {
                            if (entryFee > 0) {
                                std::string msg = fmt::format("The entry fee is {:.8f} Flux, which is sent to {} copy the txid for the join command",
                                    entryFee, botadr);
                                sendTell(playerid, msg);
                                sendTell(playerid, "join <txid>");
                            } else {
                                sendTell(playerid, "The current tourney has no entry fee, so just type 'join'");
                            }
                        }
                    }
                    if (chat.chattext() == "join") {
                        if (playerid == 0) {
                            std::cout << "join: no playerid for: " << chat.chattext() << std::endl;
                            break;
                        }
                        if (activeTourney == NoTourney) {
                            sendTell(playerid, "No tourney active, see 'join help' for more info");
                            break;
                        }
                        if (!registrationOpen) {
                            sendTell(playerid, "Registration is not open, sorry.");
                            break;
                        }
                        if (registeredPlayers.size() == maxRegistration) {
                            sendTell(playerid, "I am sorry the tourney is full, better luck next time!");
                            if (entryFee > 0) {
                                sendTell(playerid, "If you have sent an entry fee, you can use it for a future tourney");
                            }
                            break;
                        }
                        if (entryFee == 0) { // Enter player in tourney
                            sendTell(playerid, "You have been entered into the tourney, you will receive an invite when play starts");
                            registeredPlayers[playerid] = "free";
                            std::cout << "There are " << registeredPlayers.size() << " registered" << std::endl;
                            break;
                        } else {
                            std::string msg = fmt::format("The fee is {:.8f} Flux sent to {}", entryFee, botadr);
                            sendTell(playerid, msg);
                            msg = "Send the payment, copy the <txid>, wait 2-3 minutes for it to confirm and then re-join";
                            sendTell(playerid, msg);
                            msg = "/msg PokerBot join <txid>";
                            sendTell(playerid, msg);
                            break;
                        }
                    }
                    if (chat.chattext().compare(0, 5, "join ") == 0) {
                        if (activeTourney == NoTourney) {
                            sendTell(playerid, "No tourney active");
                            break;
                        }
                        if (!registrationOpen) {
                            sendTell(playerid, "Registration is not open, sorry.");
                            break;
                        }
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
                                [self, fetcher, playerid, txid, this](boost::system::error_code ec, Txn txn) {
                                    if (ec) {
                                        std::cout << "Failed to fetch transaction: " << ec.message() << std::endl;
                                        self->sendTell(playerid, "Transaction verification failed.");
                                        return;
                                    }
                                    std::cout << "Transaction fetch success for player " << playerid << std::endl;
                                    self->validateFluxFee(playerid, txid, txn, [this, playerid, txid](FluxResult result) {
                                        std::cout << "Received result for player address: " << result.vin_address << std::endl;
                                        if (result.paid_flux != entryFee) {
                                            sendTell(playerid, fmt::format("The entry fee is {:.8f} Flux, your txid is for {:.8f} Flux",
                                                entryFee, result.paid_flux));
                                        } else {
                                            sendTell(playerid, "Your entry has been accepted, you will receive an invite");
                                            std::cout << "Fee Paid " << result.paid_flux << " pot " << result.pot_flux << std::endl;
                                            registeredPlayers[playerid] = txid;
                                            std::cout << "There are " << registeredPlayers.size() << " registered" << std::endl;
                                        }
                                    });
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
    std::string botadr = "t1KbvgXPrJ1RuCzBr5FjsPZk7XUrswu99zu";
    TourneyType activeTourney = NoTourney;
    bool registrationOpen = false;
    uint32_t maxRegistration;
    Table *activeTable;
    std::string prizeTxid = "";
    std::unordered_map<int, std::string> registeredPlayers;
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

