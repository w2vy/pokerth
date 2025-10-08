#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/json.hpp>
#include <boost/asio/ssl/error.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <functional>
#include <cctype>

#ifndef TLS1_2_VERSION
#define TLS1_2_VERSION 0x0303
#endif

struct Txn {
    boost::json::value raw;
};

inline bool isValidTxid(const std::string& txid) {
    return txid.size() == 64 &&
           std::all_of(txid.begin(), txid.end(), [](char c) { return std::isxdigit(c); });
}

class TransactionFetcher {
public:
    TransactionFetcher(boost::asio::io_context& io, boost::asio::ssl::context& ssl_ctx)
        : io_(io), ssl_ctx_(ssl_ctx)
    {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(boost::asio::ssl::verify_peer);

#if defined(SSL_CTX_set_min_proto_version)
        SSL_CTX_set_min_proto_version(ssl_ctx_.native_handle(), TLS1_2_VERSION);
#endif
    }

    template <typename Handler>
    void async_fetch(const std::string& host, const std::string& url, Handler&& handler)
    {
        using HandlerType = std::decay_t<Handler>;

        struct Session : public std::enable_shared_from_this<Session> {
            TransactionFetcher* fetcher;
            std::string host;
            std::string target;
            boost::asio::ip::tcp::resolver resolver;
            boost::asio::ssl::stream<boost::beast::tcp_stream> stream;
            boost::beast::flat_buffer buffer;
            boost::beast::http::request<boost::beast::http::empty_body> req;
            boost::beast::http::response<boost::beast::http::string_body> res;
            int attempt = 0;
            HandlerType handler;

            Session(TransactionFetcher* f, std::string h, std::string t, HandlerType&& hnd)
                : fetcher(f),
                  host(std::move(h)),
                  target(std::move(t)),
                  resolver(f->io_),
                  stream(f->io_, f->ssl_ctx_),
                  handler(std::forward<HandlerType>(hnd))
            {
                req.method(boost::beast::http::verb::get);
                req.version(11);
                req.target(target);
                req.set(boost::beast::http::field::host, host);
                req.set(boost::beast::http::field::user_agent, "PokerBotFetcher/1.0");
            }

            void run() {
                attempt++;
                auto self = this->shared_from_this();
                resolver.async_resolve(host, "443",
                    [self](auto ec, auto results) {
                        if (ec) return self->fail(ec);
                        boost::beast::get_lowest_layer(self->stream).expires_after(std::chrono::seconds(15));
                        boost::beast::get_lowest_layer(self->stream).async_connect(results,
                            [self](auto ec, auto) {
                                if (ec) return self->fail(ec);
                                self->handshake();
                            });
                    });
            }

            void handshake() {
                auto self = this->shared_from_this();
                if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
                    boost::beast::error_code ec(
                        static_cast<int>(::ERR_get_error()),
                        boost::asio::error::get_ssl_category()
                    );
                    self->fail(ec);
                    return;
                }

                stream.async_handshake(boost::asio::ssl::stream_base::client,
                    [self](auto ec) {
                        if (ec) return self->fail(ec);
                        self->write_request();
                    });
            }

            void write_request() {
                auto self = this->shared_from_this();
                boost::beast::http::async_write(stream, req,
                    [self](auto ec, auto) {
                        if (ec) return self->fail(ec);
                        self->read_response();
                    });
            }

            void read_response() {
                auto self = this->shared_from_this();
                boost::beast::http::async_read(stream, buffer, res,
                    [self](auto ec, auto) {
                        if (ec) return self->fail(ec);
                        self->shutdown();
                    });
            }

            void shutdown() {
                auto self = this->shared_from_this();
                stream.async_shutdown(
                    [self](boost::system::error_code ec) {
                        if (ec && ec != boost::asio::error::eof && ec != boost::asio::ssl::error::stream_truncated) {
                            self->fail(ec);
                            return;
                        }

                        if (self->res.result() != boost::beast::http::status::ok) {
                            std::cerr << "HTTP " << self->res.result_int()
                                      << ": " << self->res.reason()
                                      << "\nBody: " << self->res.body() << std::endl;
                            self->fail(boost::beast::error_code(
                                boost::system::errc::protocol_error,
                                boost::system::generic_category()
                            ));
                            return;
                        }

                        boost::beast::error_code jec;
                        auto jv = boost::json::parse(self->res.body(), jec);
                        if (jec || !jv.is_object()) {
                            self->fail(boost::beast::error_code());
                            return;
                        }

                        Txn txn{ jv };
                        self->handler({}, txn);
                    });
            }

            void fail(boost::beast::error_code ec) {
                std::cerr << "⚠️  Fetch attempt " << attempt
                          << " failed: " << ec.message() << std::endl;

                if (attempt < 3) {
                    auto f = fetcher;
                    auto h = host;
                    auto t = target;
                    auto hnd = handler;

                    auto timer = std::make_shared<boost::asio::steady_timer>(f->io_, std::chrono::seconds(1));
                    timer->async_wait([f, h, t, hnd = std::move(hnd), timer](auto) {
                        auto retry = std::make_shared<Session>(f, h, t, std::move(hnd));
                        retry->run();
                    });
                } else {
                    handler(ec, {});
                }
            }
        };

        auto session = std::make_shared<Session>(this, host, url, std::forward<Handler>(handler));
        session->run();
    }

private:
    boost::asio::io_context& io_;
    boost::asio::ssl::context& ssl_ctx_;
};
