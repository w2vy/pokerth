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

class PokerClient : public std::enable_shared_from_this<PokerClient> {
public:
    PokerClient(boost::asio::io_context& io, const po::variables_map& vm)
        : io_context_(io), socket_(io), vm_(vm) {}

    tcp::socket& socket() { return socket_; }
    virtual void start() { do_read_header(); }
    virtual void handle_message(const std::vector<char>& data) = 0;

    ~PokerClient() {
        clear_auth_context();
    }

    void clear_auth_context() {
        if (authSession_) gsasl_finish(authSession_);
        if (authCtx_) gsasl_done(authCtx_);
    }

    void set_auth_context(Gsasl* ctx, Gsasl_session* session) {
        authCtx_ = ctx;
        authSession_ = session;
    }

    void server_auth(const std::string& username, const std::string& password,
                     const std::string& server_password) {
        Gsasl* ctx = nullptr;
        Gsasl_session* session = nullptr;

        if (gsasl_init(&ctx) != GSASL_OK ||
            !gsasl_client_support_p(ctx, "SCRAM-SHA-1")) {
            std::cerr << "GSASL SCRAM-SHA-1 unsupported or init failed" << std::endl;
            return;
        }

        PokerTHMessage msg;
        msg.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_InitMessage);
        InitMessage* init = msg.mutable_initmessage();
        init->mutable_requestedversion()->set_majorversion(NET_VERSION_MAJOR);
        init->mutable_requestedversion()->set_minorversion(NET_VERSION_MINOR);
        init->set_buildid(0);

        if (!server_password.empty()) {
            init->set_authserverpassword(server_password);
        }

        if (username.empty()) {
            std::cout << "Login as Guest" << std::endl;
            int guestId = std::rand() % 99999 + 1;
            char guest[64];
            std::snprintf(guest, sizeof(guest), "Guest%05d", guestId);
            init->set_login(InitMessage::guestLogin);
            init->set_nickname(guest);
            send_message(msg);
        } else if (password.empty()) {
            std::cout << "Login Unauthenticated" << std::endl;
            init->set_login(InitMessage::unauthenticatedLogin);
            init->set_nickname(username);
            send_message(msg);
        } else {
            std::cout << "Login full auth!" << std::endl;
            if (gsasl_client_start(ctx, "SCRAM-SHA-1", &session) != GSASL_OK) {
                std::cerr << "GSASL client_start failed" << std::endl;
                gsasl_done(ctx);
                return;
            }
            gsasl_property_set(session, GSASL_AUTHID, username.c_str());
            gsasl_property_set(session, GSASL_PASSWORD, password.c_str());

            init->set_login(InitMessage::authenticatedLogin);

            char* tmpOut;
            size_t tmpOutSize;
            std::string nextGsaslMsg;

            if (gsasl_step(session, NULL, 0, &tmpOut, &tmpOutSize) == GSASL_NEEDS_MORE) {
                nextGsaslMsg.assign(tmpOut, tmpOutSize);
                gsasl_free(tmpOut);
                init->set_clientuserdata(nextGsaslMsg);
                send_message(msg);
                set_auth_context(ctx, session);
            } else {
                std::cerr << "GSASL step failed" << std::endl;
                gsasl_finish(session);
                gsasl_done(ctx);
            }
        }
    }

    void send_message(const PokerTHMessage& msg) {
        std::string serialized;
        msg.SerializeToString(&serialized);
        auto buffer = std::make_shared<std::vector<char>>();
        buffer->resize(4 + serialized.size());
        uint32_t len_net = htonl(static_cast<uint32_t>(serialized.size()));
        memcpy(buffer->data(), &len_net, 4);
        memcpy(buffer->data() + 4, serialized.data(), serialized.size());

        boost::asio::async_write(socket_, boost::asio::buffer(*buffer),
            [buffer](boost::system::error_code ec, std::size_t) {
                if (ec) {
                    std::cerr << "Send error: " << ec.message() << std::endl;
                }
        });
    }

