#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/bind_handler.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/buffer_body.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/version.hpp>
#include <boost/filesystem.hpp>
#include <boost/json.hpp>
#include <boost/json/src.hpp>
#include <boost/optional.hpp>
#include <boost/optional/optional_io.hpp>
#include <boost/program_options.hpp>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

const int TIMEOUT = 2;

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

void fail(beast::error_code ec, char const *what)
{
  std::cerr << what << ": " << ec.message() << "\n";
}

using Callback = std::function<void(bool, beast::error_code, std::size_t)>;

class PiecewiseForwarder : public std::enable_shared_from_this<PiecewiseForwarder>
{
public:
  PiecewiseForwarder(net::io_context &ioc, http::request_parser<http::buffer_body> &incParser,
                     beast::tcp_stream &incStrea);

  void forward(const char *host, const char *port, const Callback &cb);

private:
  template <typename Derived>
  std::shared_ptr<Derived> shared_from_base()
  {
    return std::static_pointer_cast<Derived>(shared_from_this());
  }

  const char *host() const;
  const char *port() const;

  void onResolveForward(beast::error_code ec, tcp::resolver::results_type results);
  void onConnectForward(beast::error_code ec, tcp::resolver::results_type::endpoint_type);

  void onWriteForwardHeader(beast::error_code ec, std::size_t);
  void readIncomingRequestBody();
  void onReadIncomingRequestBodyProgress(beast::error_code ec, std::size_t bytesTransferred);
  void onIncomingRequestBodyProgressSent(beast::error_code ec, std::size_t);
  void onReadIncomingRequestBody(beast::error_code ec, std::size_t);
  void onWriteForward(beast::error_code ec, std::size_t);

  void onReadForwardHeader(beast::error_code ec, std::size_t);
  void onReturnHeaderSent(beast::error_code ec, std::size_t);
  void readForward();
  void onReadForwardProgress(beast::error_code ec, std::size_t bytesTransferred);
  void onReturnProgressSent(beast::error_code ec, std::size_t);
  void onReadForward(beast::error_code ec, std::size_t);

  void onFinished(bool needEof, beast::error_code ec, std::size_t bytesTransmitted);

  boost::optional<tcp::resolver> resolver_;
  boost::optional<beast::tcp_stream> stream_;

  net::io_context &ioc_;

  beast::tcp_stream &incStream_;
  http::request_parser<http::buffer_body> &incomingParser_;

  boost::optional<http::request_serializer<http::buffer_body>> forwardSerial_;
  boost::optional<http::response_parser<http::buffer_body>> forwardParser_;
  boost::optional<http::response_serializer<http::buffer_body>> returnSerial_;

  static const unsigned int BODY_BUF_SIZE = 64 * 1024;
  static const unsigned int READ_BUF_SIZE = 8 * 1024 * 1024;

  char bodyBuf_[BODY_BUF_SIZE];
  beast::flat_buffer readBuffer_;

  std::string host_;
  std::string port_;

  Callback onFinished_;
};

PiecewiseForwarder::PiecewiseForwarder(net::io_context &ioc,
                                       http::request_parser<http::buffer_body> &incParser,
                                       beast::tcp_stream &incStream)
  : ioc_(ioc)
  , incStream_(incStream)
  , incomingParser_(incParser)
{
}

const char *PiecewiseForwarder::host() const
{
  return host_.c_str();
}

const char *PiecewiseForwarder::port() const
{
  return port_.c_str();
}

void PiecewiseForwarder::forward(const char *host, const char *port, const Callback &cb)
{
  host_ = host;
  port_ = port;
  onFinished_ = cb;

  resolver_.emplace(net::make_strand(ioc_));
  stream_.emplace(net::make_strand(ioc_));

  incStream_.expires_after(std::chrono::seconds(TIMEOUT));
  stream_->expires_after(std::chrono::seconds(TIMEOUT));

  resolver_->async_resolve(
      host, port,
      beast::bind_front_handler(&PiecewiseForwarder::onResolveForward, shared_from_this()));
}

void PiecewiseForwarder::onResolveForward(beast::error_code ec, tcp::resolver::results_type results)
{
  if (ec)
    return fail(ec, "onResolveForward");

  incStream_.expires_after(std::chrono::seconds(TIMEOUT));
  stream_->expires_after(std::chrono::seconds(TIMEOUT));

  stream_->async_connect(results, beast::bind_front_handler(&PiecewiseForwarder::onConnectForward,
                                                            shared_from_this()));
}

