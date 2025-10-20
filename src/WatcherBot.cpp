#include "PokerClient.h"
#include "Table.h"
#include "TableManager.h"
#include "WatcherBot.h"
#include <thread>
#include <optional>

std::string printableSessionId(const std::string& sessionId);
std::string printableErrorReason(ErrorMessage_ErrorReason cause);

WatcherBot::WatcherBot(boost::asio::io_context& io, const po::variables_map& vm, Table& table, TournamentDirector* td, TableManager& tourneyManager)
 : PokerClient(io, vm), io_(io), vm_(vm), watchTable_(table), mytd_(td), tourneyManager_(tourneyManager) {
    std::cout << watchTable_.info.watcher << ": Watch " << watchTable_.info.name << std::endl;
}

void WatcherBot::start(void) {
    const std::string& username = watchTable_.info.watcher;
    const std::string& password = vm_["watcher-password"].as<std::string>();
    const std::string& server_password = vm_["server-password"].as<std::string>();
    const std::string& host = vm_["host"].as<std::string>();
    const int& port = vm_["port"].as<int>();
    std::string game_name = vm_["game-name"].as<std::string>();

    tcp::resolver resolver(io_);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    std::cout << "Start WatcherBot"  << std::endl;
    tourneyManager_.updateState(mytd_, watchTable_, TableState::Connecting);

    boost::asio::async_connect(socket(), endpoints,
        [this, username, password, server_password](boost::system::error_code ec, const tcp::endpoint&) {
            if (!ec) {
                std::cout << "WatchBot connected successfully." << std::endl;
                do_read_header();
            } else {
                std::cerr << "WatchBot connection failed: " << ec.message() << std::endl;
            }
        });
}

std::string WatcherBot::getNetPlayerState(uint32_t state) {
    switch (state) {
        case 0:
            return "Normal";
        case 1:
            return "Inactive";
        case 2:
            return "No Money!";
        default:
            break;
    }
    return "Unknown State " + std::to_string(state);
}

std::string WatcherBot::getNetGameState(NetGameState state) {
    switch (state) {
        case netStatePreflop:
            return "PreFlop";
        case netStateFlop:
            return "Flop";
        case netStateTurn:
            return "Turn";
        case netStateRiver:
            return "River";
        case netStatePreflopSmallBlind:
            return "PreFlop Small Blind";
        case netStatePreflopBigBlind:
            return "PreFlop Big Blind";
        default:
            break;
    }
    return "Unknown State " + std::to_string(state);
}

void WatcherBot::addPlayer(uint32_t player_id) {
    if (Players.find(player_id) == Players.end()) {
        Players[player_id] = { player_id, {}, "", start_money, 0, 0, 0 };
    }
}

std::string WatcherBot::getPlayerName(uint32_t player_id) {
    auto it = Players.find(player_id);
    if (it != Players.end()) {
        return Players[player_id].name;
    }
    return "Player " + std::to_string(player_id);
}

uint32_t WatcherBot::getPlayerByName(std::string name) {
    // Find the player by name
    auto it = std::find_if(Players.begin(), Players.end(),
        [&name](const auto& pair) {
        return pair.second.name == name;
    });

    uint32_t id = 0;
    if (it != Players.end()) {
        std::cout << "Found player ID: " << it->second.player_id << std::endl;
        id = it->second.player_id;
    } else {
        std::cout << "Player not found." << std::endl;
    }
    return id;
}

void WatcherBot::setPlayerName(uint32_t player_id, std::string name) {
    auto it = Players.find(player_id);
    if (it != Players.end()) {
        Players[player_id].name = name;
    }
}

// The Pokerth messages give stack remaining, so we can compute
// the committed chips StartingChips - stack (available from the message)
void WatcherBot::setPlayerStack(uint32_t player_id, int stackLeft) {
    auto it = Players.find(player_id);
    if (it != Players.end()) {
        int stack = Players[player_id].startingStack - stackLeft;
        if (stack < 0) stack =  Players[player_id].startingStack;
        Players[player_id].committed = stack;
    }
}

// At the End of Hand the winners will be given their winnings (committed wil be 0)