protected:
    boost::asio::io_context& io_context_; // Store reference
    tcp::socket socket_;
    const po::variables_map& vm_;
    Gsasl* authCtx_;
    Gsasl_session* authSession_;

    void do_read_header() {
        auto self(shared_from_this());
        boost::asio::async_read(socket_, boost::asio::buffer(header_),
            [this, self](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    uint32_t msg_len;
                    memcpy(&msg_len, header_.data(), 4);
                    msg_len = ntohl(msg_len);
                    if (msg_len <= 0 || msg_len > 10 * 1024 * 1024) {
                        std::cerr << "Invalid message length: " << msg_len << std::endl;
                        return;
                    }
                    body_.resize(msg_len);
                    do_read_body();
                } else {
                    std::cerr << "Header read error: " << ec.message() << std::endl;
                }
            });
    }

    void do_read_body() {
        auto self(shared_from_this());
        boost::asio::async_read(socket_, boost::asio::buffer(body_),
            [this, self](boost::system::error_code ec, std::size_t bytes_transferred) {
                if (!ec && bytes_transferred == body_.size()) {
                    handle_message(body_);
                    do_read_header();
                } else {
                    std::cerr << "Body read error or incomplete: " << ec.message()
                              << ", bytes: " << bytes_transferred << " expected: " << body_.size() << std::endl;
                }
            });
    }

private:
    std::array<char, 4> header_;
    std::vector<char> body_;
};