void PiecewiseForwarder::onConnectForward(beast::error_code ec,
                                          tcp::resolver::results_type::endpoint_type)
{
  if (ec)
    return fail(ec, "on_connect_forward_imm");

  incStream_.expires_after(std::chrono::seconds(TIMEOUT));
  stream_->expires_after(std::chrono::seconds(TIMEOUT));

  forwardSerial_.emplace(incomingParser_.get());
  http::async_write_header(*stream_, *forwardSerial_,
                           beast::bind_front_handler(&PiecewiseForwarder::onWriteForwardHeader,
                                                     shared_from_base<PiecewiseForwarder>()));
}

void PiecewiseForwarder::onWriteForwardHeader(beast::error_code ec, std::size_t)
{
  if (ec)
    return fail(ec, "on_write_forward_header_imm");

  assert(forwardSerial_->is_header_done());

  readIncomingRequestBody();
}

void PiecewiseForwarder::readIncomingRequestBody()
{
  incStream_.expires_after(std::chrono::seconds(TIMEOUT));
  stream_->expires_after(std::chrono::seconds(TIMEOUT));

  incomingParser_.get().body().data = bodyBuf_;
  incomingParser_.get().body().size = sizeof(bodyBuf_);

  http::async_read(incStream_, readBuffer_, incomingParser_,
                   beast::bind_front_handler(&PiecewiseForwarder::onReadIncomingRequestBodyProgress,
                                             shared_from_base<PiecewiseForwarder>()));
}

void PiecewiseForwarder::onReadIncomingRequestBodyProgress(beast::error_code ec,
                                                           std::size_t bytesTransferred)
{
  if (ec == http::error::need_buffer)
    ec = {};
  if (ec)
    return fail(ec, "onReadIncomingRequestBodyProgress");

  incomingParser_.get().body().data = bodyBuf_;
  incomingParser_.get().body().size = bytesTransferred;

  if (incomingParser_.is_done())
    return onReadIncomingRequestBody(ec, bytesTransferred);

  incomingParser_.get().body().more = true;

  incStream_.expires_after(std::chrono::seconds(TIMEOUT));
  stream_->expires_after(std::chrono::seconds(TIMEOUT));

  http::async_write(
      *stream_, *forwardSerial_,
      beast::bind_front_handler(&PiecewiseForwarder::onIncomingRequestBodyProgressSent,
                                shared_from_base<PiecewiseForwarder>()));
}

void PiecewiseForwarder::onIncomingRequestBodyProgressSent(beast::error_code ec, std::size_t)
{
  if (ec == http::error::need_buffer)
    ec = {};
  if (ec)
    return fail(ec, "onIncomingRequestBodyProgressSent");

  readIncomingRequestBody();
}

void PiecewiseForwarder::onReadIncomingRequestBody(beast::error_code ec, std::size_t)
{
  if (ec)
    return fail(ec, "onReadIncomingRequestBody");

  incStream_.expires_after(std::chrono::seconds(TIMEOUT));
  stream_->expires_after(std::chrono::seconds(TIMEOUT));

  incomingParser_.get().body().more = false;

  http::async_write(*stream_, *forwardSerial_,
                    beast::bind_front_handler(&PiecewiseForwarder::onWriteForward,
                                              shared_from_base<PiecewiseForwarder>()));
}

void PiecewiseForwarder::onWriteForward(beast::error_code ec, std::size_t)
{
  if (ec)
    return fail(ec, "on_write");

  incStream_.expires_after(std::chrono::seconds(TIMEOUT));
  stream_->expires_after(std::chrono::seconds(TIMEOUT));

  forwardParser_.emplace();
  forwardParser_->body_limit(boost::none);
  readBuffer_.reserve(READ_BUF_SIZE);

  http::async_read_header(*stream_, readBuffer_, *forwardParser_,
                          beast::bind_front_handler(&PiecewiseForwarder::onReadForwardHeader,
                                                    shared_from_base<PiecewiseForwarder>()));
}

void PiecewiseForwarder::onReadForwardHeader(beast::error_code ec, std::size_t)
{
  if (ec)
    return fail(ec, "onReadForwardHeader");

  incStream_.expires_after(std::chrono::seconds(TIMEOUT));
  stream_->expires_after(std::chrono::seconds(TIMEOUT));

  returnSerial_.emplace(forwardParser_->get());
  returnSerial_->split(true);

  http::async_write_header(incStream_, *returnSerial_,
                           beast::bind_front_handler(&PiecewiseForwarder::onReturnHeaderSent,
                                                     shared_from_base<PiecewiseForwarder>()));
}

