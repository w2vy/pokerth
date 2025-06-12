#pragma once
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

#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <memory>
#include <boost/asio/post.hpp>

int safe_stoi(const std::string& str, int error_val);

namespace po = boost::program_options;

class PokerClient : public std::enable_shared_from_this<PokerClient> {
public:
    PokerClient(boost::asio::io_context& io, const po::variables_map& vm)
        : io_context_(io), socket_(io), vm_(vm) {}

    virtual void shutdownAndDie() {
        auto self = shared_from_this();
        std::cout << "shutdownAndDie" << std::endl;
        close();
        // Optionally clean up GSASL if used
        if (authSession_) {
            gsasl_finish(authSession_);
        }
        if (authCtx_) {
            gsasl_done(authCtx_);
        }
        boost::asio::post(io_context_, [self]() {});
    }

    tcp::socket& socket() { return socket_; }
    virtual void start() { do_read_header(); }
    virtual void handle_message(const std::vector<char>& data) = 0;

    virtual void close() {
        boost::system::error_code ec;
   
        // Cancel any ongoing asynchronous operations
        socket_.cancel(ec);
    
        // Shut down the socket (both directions)
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    
        // Close the socket
        socket_.close(ec);
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

    virtual void sendTell(uint32_t playerid, std::string tell) {
        PokerTHMessage chat;
        chat.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_ChatRequestMessage);
        ChatRequestMessage* ChatReq = chat.mutable_chatrequestmessage();
        ChatReq->set_chattext(tell);
        ChatReq->set_targetplayerid(playerid);
        send_message(chat);
    }

    virtual void sendLobby(std::string shout) {
        std::cout << shout << std::endl;
        PokerTHMessage chat;
        chat.set_messagetype(PokerTHMessage_PokerTHMessageType_Type_ChatRequestMessage);
        ChatRequestMessage* ChatReq = chat.mutable_chatrequestmessage();
        ChatReq->set_chattext(shout);
        send_message(chat);
    }

protected:
    boost::asio::io_context& io_context_; // Store reference
    tcp::socket socket_;
    const po::variables_map& vm_;
    Gsasl* authCtx_ = nullptr;
    Gsasl_session* authSession_ = nullptr;

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
                        shutdownAndDie();
                        return;
                    }
                    body_.resize(msg_len);
                    do_read_body();
                } else {
                    std::cerr << "Header read error: " << ec.message() << std::endl;
                    shutdownAndDie();
                    return;
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
                    shutdownAndDie();
                    return;
                }
            });
    }

private:
    std::array<char, 4> header_;
    std::vector<char> body_;
};