void WatcherBot::setPlayerStackWon(uint32_t player_id, int winnings) {
    auto it = Players.find(player_id);
    if (it != Players.end()) {
        Players[player_id].winnings = winnings;
    }
}

// At the start of the hand reset startingStack by reducing the stack by committed
void WatcherBot::setPlayersStartingStack(void) {
    for (auto it = Players.begin(); it != Players.end(); ++it) {
        const int player_id   = it->first;
        if (Players[player_id].committed > 0) {
            int stack = Players[player_id].startingStack - Players[player_id].committed + Players[player_id].winnings;
            if (stack < 0) stack = 0;
            Players[player_id].startingStack = stack;
            Players[player_id].committed = 0;
            Players[player_id].winnings = 0;
        }
        Players[player_id].hand = 0;
        std::cout << watchTable_.info.watcher << " " << getPlayerName(player_id) << " Stack " << Players[player_id].startingStack << std::endl;
    }
}

void WatcherBot::setPlayerHand(uint32_t player_id, int hand) {
    auto it = Players.find(player_id);
    if (it != Players.end()) {
        Players[player_id].hand = hand;
    }
}

void WatcherBot::inviteGame(uint32_t gameid, uint32_t player_id) {
    PokerTHMessage imsg;
    std::cout << "inviteGame: " << gameid << " Player " << player_id << std::endl;
    imsg.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_InvitePlayerToGameMessage);
    InvitePlayerToGameMessage* invite = imsg.mutable_inviteplayertogamemessage();
    invite->set_gameid(gameid);
    invite->set_playerid(player_id);
    send_message(imsg);
}

void WatcherBot::startGame(uint32_t gameid) {
    PokerTHMessage start;
    start.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_StartEventMessage);
    start.mutable_starteventmessage()->set_gameid(gameid);
    start.mutable_starteventmessage()->set_starteventtype(StartEventMessage_StartEventType_startEvent);
    start.mutable_starteventmessage()->set_fillwithcomputerplayers(false);
    send_message(start);
}

void WatcherBot::leaveGame(uint32_t gameid) {
    PokerTHMessage leave;
    leave.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_LeaveGameRequestMessage);
    leave.mutable_leavegamerequestmessage()->set_gameid(gameid);
    send_message(leave);
}

void WatcherBot::watchGame(uint32_t gameid) {
    PokerTHMessage joinMsg;
    joinMsg.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_JoinExistingGameMessage);
    JoinExistingGameMessage* joinGame = joinMsg.mutable_joinexistinggamemessage();
    joinGame->set_gameid(gameid);
    joinGame->set_spectateonly(true);

    send_message(joinMsg);
}