void PiecewiseForwarder::onReturnHeaderSent(beast::error_code ec, std::size_t)
{
  if (ec)
    return fail(ec, "onReturnHeaderSent");

  assert(returnSerial_->is_header_done());

  readForward();
}

void PiecewiseForwarder::readForward()
{
  incStream_.expires_after(std::chrono::seconds(TIMEOUT));
  stream_->expires_after(std::chrono::seconds(TIMEOUT));

  forwardParser_->get().body().data = bodyBuf_;
  forwardParser_->get().body().size = sizeof(bodyBuf_);

  http::async_read(*stream_, readBuffer_, *forwardParser_,
                   beast::bind_front_handler(&PiecewiseForwarder::onReadForwardProgress,
                                             shared_from_base<PiecewiseForwarder>()));
}

void PiecewiseForwarder::onReadForwardProgress(beast::error_code ec, std::size_t bytesTransferred)
{
  if (ec == http::error::need_buffer)
    ec = {};
  if (ec)
    return fail(ec, "onReadForwardProgress");

  forwardParser_->get().body().data = bodyBuf_;
  forwardParser_->get().body().size = bytesTransferred;

  if (forwardParser_->is_done())
    return onReadForward(ec, bytesTransferred);

  forwardParser_->get().body().more = true;

  incStream_.expires_after(std::chrono::seconds(TIMEOUT));
  stream_->expires_after(std::chrono::seconds(TIMEOUT));

  http::async_write(incStream_, *returnSerial_,
                    beast::bind_front_handler(&PiecewiseForwarder::onReturnProgressSent,
                                              shared_from_base<PiecewiseForwarder>()));
}

void PiecewiseForwarder::onReturnProgressSent(beast::error_code ec, std::size_t)
{
  if (ec == http::error::need_buffer)
    ec = {};
  if (ec)
    return fail(ec, "onReturnProgressSent");

  readForward();
}

void PiecewiseForwarder::onReadForward(beast::error_code ec, std::size_t)
{
  if (ec)
    return fail(ec, "on_read");

  incStream_.expires_after(std::chrono::seconds(TIMEOUT));
  stream_->expires_after(std::chrono::seconds(TIMEOUT));

  forwardParser_->get().body().more = false;

  http::async_write(
      incStream_, *returnSerial_,
      [self = shared_from_base<PiecewiseForwarder>(), need_eof = returnSerial_->get().need_eof()](
          beast::error_code ec, std::size_t bytes) { self->onFinished(need_eof, ec, bytes); });

  stream_->socket().shutdown(tcp::socket::shutdown_both, ec);

  if (ec && ec != beast::errc::not_connected)
    return fail(ec, "shutdown");
}

void PiecewiseForwarder::onFinished(bool needEof, beast::error_code ec,
                                    std::size_t bytesTransmitted)
{
  onFinished_(needEof, ec, bytesTransmitted);
}

class Session : public std::enable_shared_from_this<Session>
{
public:
  Session(net::io_context &ioc, tcp::socket &&socket);
  ~Session();

  void run();

private:
  void doReadHeader();
  void onReadHeader(beast::error_code ec, std::size_t);
  void handleRequestPiecewise();
  std::string adaptTarget(const std::string &target);
  void piecewiseForward(const char *host, const char *port);
  void onWrite(bool close, beast::error_code ec, std::size_t);
  void doClose();

  static const unsigned int READ_BUF_SIZE = 8 * 1024 * 1024;

  net::io_context &ioc_;
  beast::tcp_stream stream_;
  std::shared_ptr<void> res_;
  boost::optional<http::request_parser<http::empty_body>> headParser_;
  boost::optional<http::request_parser<http::buffer_body>> bufParser_;
  beast::flat_buffer readBuffer_;
  std::shared_ptr<PiecewiseForwarder> fafForwarder_;
};

Session::Session(net::io_context &ioc, tcp::socket &&socket)
  : ioc_(ioc)
  , stream_(std::move(socket))
{
}

Session::~Session()
{
}

void Session::run()
{
  net::dispatch(stream_.get_executor(),
                beast::bind_front_handler(&Session::doReadHeader, shared_from_this()));
}

