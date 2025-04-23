#include <boost/asio.hpp>
#include <google/protobuf/message.h>
#include <memory>
#include <vector>
#include <array>
#include <unordered_map>
#include <iostream>
#include <boost/program_options.hpp>

using boost::asio::ip::tcp;
using namespace std;
namespace po = boost::program_options;

string server;
string port;
string username;
string password;
string spasswd;

class PokerClient : public std::enable_shared_from_this<PokerClient> {
public:
    PokerClient(boost::asio::io_context& io) : socket_(io) {}
    tcp::socket& socket() { return socket_; }
    virtual void start() { do_read_header(); }
    virtual void handle_message(const std::vector<char>& data) = 0;

    void send_message(const google::protobuf::Message& msg) {
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
                    do_read_body(msg_len);
                } else {
                    std::cerr << "Header read error: " << ec.message() << std::endl;
                }
            });
    }

    void do_read_body(std::size_t len) {
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
    TournamentDirector(boost::asio::io_context& io) : PokerClient(io) {}

    void handle_message(const std::vector<char>& data) override {
        // TODO: Parse lobby messages, create games, launch watchers
    }

    void create_game_with_watchers() {
        // TODO: Send CreateGameMessage, instantiate WatcherBot
    }

private:
    std::unordered_map<int, std::shared_ptr<WatcherBot>> watchers_;
};

int main() {
    // Check command line options.
    po::options_description desc("Allowed options");
    desc.add_options()
    ("help,h", "produce help message")
    ("server,s", po::value<string>(), "PokerTH server name")
    ("spasswd,S", po::value<string>(), "PokerTH Server Password")
    ("port,P", po::value<string>(), "PokerTH server port")
    ("username,u", po::value<string>(), "user name used for test")
    ("password,p", po::value<string>(), "password used for test")
    ("watch,w", po::value<string>(), "Watch Game (name) as spectator")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        cout << desc << endl;
        return 1;
    }
    if (!vm.count("server") || !vm.count("port") || !vm.count("mode")) {
        cout << "Missing option!" << endl << desc << endl;
        return 1;
    }

    server = vm["server"].as<string>();
    port = vm["port"].as<string>();
    if (vm.count("username")) {
        username = vm["username"].as<string>();
    }
    if (vm.count("password")) {
        password = vm["password"].as<string>();
    }
    if (vm.count("spasswd")) {
        spasswd = vm["spasswd"].as<string>();
    }
    boost::asio::io_context io;
    auto td = std::make_shared<TournamentDirector>(io);
    tcp::resolver resolver(io);
    auto endpoints = resolver.resolve(server, port);

    boost::asio::async_connect(td->socket(), endpoints,
        [td](boost::system::error_code ec, const tcp::endpoint&) {
            if (!ec) td->start();
        });

    io.run();
    return 0;
}

