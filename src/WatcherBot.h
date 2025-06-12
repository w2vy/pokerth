#pragma once

#include "Table.h"
#include "TableManager.h"
#include "PokerClient.h"

#include <optional>

std::string printableSessionId(const std::string& sessionId);
std::string printableErrorReason(ErrorMessage_ErrorReason cause);
TableManager TourneyManager;

class WatcherBot : public PokerClient {
public:
    WatcherBot(boost::asio::io_context& io, const po::variables_map& vm, Table *table) : PokerClient(io, vm) {
        watchTable = table;
        std::cout << watchTable->watcher << ": Watch " << watchTable->name << std::endl;
    }

    std::string getNetPlayerState(uint32_t state) {
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

    std::string getNetGameState(NetGameState state) {
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

    void addPlayer(uint32_t player_id) {
        if (Players.find(player_id) == Players.end()) {
            Players[player_id] = { player_id, "", start_money, 0, 0, 0 };
        }
    }

    std::string getPlayerName(uint32_t player_id) {
        auto it = Players.find(player_id);
        if (it != Players.end()) {
            return Players[player_id].name;
        }
        return "Player " + std::to_string(player_id);
    }

    void setPlayerName(uint32_t player_id, std::string name) {
        auto it = Players.find(player_id);
        if (it != Players.end()) {
            Players[player_id].name = name;
        }
    }

    // The Pokerth messages give stack remaining, so we can compute
    // the committed chips StartingChips - stack (available from the message)
    void setPlayerStack(uint32_t player_id, int stackLeft) {
        auto it = Players.find(player_id);
        if (it != Players.end()) {
            int stack = Players[player_id].startingStack - stackLeft;
            if (stack < 0) stack =  Players[player_id].startingStack;
            Players[player_id].committed = stack;
        }
    }

    // At the End of Hand the winners will be given their winnings (committed wil be 0)

    void setPlayerStackWon(uint32_t player_id, int winnings) {
        auto it = Players.find(player_id);
        if (it != Players.end()) {
            Players[player_id].winnings = winnings;
        }
    }

    // At the start of the hand reset startingStack by reducing the stack by committed
    void setPlayersStartingStack(void) {
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
            std::cout << watchTable->watcher << " " << getPlayerName(player_id) << " Stack " << Players[player_id].startingStack << std::endl;
        }
    }

    void setPlayerHand(uint32_t player_id, int hand) {
        auto it = Players.find(player_id);
        if (it != Players.end()) {
            Players[player_id].hand = hand;
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
            
                std::cout << watchTable->watcher << " Received AnnounceMessage:\n"
                        << "  Protocol Version: " << protoVer.majorversion() << "." << protoVer.minorversion() << "\n"
                        << "  Latest Game Version: " << latestVer.majorversion() << "." << latestVer.minorversion() << "\n"
                        << "  Latest Beta Revision: " << betaRev << "\n"
                        << "  Server Type: " << serverType << "\n"
                        << "  Number of Players on Server: " << numPlayers << std::endl;

                const std::string& password = vm_["watcher-password"].as<std::string>();
                const std::string& server_password = vm_["server-password"].as<std::string>();
                            
                server_auth(watchTable->watcher, password, server_password);
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
                std::cerr << watchTable->watcher << " Received error, reason " << printableErrorReason(cause) << std::endl;
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
                std::cout << watchTable->watcher << " Authentication complete!" << std::endl;
                gsasl_finish(authSession_);
                gsasl_done(authCtx_);
                authSession_ = NULL;
                authCtx_ = NULL;
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_GameListNewMessage: {
                // When does this happen? Game Created or Play Starts?
                std::cout << watchTable->watcher << " Received GameListNewMessage" << std::endl;
                const GameListNewMessage& newGame = msg.gamelistnewmessage();
                uint32_t gameid = newGame.has_gameid() ? newGame.gameid() : 0;
                std::string gname = newGame.gameinfo().gamename();
                std::cout << "Game " << gname << " (" << gameid << ") just started!" << std::endl;
                std::cout << "watchTable Game " << watchTable->name << " " << watchTable->game_id << std::endl;
                std::optional<size_t>table_num = TourneyManager.findTableByGameId(gameid);
                if (table_num.has_value()) {
                    Table *t = TourneyManager.getTable(table_num);
                    int gid = 0;
                    if (t) gid = t->game_id;
                    std::cout << "findTable " << *table_num << " game id " << gid << std::endl;
                } else std::cout << "No table for Game " << gameid << std::endl;
            
                if (watchTable->game_id == gameid && gname == watchTable->name) {
                    std::cout << "Found My Game " << watchTable->name << std::endl;
                    //watchTable->game_id = gameid;

                    PokerTHMessage joinMsg;
                    joinMsg.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_JoinExistingGameMessage);
                    JoinExistingGameMessage* joinGame = joinMsg.mutable_joinexistinggamemessage();
                    joinGame->set_gameid(gameid);
                    joinGame->set_spectateonly(true);
            
                    send_message(joinMsg);
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_JoinGameAckMessage: {
                const JoinGameAckMessage ack = msg.joingameackmessage();
                std::cout << watchTable->watcher << " JoinGame Ack Watcher " << ack.gameid() << " Table " << watchTable->game_id << std::endl;
                if (ack.gameid() == watchTable->game_id) {
                    if (ack.has_spectateonly()) {
                        if (ack.spectateonly()) {
                            std::cout << "Joined game " << watchTable->name << " (" << watchTable->game_id << ") as spectator!" << std::endl;
                            if (ack.has_gameinfo()) {
                                start_money = ack.gameinfo().startmoney();
                            }
                        } else {
                            std::cout << "Joined game " << watchTable->name << " (" << watchTable->game_id << ") NOT as spectator!" << std::endl;
                        }
                    } else {
                        std::cout << "Joined game " << watchTable->name << " (" << watchTable->game_id << ") NO Spectator field!" << std::endl;
                    }
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_JoinGameFailedMessage: {
                const JoinGameFailedMessage nack = msg.joingamefailedmessage();
                if (nack.gameid() == watchTable->game_id) {
                    int cause = nack.joingamefailurereason();
                    std::cout << watchTable->watcher << " Join game " << watchTable->name << " (" << watchTable->game_id << ") as Spectator failed " << cause << std::endl;
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_GameStartInitialMessage: {
                const GameStartInitialMessage start = msg.gamestartinitialmessage();
                if (start.has_gameid()) {
                    uint32_t game_Id = start.gameid();
                    if (game_Id == watchTable->game_id) { // Our game has started
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
                        TourneyManager.updateState(watchTable, Playing);
                    }
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_GameListPlayerLeftMessage: {
                const GameListPlayerLeftMessage left = msg.gamelistplayerleftmessage();
                if (left.gameid() == watchTable->game_id) { // This is our table
                    watchTable->num_players--;
                    std::cout << watchTable->watcher << " WB Player for " << watchTable->name << " (" << watchTable->game_id << ") " + getPlayerName(left.playerid()) + " (" + std::to_string(left.playerid()) + ") has left " << std::to_string(watchTable->num_players) << " Players" << std::endl;
                    if (watchTable->num_players == 0) {
                        shutdownAndDie(); // Close our connection, close our object
                    }
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_GameListUpdateMessage: {
                // Removed 6/10 TD needs to handle this event
                // const GameListUpdateMessage updateGame = msg.gamelistupdatemessage();

                // if (updateGame.gameid() == watchTable->game_id) { // Our game state has changed
                //     switch (updateGame.gamemode()) {
                //         case netGameClosed:
                //             watchTable->game_id = 0; // Our game is gone
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
                    std::cout << watchTable->name << ": Player " << player_id << " is " << name << std::endl;
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
                if (endGame.has_gameid() && watchTable->game_id == endGame.gameid()) {
                    std::cout << watchTable->watcher << " Game " << watchTable->name << " has ended - Winner: " << std::endl;
                    if (endGame.has_winnerplayerid()) {
                        std::cout << "The winner is: " << getPlayerName(endGame.winnerplayerid()) << std::endl;
                    }

                    auto [first, second] = find_winners();
                    // Set results so TD can see who advances
                    Player Winner = {first->player_id, first->name, 0, 0, 0, 0};
                    watchTable->Invite.push_back(Winner);
                    Player RunnerUp = {second->player_id, second->name, 0, 0, 0, 0};
                    watchTable->Invite.push_back(RunnerUp);
                    Players.clear(); // Give winner all txids
                    TourneyManager.updateState(watchTable, Finished);
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_PlayersActionDoneMessage: {
                const PlayersActionDoneMessage done = msg.playersactiondonemessage();
                if (done.has_gameid() && done.gameid() == watchTable->game_id && done.has_playerid() && done.has_playermoney()) {
                    setPlayerStack(done.playerid(), done.playermoney());
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_PlayersTurnMessage: {
                const PlayersTurnMessage turn = msg.playersturnmessage();
                if (turn.gameid() == watchTable->game_id) {
                    //std::cout << "Players Turn: Player: " << getPlayerName(turn.playerid()) << " " << getNetGameState(turn.gamestate()) << std::endl;
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_HandStartMessage: {
                const HandStartMessage start = msg.handstartmessage();
                if (start.has_gameid()) {
                    if (start.gameid() == watchTable->game_id) {
                        std::cout << watchTable->watcher << " Start Hand:";
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
                if (end.has_gameid() && end.gameid() == watchTable->game_id && end.has_playerid() && end.has_playermoney()) {
                    std::cout << watchTable->watcher << " End of Hand Hide: Player: " << getPlayerName(end.playerid()) << " Won " << end.moneywon() << " Total " << end.playermoney() << std::endl;
                    setPlayerStackWon(end.playerid(), end.moneywon());
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_EndOfHandShowCardsMessage: {
                EndOfHandShowCardsMessage show = msg.endofhandshowcardsmessage();
                if (show.gameid() == watchTable->game_id) {
                    std::cout << watchTable->watcher << " End of Hand Show Cards" << std::endl;
                    for (int i=0;i<show.playerresults_size();i++) {
                        PlayerResult res = show.playerresults()[i];
                        std::string hand = "";
                        if (res.has_cardsvalue()) {
                            hand = " Hand Strength: " + std::to_string(res.cardsvalue());
                            setPlayerHand(res.playerid(), res.cardsvalue());
                        }
                        std::cout << watchTable->watcher << " Player " << getPlayerName(res.playerid()) << " Won " << res.moneywon() << " Total " << res.playermoney() << hand << std::endl;
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
                    std::cout << watchTable->watcher << " Player " << getPlayerName(res.playerid()) << " Hand Strength: " + std::to_string(res.cardsvalue()) << std::endl;
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
                std::cout << watchTable->watcher << " Received Chat: " << chat.chattext() << std::endl;
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
                std::cerr << watchTable->watcher << " Unhandled message type: " << msg.messagetype() << " size: " << data.size() << std::endl;
                break;
        }
}

private:
    Table *watchTable;
    std::unordered_map<int, Player> Players;
    std::int32_t start_money;

    struct Pot {
        int amount;
        std::vector<Player*> eligible_players;
    };
    
    // ------------------ Functions ------------------
    
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

