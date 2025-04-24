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
    PokerClient(boost::asio::io_context& io) : socket_(io) {}
    tcp::socket& socket() { return socket_; }
    virtual void start() { do_read_header(); }
    virtual void handle_message(const std::vector<char>& data) = 0;

    void send_message(const PokerTHMessage& msg) {
        std::string serialized;
        msg.SerializeToString(&serialized);
        uint32_t len = htonl(static_cast<uint32_t>(serialized.size()));
    
        std::vector<boost::asio::const_buffer> buffers = {
            boost::asio::buffer(&len, sizeof(len)),
            boost::asio::buffer(serialized)
        };
    
        boost::asio::async_write(socket_, buffers,
            [](boost::system::error_code ec, std::size_t) {
                if (ec) {
                    std::cerr << "Send error: " << ec.message() << std::endl;
                }
            });
    }
    
protected:
    void do_read_header() {
        auto self(shared_from_this());
        boost::asio::async_read(socket_, boost::asio::buffer(header_),
            [this, self](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    uint32_t msg_len = ntohl(*reinterpret_cast<uint32_t*>(header_.data()));
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
            [this, self](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    handle_message(body_);
                    do_read_header();
                } else {
                    std::cerr << "Body read error: " << ec.message() << std::endl;
                }
            });
    }

private:
    tcp::socket socket_;
    std::array<char, 4> header_;
    std::vector<char> body_;
};

class WatcherBot : public PokerClient {
public:
    using PokerClient::PokerClient;
    void handle_message(const std::vector<char>& data) override {
        // TODO: Parse message and track game state
    }
};

class TournamentDirector : public PokerClient {
public:
    TournamentDirector(boost::asio::io_context& io) : PokerClient(io), authCtx_(nullptr), authSession_(nullptr) {}

    ~TournamentDirector() {
        if (authSession_) gsasl_finish(authSession_);
        if (authCtx_) gsasl_done(authCtx_);
    }

    void set_auth_context(Gsasl* ctx, Gsasl_session* session) {
        authCtx_ = ctx;
        authSession_ = session;
    }

    void handle_message(const std::vector<char>& data) override {
        PokerTHMessage msg;
        if (!msg.ParseFromArray(data.data(), data.size())) {
            std::cerr << "Failed to parse PokerTHMessage" << std::endl;
            return;
        }

        switch (msg.messagetype()) {
            case PokerTHMessage_PokerTHMessageType_Type_AuthServerChallengeMessage: {
                const auto& challenge = msg.authserverchallengemessage().serverchallenge();

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
                authSession_ = nullptr;
                authCtx_ = nullptr;
                break;
            }

            default:
                std::cerr << "Unhandled message type: " << msg.messagetype() << std::endl;
                break;
        }
    }

    void create_game_with_watchers() {
        // TODO: Send CreateGameMessage, instantiate WatcherBot
    }

private:
    std::unordered_map<int, std::shared_ptr<WatcherBot>> watchers_;
    Gsasl* authCtx_;
    Gsasl_session* authSession_;
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
        int guestId = std::rand() % 99999 + 1;
        char guest[64];
        std::snprintf(guest, sizeof(guest), "Guest%05d", guestId);
        init->set_login(InitMessage::guestLogin);
        init->set_nickname(guest);
        client->send_message(msg);
    } else if (password.empty()) {
        init->set_login(InitMessage::unauthenticatedLogin);
        init->set_nickname(username);
        client->send_message(msg);
    } else {
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
    int game_id = -1;
    bool is_td = false;

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("host", po::value<std::string>(&host)->default_value("127.0.0.1"), "server host")
        ("port", po::value<int>(&port)->default_value(7234), "server port")
        ("td", po::bool_switch(&is_td)->default_value(false), "run as tournament director")
        ("username", po::value<std::string>(&username)->default_value("TD"), "username")
        ("password", po::value<std::string>(&password)->default_value(""), "user password")
        ("watcher-password", po::value<std::string>(&watcher_password)->default_value(""), "watcher bot password")
        ("server-password", po::value<std::string>(&server_password)->default_value(""), "server password")
        ("game-id", po::value<int>(&game_id)->default_value(-1), "game ID to watch (used by watcher bots)");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 0;
    }

    boost::asio::io_context io;

    if (!is_td) {
        std::cout << "Launching Watcher Bot for game ID: " << game_id << std::endl;
        auto watcher = std::make_shared<WatcherBot>(io);
        tcp::resolver resolver(io);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        boost::asio::async_connect(watcher->socket(), endpoints,
            [watcher, watcher_password, server_password](boost::system::error_code ec, const tcp::endpoint&) {
                if (!ec) {
                    async_connect_and_auth(watcher, "", watcher_password, server_password);
                    watcher->start();
                }
            });
        io.run();
        return 0;
    }

    auto td = std::make_shared<TournamentDirector>(io);
    tcp::resolver resolver(io);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    boost::asio::async_connect(td->socket(), endpoints,
        [td, username, password, server_password](boost::system::error_code ec, const tcp::endpoint&) {
            if (!ec) {
                async_connect_and_auth(td, username, password, server_password);
                td->start();
            }
        });

    io.run();
    return 0;
}