class WatcherBot : public PokerClient {
public:
    WatcherBot(boost::asio::io_context& io, const po::variables_map& vm) : PokerClient(io, vm) {
        if (!vm.count("game-name")) {
            std::cout << "WatcherBot: No Game-Name specified!" << std::endl;
        } else {
            std::cout << "WatcherBot: Watch " << game_name << std::endl;
            game_name = vm["game-name"].as<std::string>();
        }
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

    std::string getPlayerName(uint32_t player_id) {
        auto it = Players.find(player_id);
        if (it != Players.end()) {
            return Players[player_id].name;
        }
        return "Player " + std::to_string(player_id);
    }

    void setPlayerName(uint32_t player_id, std::string name) {
        auto it = Players.find(player_id);
        if (it == Players.end()) {
            Players[player_id] = { player_id, name, start_money, 0, 0, 0 };
        } else {
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
            std::cout << getPlayerName(player_id) << " Stack " << Players[player_id].startingStack << std::endl;
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
            
                std::cout << "Received AnnounceMessage:\n"
                        << "  Protocol Version: " << protoVer.majorversion() << "." << protoVer.minorversion() << "\n"
                        << "  Latest Game Version: " << latestVer.majorversion() << "." << latestVer.minorversion() << "\n"
                        << "  Latest Beta Revision: " << betaRev << "\n"
                        << "  Server Type: " << serverType << "\n"
                        << "  Number of Players on Server: " << numPlayers << std::endl;

                const std::string& password = vm_["watcher-password"].as<std::string>();
                const std::string& server_password = vm_["server-password"].as<std::string>();
                            
                server_auth("WatcherBot", password, server_password);
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_InitAckMessage: {
                const auto& ack = msg.initackmessage();
                std::cout << "Received InitAckMessage:\n"
                          << "  Session ID: " << ack.yoursessionid() << "\n"
                          << "  Player ID: " << ack.yourplayerid() << std::endl;
                if (ack.has_youravatarhash()) {
                    std::cout << "  Avatar Hash: " << ack.youravatarhash() << std::endl;
                }
                if (ack.has_rejoingameid()) {
                    std::cout << "  Rejoin Game ID: " << ack.rejoingameid() << std::endl;
                }
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
                clear_auth_context();
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_GameListNewMessage: {
                std::cout << "Received GameListNewMessage" << std::endl;
                const GameListNewMessage& newGame = msg.gamelistnewmessage();
                int gameid = newGame.has_gameid() ? newGame.gameid() : 0;
                std::string gname = newGame.gameinfo().gamename();
                std::cout << "Game " << gname << " (" << gameid << ") just started!" << std::endl;
            
                if (game_id == 0 && gname == game_name) {
                    std::cout << "Found My Game " << gname << std::endl;
                    game_id = gameid;

                    PokerTHMessage joinMsg;
                    joinMsg.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_JoinExistingGameMessage);
                    JoinExistingGameMessage* joinGame = joinMsg.mutable_joinexistinggamemessage();
                    joinGame->set_gameid(game_id);
                    joinGame->set_spectateonly(true);
            
                    send_message(joinMsg);
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_JoinGameAckMessage: {
                const JoinGameAckMessage ack = msg.joingameackmessage();
                if (ack.gameid() == game_id) {
                    if (ack.has_spectateonly()) {
                        if (ack.spectateonly()) {
                            std::cout << "Joined game " << game_name << " (" << game_id << ") as spectator!" << std::endl;
                            if (ack.has_gameinfo()) {
                                start_money = ack.gameinfo().startmoney();
                            }
                        } else {
                            std::cout << "Joined game " << game_name << " (" << game_id << ") NOT as spectator!" << std::endl;
                        }
                    } else {
                        std::cout << "Joined game " << game_name << " (" << game_id << ") NO Spectator field!" << std::endl;
                    }
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_JoinGameFailedMessage: {
                const JoinGameFailedMessage nack = msg.joingamefailedmessage();
                if (nack.gameid() == game_id) {
                    int cause = nack.joingamefailurereason();
                    std::cout << "Join game " << game_name << " (" << game_id << ") as Spectator failed " << cause << std::endl;
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_GameStartInitialMessage: {
                const GameStartInitialMessage start = msg.gamestartinitialmessage();
                if (start.has_gameid()) {
                    std::string message = "Game started with " + std::to_string(start.playerseats_size()) + " Players";
                    PokerTHMessage chat;
                    chat.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_ChatRequestMessage);
                    ChatRequestMessage* ChatReq = chat.mutable_chatrequestmessage();
                    ChatReq->set_chattext(message);
                    send_message(chat);
                }
                for (int i=0;i<start.playerseats_size();i++) {
                    uint32_t player_id = start.playerseats()[i];
                    setPlayerStack(player_id, start_money);
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_GameListUpdateMessage: {
                const GameListUpdateMessage updateGame = msg.gamelistupdatemessage();

                if (updateGame.gameid() == game_id) { // Our game state has changed
                    switch (updateGame.gamemode()) {
                        case netGameClosed:
                            game_id = 0; // Our game is gone
                            // io_context_.stop();
                            break;
                        case netGameCreated:
                        case netGameStarted:
                            break;
                    }
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_GameListPlayerJoinedMessage: {
                const GameListPlayerJoinedMessage joined = msg.gamelistplayerjoinedmessage();
                if (joined.gameid() == game_id) {
                    uint32_t player = joined.playerid();
                    if (Players.find(player) == Players.end()) {
                        PokerTHMessage info;
                        info.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_PlayerInfoRequestMessage);
                        info.mutable_playerinforequestmessage()->add_playerid(player);
                        send_message(info);
                    }
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_PlayerInfoReplyMessage: {
                const PlayerInfoReplyMessage info = msg.playerinforeplymessage();
                uint32_t player_id = info.playerid();
                if (info.has_playerinfodata()) {
                    std::string name = info.playerinfodata().playername();
                    setPlayerName(player_id, name);
                    std::cout << "Player " << player_id << " is " << name << std::endl;
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
                if (endGame.has_gameid() && game_id == endGame.gameid()) {
                    std::cout << "Game " << game_name << " has ended - Winner: " << std::endl;
                    if (endGame.has_winnerplayerid()) {
                        std::cout << "The winner is: " << getPlayerName(endGame.winnerplayerid()) << std::endl;
                    }

                    auto [first, second] = find_winners();
                    std::string shout = "Congratulations to the winner of " + game_name + ": " + first->name + " and Runner Up " + second->name;
                    std::cout << shout << std::endl;
                    PokerTHMessage chat;
                    chat.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_ChatRequestMessage);
                    ChatRequestMessage* ChatReq = chat.mutable_chatrequestmessage();
                    ChatReq->set_chattext(shout);
                    send_message(chat);
                    // Set results so TD can see who advances
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_PlayersActionDoneMessage: {
                const PlayersActionDoneMessage done = msg.playersactiondonemessage();
                if (done.has_gameid() && done.gameid() == game_id && done.has_playerid() && done.has_playermoney()) {
                    //std::cout << "Players Action Done: Player " << getPlayerName(done.playerid()) << " Bet " << done.totalplayerbet() << " Money " << done.playermoney() << std::endl;
                    setPlayerStack(done.playerid(), done.playermoney());
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_PlayersTurnMessage: {
                const PlayersTurnMessage turn = msg.playersturnmessage();
                if (turn.gameid() == game_id) {
                    //std::cout << "Players Turn: Player: " << getPlayerName(turn.playerid()) << " " << getNetGameState(turn.gamestate()) << std::endl;
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_HandStartMessage: {
                const HandStartMessage start = msg.handstartmessage();
                if (start.has_gameid()) {
                    if (start.gameid() == game_id) {
                        std::cout << "Start Hand:";
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
                if (end.has_gameid() && end.gameid() == game_id && end.has_playerid() && end.has_playermoney()) {
                    std::cout << "End of Hand Hide: Player: " << getPlayerName(end.playerid()) << " Won " << end.moneywon() << " Total " << end.playermoney() << std::endl;
                    setPlayerStackWon(end.playerid(), end.moneywon());
                }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_EndOfHandShowCardsMessage: {
                EndOfHandShowCardsMessage show = msg.endofhandshowcardsmessage();
                if (show.gameid() == game_id) {
                    std::cout << "End of Hand Show Cards" << std::endl;
                    for (int i=0;i<show.playerresults_size();i++) {
                        PlayerResult res = show.playerresults()[i];
                        std::string hand = "";
                        if (res.has_cardsvalue()) {
                            hand = " Hand Strength: " + std::to_string(res.cardsvalue());
                            setPlayerHand(res.playerid(), res.cardsvalue());
                        }
                        std::cout << "Player " << getPlayerName(res.playerid()) << " Won " << res.moneywon() << " Total " << res.playermoney() << hand << std::endl;
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
                    std::cout << "Player " << getPlayerName(res.playerid()) << " Hand Strength: " + std::to_string(res.cardsvalue()) << std::endl;
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
                std::cout << "Received Chat: " << chat.chattext() << std::endl;
                if (chat.chattext() == "exit") {
                    std::cout << "Exiting TD" << std::endl;
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
            case PokerTHMessage_PokerTHMessageType_Type_DealFlopCardsMessage:
                break;
            case PokerTHMessage_PokerTHMessageType_Type_DealTurnCardMessage:
                break;
            case PokerTHMessage_PokerTHMessageType_Type_DealRiverCardMessage:
                break;
            default:
                std::cerr << "Unhandled message type: " << msg.messagetype() << std::endl;
                std::cout << "WatcherBot received message of size: " << data.size() << std::endl;
                break;
        }
}

private:
    std::string game_name;
    std::uint32_t game_id = 0;
    struct Player {
        uint32_t player_id;
        std::string name;
        int startingStack;    // Money at the start of the hand
        int committed;        // What they have bet in this hand
        int winnings;         // what money they won
        int hand;
    };
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

        std::cout << "1st: "  << first->name << "\n"
                << "2nd: "  << second->name << "\n";
        return {first, second};
    }
};

class TournamentDirector : public PokerClient {
public:
    TournamentDirector(boost::asio::io_context& io, const boost::program_options::variables_map& vm) : PokerClient(io, vm) {}

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
                std::cout << "Received InitAckMessage:\n"
                          << "  Session ID: " << ack.yoursessionid() << "\n"
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
                int cause = msg.errormessage().errorreason();
                std::cerr << "Received error, reason " << cause << std::endl;
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
                clear_auth_context();
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_JoinGameAckMessage: {
                const JoinGameAckMessage ack = msg.joingameackmessage();
                if (ack.has_gameid()) {
                    if (ack.has_spectateonly()) {
                        if (not ack.spectateonly()) {
                            std::cout << "Joined game not as spectator! Terminate" << std::endl;
                        } else {
                            std::cout << "Watching a game " << std::endl;
                        }
                    }
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_JoinGameFailedMessage: {
                const JoinGameFailedMessage failed = msg.joingamefailedmessage();
                if (failed.has_gameid()) {
                    uint32_t cause = failed.joingamefailurereason();
                    //if (myGameId == failed.gameid()) {
                        std::cout << "Join Game " << failed.gameid() << " " << cause << " Terminated" << std::endl;
                        // Consider signaling termination, depending on your architecture
                    //}
                }
                break;
            }

            case PokerTHMessage_PokerTHMessageType_Type_GameListNewMessage: {
                std::cout << "Received GameListNewMessage" << std::endl;
                const GameListNewMessage& newGame = msg.gamelistnewmessage();
                int gameid = newGame.has_gameid() ? newGame.gameid() : 0;
                std::string gname = newGame.gameinfo().gamename();
                std::cout << "Game " << gname << " (" << gameid << ") just started!" << std::endl;
            
                // if (state == 1 && watch == gname) {
                //     state++;
                //     myGameId = gameid;
                //     std::cout << "Found Game " << gname << std::endl;
            
                //     PokerTHMessage joinMsg;
                //     joinMsg.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_JoinExistingGameMessage);
                //     JoinExistingGameMessage* joinGame = joinMsg.mutable_joinexistinggamemessage();
                //     joinGame->set_gameid(myGameId);
                //     joinGame->set_spectateonly(true);
            
                //     if (!sendMessage(socket, joinMsg)) {
                //         std::cout << "Create game failed" << std::endl;
                //         // Consider signaling failure appropriately
                //     }
                // }
                break;
            }
            case PokerTHMessage_PokerTHMessageType_Type_EndOfGameMessage: {
                const EndOfGameMessage endGame = msg.endofgamemessage();
                if (endGame.has_gameid()) {
                    std::cout << "Game " << endGame.gameid() << " has ended" << std::endl;
                } else {
                    std::cout << "A Game has ended" << std::endl;
                }
                // if (endGame.has_gameid() && state == 2 && myGameId == endGame.gameid()) {
                //     std::cout << "Game " << watch << " has ended" << std::endl;
                //     // You may want to trigger cleanup or transition here
                // }
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
                std::cout << "Received Chat: " << chat.chattext() << std::endl;
                if (chat.chattext() == "watch") {
                    std::cout << "Starting WatcherBot" << std::endl;
                    create_and_run_watcher_bot(io_context_, vm_);
                }
                if (chat.chattext() == "exit") {
                    std::cout << "Exiting TD" << std::endl;
                    io_context_.stop();
                }
                break;
            }

            default:
                std::cerr << "Unhandled message type: " << msg.messagetype() << std::endl;
                break;
        }
    }

    void create_and_run_watcher_bot(boost::asio::io_context& io, const po::variables_map& vm) {
        const std::string& username = vm["username"].as<std::string>();
        const std::string& password = vm["watcher-password"].as<std::string>();
        const std::string& server_password = vm["server-password"].as<std::string>();
        const std::string& host = vm["host"].as<std::string>();
        const int& port = vm["port"].as<int>();
        std::string game_name = vm["game-name"].as<std::string>();
        auto bot = std::make_shared<WatcherBot>(io, vm);

        tcp::resolver resolver(io);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        boost::asio::async_connect(bot->socket(), endpoints,
            [&io, bot, username, password, server_password](boost::system::error_code ec, const tcp::endpoint&) {
                if (!ec) {
                    std::cout << "WatcherBot connected successfully." << std::endl;
                    bot->start();
                } else {
                    std::cerr << "WatcherBot connection failed: " << ec.message() << std::endl;
                }
            });
    }

private:
    std::unordered_map<int, std::shared_ptr<WatcherBot>> watchers_;
};

void async_connect_and_auth(std::shared_ptr<PokerClient> client,
                            const std::string& username,
                            const std::string& password,
                            const std::string& server_password) {
    Gsasl* ctx = nullptr;
    Gsasl_session* session = nullptr;

    if (gsasl_init(&ctx) != GSASL_OK ||
        !gsasl_client_support_p(ctx, "SCRAM-SHA-1")) {
        std::cerr << "GSASL SCRAM-SHA-1 unsupported or init failed" << std::endl;
        return;
    }

    PokerTHMessage msg;
    msg.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_InitMessage);
    InitMessage* init = msg.mutable_initmessage();
    init->mutable_requestedversion()->set_majorversion(NET_VERSION_MAJOR);
    init->mutable_requestedversion()->set_minorversion(NET_VERSION_MINOR);
    init->set_buildid(0);

    if (!server_password.empty()) {
        init->set_authserverpassword(server_password);
    }

    if (username.empty()) {
        std::cout << "Login as Guest" << std::endl;
        int guestId = std::rand() % 99999 + 1;
        char guest[64];
        std::snprintf(guest, sizeof(guest), "Guest%05d", guestId);
        init->set_login(InitMessage::guestLogin);
        init->set_nickname(guest);
        client->send_message(msg);
    } else if (password.empty()) {
        std::cout << "Login Unauthenticated" << std::endl;
        init->set_login(InitMessage::unauthenticatedLogin);
        init->set_nickname(username);
        client->send_message(msg);
    } else {
        std::cout << "Login full auth!" << std::endl;
        if (gsasl_client_start(ctx, "SCRAM-SHA-1", &session) != GSASL_OK) {
            std::cerr << "GSASL client_start failed" << std::endl;
            gsasl_done(ctx);
            return;
        }
        gsasl_property_set(session, GSASL_AUTHID, username.c_str());
        gsasl_property_set(session, GSASL_PASSWORD, password.c_str());

        init->set_login(InitMessage::authenticatedLogin);

        char* tmpOut;
        size_t tmpOutSize;
        std::string nextGsaslMsg;

        if (gsasl_step(session, NULL, 0, &tmpOut, &tmpOutSize) == GSASL_NEEDS_MORE) {
            nextGsaslMsg.assign(tmpOut, tmpOutSize);
            gsasl_free(tmpOut);
            init->set_clientuserdata(nextGsaslMsg);
            client->send_message(msg);

            if (auto td = std::dynamic_pointer_cast<TournamentDirector>(client)) {
                td->set_auth_context(ctx, session);
            }
        } else {
            std::cerr << "GSASL step failed" << std::endl;
            gsasl_finish(session);
            gsasl_done(ctx);
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

    if (false && !game_name.empty()) {
        std::cout << "Launching Watcher Bot for game: " << game_name << std::endl;
        auto watcher = std::make_shared<WatcherBot>(io, vm);
        tcp::resolver resolver(io);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        boost::asio::async_connect(watcher->socket(), endpoints,
            [watcher, username, watcher_password, server_password](boost::system::error_code ec, const tcp::endpoint&) {
                if (!ec) {
                    async_connect_and_auth(watcher, username, watcher_password, server_password);
                    watcher->start();
                }
            });
        io.run();
        return 0;
    }

    auto td = std::make_shared<TournamentDirector>(io, vm);
    tcp::resolver resolver(io);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    boost::asio::async_connect(td->socket(), endpoints,
    [&io, td, username, password, server_password](boost::system::error_code ec, const tcp::endpoint&) {
        if (!ec) {
            std::cout << "WatcherBot connected successfully." << std::endl;
            td->start();
        } else {
            std::cerr << "WatcherBot connection failed: " << ec.message() << std::endl;
        }
    });

    io.run();
    return 0;
}