void Session::doReadHeader()
{
  stream_.expires_after(std::chrono::seconds(TIMEOUT));

  headParser_.emplace();
  headParser_->body_limit(boost::none);
  readBuffer_.reserve(READ_BUF_SIZE);

  http::async_read_header(stream_, readBuffer_, *headParser_,
                          beast::bind_front_handler(&Session::onReadHeader, shared_from_this()));
}

void Session::onReadHeader(beast::error_code ec, std::size_t)
{
  if (ec == http::error::end_of_stream)
    return doClose();
  if (ec)
    return fail(ec, "onReadHeader");

  handleRequestPiecewise();
}

void Session::handleRequestPiecewise()
{
  bufParser_.emplace(std::move(*headParser_));
  bufParser_->body_limit(boost::none);

  auto header = std::move(bufParser_->get().base());
  std::string target(header.target());
  std::string newTarget = adaptTarget(target);
  header.target(newTarget);

  bufParser_->get().base() = std::move(header);
  bufParser_->get().set(http::field::server, BOOST_BEAST_VERSION_STRING);
  piecewiseForward("localhost", "3001");
}

std::string Session::adaptTarget(const std::string &target)
{
  std::istringstream pathStream(target);
  std::string element;
  std::string lastDir, fileName;
  while (std::getline(pathStream, element, '/'))
  {
    lastDir = fileName;
    fileName = element;
  }
  return std::string("/") + fileName;
}

void Session::piecewiseForward(const char *host, const char *port)
{
  fafForwarder_ = std::make_shared<PiecewiseForwarder>(ioc_, *bufParser_, stream_);
  fafForwarder_->forward(
      host, port, [self = shared_from_this()](bool close, beast::error_code ec, std::size_t bytes) {
        self->onWrite(close, ec, bytes);
      });
}

void Session::onWrite(bool close, beast::error_code ec, std::size_t)
{
  if (ec)
    return fail(ec, "on_write");

  if (close)
    return doClose();

  res_ = nullptr;
  fafForwarder_ = nullptr;

  doReadHeader();
}

void Session::doClose()
{
  beast::error_code ec;
  stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
}

class Listener : public std::enable_shared_from_this<Listener>
{
public:
  Listener(net::io_context &ioc, tcp::endpoint endpoint);

  void run();

private:
  void doAccept();
  void onAccept(beast::error_code ec, tcp::socket socket);
  void initAcceptor(tcp::endpoint endpoint);

  net::io_context &ioc_;
  tcp::acceptor acceptor_;
};

Listener::Listener(net::io_context &ioc, tcp::endpoint endpoint)
  : ioc_(ioc)
  , acceptor_(net::make_strand(ioc))
{
  initAcceptor(endpoint);
}

void Listener::run()
{
  net::dispatch(acceptor_.get_executor(),
                beast::bind_front_handler(&Listener::doAccept, shared_from_this()));
}

void Listener::doAccept()
{
  acceptor_.async_accept(net::make_strand(ioc_),
                         beast::bind_front_handler(&Listener::onAccept, shared_from_this()));
}

void Listener::onAccept(beast::error_code ec, tcp::socket socket)
{
  if (ec)
  {
    fail(ec, "accept");
    return;
  }
  else
  {
    std::make_shared<Session>(ioc_, std::move(socket))->run();
  }

  doAccept();
}

void Listener::initAcceptor(tcp::endpoint endpoint)
{
  beast::error_code ec;

  acceptor_.open(endpoint.protocol(), ec);
  if (ec)
  {
    fail(ec, "open");
    return;
  }

  acceptor_.set_option(net::socket_base::reuse_address(true), ec);
  if (ec)
  {
    fail(ec, "set_option");
    return;
  }

  acceptor_.bind(endpoint, ec);
  if (ec)
  {
    fail(ec, "bind");
    return;
  }

  acceptor_.listen(net::socket_base::max_listen_connections, ec);
  if (ec)
  {
    fail(ec, "listen");
    return;
  }
}

int main(int argc, char *argv[])
{
  const auto address = net::ip::make_address("0.0.0.0");
  unsigned short port_num = 3000;
  net::io_context ioc;

  std::make_shared<Listener>(ioc, tcp::endpoint{address, port_num})->run();

  net::signal_set signals(ioc, SIGINT, SIGTERM);
  signals.async_wait([&](beast::error_code const &, int) { ioc.stop(); });

  ioc.run();

  return 0;
}