void WatcherBot::handle_message(const std::vector<char>& data) {
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
        
            std::cout << watchTable_.info.watcher << " Received AnnounceMessage:\n"
                    << "  Protocol Version: " << protoVer.majorversion() << "." << protoVer.minorversion() << "\n"
                    << "  Latest Game Version: " << latestVer.majorversion() << "." << latestVer.minorversion() << "\n"
                    << "  Latest Beta Revision: " << betaRev << "\n"
                    << "  Server Type: " << serverType << "\n"
                    << "  Number of Players on Server: " << numPlayers << std::endl;

            const std::string& password = vm_["watcher-password"].as<std::string>();
            const std::string& server_password = vm_["server-password"].as<std::string>();
                        
            server_auth(watchTable_.info.watcher, password, server_password);
            break;
        }
        case PokerTHMessage_PokerTHMessageType_Type_InitAckMessage: {
            const auto& ack = msg.initackmessage();
            const std::string sessid = printableSessionId(ack.yoursessionid());
            watcherBotID = ack.yourplayerid();
            std::cout << "Received InitAckMessage:\n"
                        << "  Session ID: " << sessid << "\n"
                        << "  Player ID: " << watcherBotID << std::endl;
            if (ack.has_youravatarhash()) {
                std::cout << "  Avatar Hash: " << ack.youravatarhash() << std::endl;
            }
            if (ack.has_rejoingameid()) {
                std::cout << "  Rejoin Game ID: " << ack.rejoingameid() << std::endl;
            }
            if (watchTable_.state == TableState::Connecting) {
                tourneyManager_.updateState(mytd_, watchTable_, TableState::CreateGame);
                PokerTHMessage new_game = tourneyManager_.createGame(watchTable_.info.name, "", NetGameInfo_NetGameType_inviteOnlyGame, watchTable_.info.max_players);
                send_message(new_game);
            }
            break;
        }
        case PokerTHMessage_PokerTHMessageType_Type_ErrorMessage: {
            ErrorMessage_ErrorReason cause = msg.errormessage().errorreason();
            std::cerr << watchTable_.info.watcher << " Received error, reason " << printableErrorReason(cause) << std::endl;
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
            std::cout << watchTable_.info.watcher << " Authentication complete!" << std::endl;
            gsasl_finish(authSession_);
            gsasl_done(authCtx_);
            authSession_ = NULL;
            authCtx_ = NULL;
            break;
        }

        case PokerTHMessage_PokerTHMessageType_Type_GameListPlayerJoinedMessage: {
            const GameListPlayerJoinedMessage joined = msg.gamelistplayerjoinedmessage();
            uint32_t player_id = joined.playerid();
            std::cout << "GameListPlayerJoinedMessage: GameID " << joined.gameid() << " Player " << player_id << std::endl;
            if (joined.gameid() == watchTable_.info.game_id) { // Player joined our table
                watchTable_.num_players++;
                std::cout << "WB Player for " << watchTable_.info.name << " (" << watchTable_.info.game_id << ") " + std::to_string(joined.playerid()) + ") has joined " << std::to_string(watchTable_.num_players) << " Players" << std::endl;
                auto it = std::find_if(watchTable_.invite.begin(), watchTable_.invite.end(),
                       [&](const Player& p) { return p.player_id == player_id; });
                if (it!= watchTable_.invite.end()) {
                    watchTable_.invite.erase(it); // TODO capture txid
                }
                if (watchTable_.num_players > 2 && !watchTable_.left_table) { // Should be 5
                    watchTable_.left_table = true;
                    leaveGame(watchTable_.info.game_id); // start watching as a spectator when player leaves
                }
            }
            break;
        }

        case PokerTHMessage_PokerTHMessageType_Type_GameListNewMessage: {
            const GameListNewMessage& newGame = msg.gamelistnewmessage();
            uint32_t gameid = newGame.has_gameid() ? newGame.gameid() : 0;
            std::string gname = newGame.gameinfo().gamename();
            std::cout << "WB Game " << gname << " (" << gameid << ") just started!" << std::endl;
            std::cout << "watchTable Game " << watchTable_.info.name << " " << watchTable_.info.game_id << std::endl;

            if (watchTable_.info.game_id == gameid && gname == watchTable_.info.name) {
                std::cout << "Found My Game " << watchTable_.info.name << std::endl;
            }
            break;
        }

        case PokerTHMessage_PokerTHMessageType_Type_RejectInvNotifyMessage: {
            const RejectInvNotifyMessage reject = msg.rejectinvnotifymessage();
            std::cout << "Invite Rejected by " << reject.playerid() << " for game " << reject.gameid() << " reason " << reject.playerrejectreason() << std::endl;
            break;
        }

        case PokerTHMessage_PokerTHMessageType_Type_JoinGameAckMessage: {
            const JoinGameAckMessage ack = msg.joingameackmessage();
            bool speculate = false;
            if (ack.has_spectateonly()) {
                speculate = ack.spectateonly();
            }
            if (ack.has_gameid()) {
                int gameid = ack.gameid();
                std::string game_name = ack.gameinfo().gamename();
                if (ack.has_gameinfo()) {
                    start_money = ack.gameinfo().startmoney();
                }
                if (speculate) {
                    std::cout << "Joined game " << watchTable_.info.name << " (" << watchTable_.info.game_id << ") as spectator!" << std::endl;
                } else {
                    if (ack.areyougameadmin()) {
                        std::cout << "JoinGame Ack WatcherBot " << gameid << " Table " << game_name << std::endl;
                        if (game_name == watchTable_.info.name && watchTable_.info.game_id == 0) {
                            // Our game and gameid is not set
                            std::cout << "JoinGameAck WB " << game_name << " id " << gameid << std::endl;
                            std::cout << "Game ID set" << std::endl;
                            watchTable_.info.game_id = gameid;
                            if (watchTable_.state == TableState::CreateGame) {
                                tourneyManager_.updateState(mytd_, watchTable_, TableState::Inviting);
                                // create async thread to invite each player, until game starts or is gone
                                invitePlayers();
                            }
                        }
                    }
                }
            }
            break;
        }

        case PokerTHMessage_PokerTHMessageType_Type_JoinGameFailedMessage: {
            const JoinGameFailedMessage nack = msg.joingamefailedmessage();
            if (nack.gameid() == watchTable_.info.game_id) {
                int cause = nack.joingamefailurereason();
                std::cout << watchTable_.info.watcher << " Join game " << watchTable_.info.name << " (" << watchTable_.info.game_id << ") as Spectator failed " << cause << std::endl;
            }
            break;
        }

        case PokerTHMessage_PokerTHMessageType_Type_GameStartInitialMessage: {
            const GameStartInitialMessage start = msg.gamestartinitialmessage();
            if (start.has_gameid()) {
                uint32_t game_Id = start.gameid();
                if (game_Id == watchTable_.info.game_id) { // Our game has started
                    std::string message = "Game started with " + std::to_string(start.playerseats_size()) + " Players";
                    PokerTHMessage chat;
                    chat.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_ChatRequestMessage);
                    ChatRequestMessage* ChatReq = chat.mutable_chatrequestmessage();
                    ChatReq->set_chattext(message);
                    send_message(chat);

                    for (int i=0;i<start.playerseats_size();i++) {
                        uint32_t player_id = start.playerseats()[i];
                        addPlayer(player_id);
                        setPlayerStack(player_id, start_money);
                        PokerTHMessage info;
                        info.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_PlayerInfoRequestMessage);
                        info.mutable_playerinforequestmessage()->add_playerid(player_id);
                        send_message(info);
                    }
                    tourneyManager_.updateState(mytd_, watchTable_, TableState::Playing);
                }
            }
            break;
        }
        case PokerTHMessage_PokerTHMessageType_Type_GameListPlayerLeftMessage: {
            const GameListPlayerLeftMessage left = msg.gamelistplayerleftmessage();
            if (left.has_gameid() && left.has_playerid()) {
                if (left.gameid() == watchTable_.info.game_id) { // This is our table
                    if (left.playerid() == watcherBotID) {
                        // WatcherBot left the game, do we need to spectate?
                        if (watchTable_.state == TableState::Inviting) {
                            watchGame(watchTable_.info.game_id);
                        }
                    }
                    watchTable_.num_players--;
                    std::cout << watchTable_.info.watcher << " WB Player for " << watchTable_.info.name << " (" << watchTable_.info.game_id << ") " + getPlayerName(left.playerid()) + " (" + std::to_string(left.playerid()) + ") has left " << std::to_string(watchTable_.num_players) << " Players" << std::endl;
                    if (watchTable_.num_players == 0) {
                        shutdownAndDie(); // Close our connection, close our object
                    }
                }
            }
            break;
        }
        case PokerTHMessage_PokerTHMessageType_Type_GameListUpdateMessage: {
            // Removed 6/10 TD needs to handle this event
            // const GameListUpdateMessage updateGame = msg.gamelistupdatemessage();

            // if (updateGame.gameid() == watchTable_.info.game_id) { // Our game state has changed
            //     switch (updateGame.gamemode()) {
            //         case netGameClosed:
            //             watchTable_.info.game_id = 0; // Our game is gone
            //             // io_context_.stop();
            //             break;
            //         case netGameCreated:
            //         case netGameStarted:
            //             break;
            //     }
            // }
            // break;
        }
        case PokerTHMessage_PokerTHMessageType_Type_PlayerInfoReplyMessage: {
            const PlayerInfoReplyMessage info = msg.playerinforeplymessage();
            uint32_t player_id = info.playerid();
            if (info.has_playerinfodata()) {
                std::string name = info.playerinfodata().playername();
                setPlayerName(player_id, name);
                std::cout << watchTable_.info.name << ": Player " << player_id << " is " << name << std::endl;
            }
            break;
        }
        case PokerTHMessage_PokerTHMessageType_Type_EndOfGameMessage: {
            const EndOfGameMessage endGame = msg.endofgamemessage();
            if (endGame.has_gameid()) {
                std::cout << "Game " << endGame.gameid() << " has ended" << std::endl;
            } else {
                std::cout << "A Game has ended" << std::endl;
            }
            if (endGame.has_gameid() && watchTable_.info.game_id == endGame.gameid()) {
                std::cout << watchTable_.info.watcher << " Game " << watchTable_.info.name << " has ended - Winner: " << std::endl;
                if (endGame.has_winnerplayerid()) {
                    std::cout << "The winner is: " << getPlayerName(endGame.winnerplayerid()) << std::endl;
                }

                if (Players.size() > 1) {
                    auto [first, second] = find_winners();
                    // Set results so TD can see who advances
                    FluxResult entry = {};
                    if (first) {
                        auto res = mytd_->findFluxResult(first->player_id);
                        if (res) {
                            entry = {res->vin_address, res->paid_flux, res->pot_flux, res->txid, res->vout_index};
                        }
                        Player Winner = {first->player_id, entry, first->name, 0, 0, 0, 0};
                        std::cout << "Winner " << Winner.entryFee.vin_address << std::endl;
                        watchTable_.winners.push_back(Winner);
                        if (second && (watchTable_.info.type == Final || watchTable_.info.type == Qualifier)) {
                            entry = {};
                            auto res = mytd_->findFluxResult(second->player_id);
                            if (res) {
                                entry = {res->vin_address, res->paid_flux, res->pot_flux, res->txid, res->vout_index};
                            }
                            Player RunnerUp = {second->player_id, entry, second->name, 0, 0, 0, 0};
                            std::cout << "Runnerup " << RunnerUp.entryFee.vin_address << std::endl;
                            watchTable_.winners.push_back(RunnerUp);
                        }
                    }
                }
                Players.clear(); // Not needed any more, all players (w/txids) are in RegisteredPlayers{}
                tourneyManager_.updateState(mytd_, watchTable_, TableState::Finished); // TODO Move to bot app
            }
            break;
        }
        case PokerTHMessage_PokerTHMessageType_Type_PlayersActionDoneMessage: {
            const PlayersActionDoneMessage done = msg.playersactiondonemessage();
            if (done.has_gameid() && done.gameid() == watchTable_.info.game_id && done.has_playerid() && done.has_playermoney()) {
                setPlayerStack(done.playerid(), done.playermoney());
            }
            break;
        }

        case PokerTHMessage_PokerTHMessageType_Type_PlayersTurnMessage: {
            const PlayersTurnMessage turn = msg.playersturnmessage();
            if (turn.gameid() == watchTable_.info.game_id) {
                //std::cout << "Players Turn: Player: " << getPlayerName(turn.playerid()) << " " << getNetGameState(turn.gamestate()) << std::endl;
            }
            break;
        }
        case PokerTHMessage_PokerTHMessageType_Type_HandStartMessage: {
            const HandStartMessage start = msg.handstartmessage();
            if (start.has_gameid()) {
                if (start.gameid() == watchTable_.info.game_id) {
                    std::cout << watchTable_.info.watcher << " Start Hand:";
                    if (start.has_plaincards()) {
                        std::cout << " Card1 " << start.plaincards().plaincard1() << "Card 2 " << start.plaincards().plaincard2();
                    }
                    std::cout << " Small Blind " << start.smallblind() << " Seats: ";
                    for (int i = 0;i < start.seatstates_size();i++) {
                        std::cout << " " << getNetPlayerState(start.seatstates()[i]);
                    }
                    std::cout << std::endl;
                    setPlayersStartingStack();
                }
            }
            break;
        }
        case PokerTHMessage_PokerTHMessageType_Type_EndOfHandHideCardsMessage: {
            EndOfHandHideCardsMessage end = msg.endofhandhidecardsmessage();
            if (end.has_gameid() && end.gameid() == watchTable_.info.game_id && end.has_playerid() && end.has_playermoney()) {
                std::cout << watchTable_.info.watcher << " End of Hand Hide: Player: " << getPlayerName(end.playerid()) << " Won " << end.moneywon() << " Total " << end.playermoney() << std::endl;
                setPlayerStackWon(end.playerid(), end.moneywon());
            }
            break;
        }
        case PokerTHMessage_PokerTHMessageType_Type_EndOfHandShowCardsMessage: {
            EndOfHandShowCardsMessage show = msg.endofhandshowcardsmessage();
            if (show.gameid() == watchTable_.info.game_id) {
                std::cout << watchTable_.info.watcher << " End of Hand Show Cards" << std::endl;
                for (int i=0;i<show.playerresults_size();i++) {
                    PlayerResult res = show.playerresults()[i];
                    std::string hand = "";
                    if (res.has_cardsvalue()) {
                        hand = " Hand Strength: " + std::to_string(res.cardsvalue());
                        setPlayerHand(res.playerid(), res.cardsvalue());
                    }
                    std::cout << watchTable_.info.watcher << " Player " << getPlayerName(res.playerid()) << " Won " << res.moneywon() << " Total " << res.playermoney() << hand << std::endl;
                    setPlayerStackWon(res.playerid(), res.moneywon());
                }
            }
            break;
        }
        case PokerTHMessage_PokerTHMessageType_Type_AfterHandShowCardsMessage: {
            AfterHandShowCardsMessage show = msg.afterhandshowcardsmessage();
            PlayerResult res = show.playerresult();
            if (res.has_cardsvalue()) {
                setPlayerHand(res.playerid(), res.cardsvalue());
                std::cout << watchTable_.info.watcher << " Player " << getPlayerName(res.playerid()) << " Hand Strength: " + std::to_string(res.cardsvalue()) << std::endl;
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
            std::cout << watchTable_.info.watcher << " Received Chat: " << chat.chattext() << std::endl;
            if (chat.chattype() == ChatMessage_ChatType_chatTypePrivate) {
                if (chat.chattext() == "exit") {
                    std::cout << "Exiting WB" << std::endl;
                    io_context_.stop();
                }
                if (chat.chattext() == "ping") {
                    std::string msg = "Ping Pong " + std::to_string(8) + " times";
                    PokerTHMessage chat;
                    chat.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_ChatRequestMessage);
                    ChatRequestMessage* ChatReq = chat.mutable_chatrequestmessage();
                    ChatReq->set_chattext(msg);
                    send_message(chat);
                }
                if (chat.chattext().compare(0, 7, "invite ") == 0) {
                    std::string pname = chat.chattext().substr(7); // Invite player
                    uint32_t player_id = getPlayerByName(pname);
                    if (player_id > 0) {
                        inviteGame(watchTable_.info.game_id, player_id);
                        std::cout << "Invite sent for Player " << player_id << " in game " << watchTable_.info.game_id << std::endl;
                    }
                    std::cout << "Bad player_id " << player_id << std::endl;
                }
                if (chat.chattext() == "start") {
                    startGame(watchTable_.info.game_id);
                    std::cout << "Game " << watchTable_.info.game_id << " Started" << std::endl;
                }
            }
            break;
        }
        case PokerTHMessage_PokerTHMessageType_Type_PlayerListMessage:
            break;
        case PokerTHMessage_PokerTHMessageType_Type_GamePlayerJoinedMessage:
            break;
        case PokerTHMessage_PokerTHMessageType_Type_DealFlopCardsMessage:
            break;
        case PokerTHMessage_PokerTHMessageType_Type_DealTurnCardMessage:
            break;
        case PokerTHMessage_PokerTHMessageType_Type_DealRiverCardMessage:
            break;
        default:
            std::cerr << watchTable_.info.watcher << " Unhandled message type: " << msg.messagetype() << " size: " << data.size() << std::endl;
            break;
    }
}
