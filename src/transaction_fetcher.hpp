#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <iostream>

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
namespace ssl   = asio::ssl;
namespace json  = boost::json;
using tcp       = asio::ip::tcp;

struct Txn {
    json::value raw;
};

inline bool isValidTxid(const std::string& txid) {
    return txid.size() == 64 && std::all_of(txid.begin(), txid.end(), [](char c) {
        return std::isxdigit(c);
    });
}

class TransactionFetcher {
public:
    TransactionFetcher(asio::io_context& io, ssl::context& ssl_ctx)
        : io_(io), ssl_ctx_(ssl_ctx) {}

    template<typename Handler>
    void async_fetch(const std::string& url, Handler&& handler) {
        auto host = std::string("api.runonflux.io");
        auto target = url;

        struct Session : public std::enable_shared_from_this<Session> {
          using std::enable_shared_from_this<Session>::shared_from_this;
            TransactionFetcher* fetcher;
            std::string host;
            std::string target;
            tcp::resolver resolver;
            ssl::stream<beast::tcp_stream> stream;
            beast::flat_buffer buffer;
            http::request<http::empty_body> req;
            http::response<http::string_body> res;
            int attempt = 0;
            Handler handler;

            Session(TransactionFetcher* f, std::string h, std::string t, Handler&& hnd)
                : fetcher(f)
                , host(std::move(h))
                , target(std::move(t))
                , resolver(f->io_)
                , stream(f->io_, f->ssl_ctx_)
                , handler(std::forward<Handler>(hnd)) {
                req.method(http::verb::get);
                req.version(11);
                req.target(target);
                req.set(http::field::host, host);
                req.set(http::field::user_agent, "PokerBotFetcher");
            }

            void run() {
                attempt++;
                auto self = shared_from_this();
                resolver.async_resolve(host, "443",
                    [self](auto ec, auto results) {
                        if (ec) return self->fail(ec);
                        beast::get_lowest_layer(self->stream).async_connect(results,
                            [self](auto ec, auto) {
                                if (ec) return self->fail(ec);
                                self->handshake();
                            });
                    });
            }

            void handshake() {
                auto self = shared_from_this();
                stream.async_handshake(ssl::stream_base::client,
                    [self](auto ec) {
                        if (ec) return self->fail(ec);
                        self->write_request();
                    });
            }

            void write_request() {
                auto self = shared_from_this();
                http::async_write(stream, req,
                    [self](auto ec, auto) {
                        if (ec) return self->fail(ec);
                        self->read_response();
                    });
            }

            void read_response() {
                auto self = shared_from_this();
                http::async_read(stream, buffer, res,
                    [self](auto ec, auto) {
                        if (ec) return self->fail(ec);
                        self->shutdown();
                    });
            }

            void shutdown() {
                auto self = shared_from_this();
                stream.async_shutdown(
                    [self](boost::system::error_code ec) {
                          if (ec) {
                            self->fail(ec); // or log or handle accordingly
                            return;
                        }
                        if (self->res.result() != http::status::ok)
                            return self->fail(beast::error_code{});
                        beast::error_code jec;
                        auto jv = json::parse(self->res.body(), jec);
                        if (jec || !jv.is_object())
                            return self->fail(beast::error_code{});
                        Txn txn{ jv };
                        self->handler({}, txn);
                    });
            }

            void fail(beast::error_code ec) {
                if (attempt < 3) {
                    auto self = shared_from_this();
                    auto timer = std::make_shared<asio::steady_timer>(fetcher->io_, std::chrono::milliseconds(500));
                    timer->async_wait([self, timer](auto) {
                        self->run();
                    });
                } else {
                    handler(ec, {});
                }
            }
        }; // Correctly closes Session struct

        auto session = std::make_shared<Session>(this, host, target, std::forward<Handler>(handler));
        session->run();
    }

private:
    asio::io_context& io_;
    ssl::context& ssl_ctx_;
};
