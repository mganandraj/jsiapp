#include "stdafx.h"

#include "inspector_socket_server.h"

//
// Copyright (c) 2016-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: Advanced server
//
//------------------------------------------------------------------------------

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/config.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <fstream>

using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
namespace http = boost::beast::http;            // from <boost/beast/http.hpp>
namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>

class websocket_session;
std::shared_ptr<websocket_session> ws_session;


// Return a reasonable mime type based on the extension of a file.
boost::beast::string_view
mime_type(boost::beast::string_view path)
{
  using boost::beast::iequals;
  auto const ext = [&path]
  {
    auto const pos = path.rfind(".");
    if (pos == boost::beast::string_view::npos)
      return boost::beast::string_view{};
    return path.substr(pos);
  }();
  if (iequals(ext, ".htm"))  return "text/html";
  if (iequals(ext, ".html")) return "text/html";
  if (iequals(ext, ".php"))  return "text/html";
  if (iequals(ext, ".css"))  return "text/css";
  if (iequals(ext, ".txt"))  return "text/plain";
  if (iequals(ext, ".js"))   return "application/javascript";
  if (iequals(ext, ".json")) return "application/json";
  if (iequals(ext, ".xml"))  return "application/xml";
  if (iequals(ext, ".swf"))  return "application/x-shockwave-flash";
  if (iequals(ext, ".flv"))  return "video/x-flv";
  if (iequals(ext, ".png"))  return "image/png";
  if (iequals(ext, ".jpe"))  return "image/jpeg";
  if (iequals(ext, ".jpeg")) return "image/jpeg";
  if (iequals(ext, ".jpg"))  return "image/jpeg";
  if (iequals(ext, ".gif"))  return "image/gif";
  if (iequals(ext, ".bmp"))  return "image/bmp";
  if (iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
  if (iequals(ext, ".tiff")) return "image/tiff";
  if (iequals(ext, ".tif"))  return "image/tiff";
  if (iequals(ext, ".svg"))  return "image/svg+xml";
  if (iequals(ext, ".svgz")) return "image/svg+xml";
  return "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string
path_cat(
  boost::beast::string_view base,
  boost::beast::string_view path)
{
  if (base.empty())
    return path.to_string();
  std::string result = base.to_string();
#if BOOST_MSVC
  char constexpr path_separator = '\\';
  if (result.back() == path_separator)
    result.resize(result.size() - 1);
  result.append(path.data(), path.size());
  for (auto& c : result)
    if (c == '/')
      c = path_separator;
#else
  char constexpr path_separator = '/';
  if (result.back() == path_separator)
    result.resize(result.size() - 1);
  result.append(path.data(), path.size());
#endif
  return result;
}

char ToLower(char c) {
  return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

bool StringEqualNoCase(const char* a, const char* b) {
  do {
    if (*a == '\0')
      return *b == '\0';
    if (*b == '\0')
      return *a == '\0';
  } while (ToLower(*a++) == ToLower(*b++));
  return false;
}

bool StringEqualNoCaseN(const char* a, const char* b, size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (ToLower(a[i]) != ToLower(b[i]))
      return false;
    if (a[i] == '\0')
      return true;
  }
  return true;
}

const char* MatchPathSegment(const char* path, const char* expected) {
  size_t len = strlen(expected);
  if (StringEqualNoCaseN(path, expected, len)) {
    if (path[len] == '/') return path + len + 1;
    if (path[len] == '\0') return path + len;
  }
  return nullptr;
}

void Escape(std::string* string) {
  for (char& c : *string) {
    c = (c == '\"' || c == '\\') ? '_' : c;
  }
}

std::string GetWsUrl(int port, const std::string& id) {
  char buf[1024];
  snprintf(buf, sizeof(buf), "127.0.0.1:%d/%s", port, id.c_str());
  return buf;
}

std::string MapToString(const std::map<std::string, std::string>& object) {
  bool first = true;
  std::ostringstream json;
  json << "{\n";
  for (const auto& name_value : object) {
    if (!first)
      json << ",\n";
    first = false;
    json << "  \"" << name_value.first << "\": \"";
    json << name_value.second << "\"";
  }
  json << "\n} ";
  return json.str();
}

std::string MapsToString(
  const std::vector<std::map<std::string, std::string>>& array) {
  bool first = true;
  std::ostringstream json;
  json << "[ ";
  for (const auto& object : array) {
    if (!first)
      json << ", ";
    first = false;
    json << MapToString(object);
  }
  json << "]\n\n";
  return json.str();
}

std::string getListResponse(node::inspector::InspectorSocketServer* server) {
    	std::vector<std::map<std::string, std::string>> response;
    	for (const std::string& id : server->Delegate()->GetTargetIds()) {
    		response.push_back(std::map<std::string, std::string>());
    		std::map<std::string, std::string>& target_map = response.back();
    		target_map["description"] = "node.js instance";
    		target_map["faviconUrl"] = "https://nodejs.org/static/favicon.ico";
    		target_map["id"] = id;
    		target_map["title"] = server->Delegate()->GetTargetTitle(id);
    		Escape(&target_map["title"]);
    		target_map["type"] = "node";
    		// This attribute value is a "best effort" URL that is passed as a JSON
    		// string. It is not guaranteed to resolve to a valid resource.
    		target_map["url"] = server->Delegate()->GetTargetUrl(id);
    		Escape(&target_map["url"]);

    		/*bool connected = false;
    		for (const auto& session : server->connected_sessions_) {
    			if (session.second->TargetId() == id) {
    				connected = true;
    				break;
    			}
    		}*/

    		//if (!connected) {
    			std::string address = GetWsUrl(server->port_, id);
    			std::ostringstream frontend_url;
    			frontend_url << "chrome-devtools://devtools/bundled";
    			frontend_url << "/inspector.html?experiments=true&v8only=true&ws=";
    			frontend_url << address;
    			target_map["devtoolsFrontendUrl"] += frontend_url.str();
    			target_map["webSocketDebuggerUrl"] = "ws://" + address;
    		//}
    	}
    	
      return MapsToString(response);
}

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<
  class Body, class Allocator,
  class Send>
  void
  handle_request(node::inspector::InspectorSocketServer* server,
    boost::beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>>&& req,
    Send&& send)
{
  // Returns a bad request response
  auto const bad_request =
    [&req](boost::beast::string_view why)
  {
    http::response<http::string_body> res{ http::status::bad_request, req.version() };
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = why.to_string();
    res.prepare_payload();
    return res;
  };

  // Returns a not found response
  auto const not_found =
    [&req](boost::beast::string_view target)
  {
    http::response<http::string_body> res{ http::status::not_found, req.version() };
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = "The resource '" + target.to_string() + "' was not found.";
    res.prepare_payload();
    return res;
  };

  // Returns a server error response
  auto const server_error =
    [&req](boost::beast::string_view what)
  {
    http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = "An error occurred: '" + what.to_string() + "'";
    res.prepare_payload();
    return res;
  };

  // Returns a server error response
  auto const respond_json_string =
    [&req](std::string str)
  {
    http::response<http::string_body> res{ http::status::ok, req.version() };
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/json");
    res.set(http::field::cache_control, "no-cache");
    res.set(http::field::content_length, str.size());
    res.keep_alive(req.keep_alive());
    res.body() = str;
    res.prepare_payload();
    return res;
  };

  const char* command = MatchPathSegment(req.target().to_string().c_str(), "/json");
  if (command == nullptr)
    return send(bad_request("Illegal request-target"));

  if (MatchPathSegment(req.target().to_string().c_str(), "list") || command[0] == '\0') {
    std::string response = getListResponse(server);
    return send(respond_json_string(response));
  }
  else if (MatchPathSegment(req.target().to_string().c_str(), "protocol")) {
    // SendProtocolJson(socket);
    std::ifstream t("concatenated_protocol.json");
    std::string str((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
    return send(respond_json_string(str));
  }
  else if (MatchPathSegment(req.target().to_string().c_str(), "version")) {
    std::map<std::string, std::string> response;
    response["Browser"] = "node.js/v8.0.0";
    response["Protocol-Version"] = "1.1";
    return send(respond_json_string(MapToString(response)));
  }
  else if (const char* target_id = MatchPathSegment(req.target().to_string().c_str(), "activate")) {
    if (server->TargetExists(target_id)) {
      return send(respond_json_string("Target activated"));
    }
  }
  
  return send(not_found(req.target()));
  
  //// Make sure we can handle the method
  //if (req.method() != http::verb::get &&
  //  req.method() != http::verb::head)
  //  return send(bad_request("Unknown HTTP-method"));


  //// Request path must be absolute and not contain "..".
  //if (req.target().empty() ||
  //  req.target()[0] != '/' ||
  //  req.target().find("..") != boost::beast::string_view::npos)
  //  return send(bad_request("Illegal request-target"));

  //// Build the path to the requested file
  //std::string path = path_cat(doc_root, req.target());
  //if (req.target().back() == '/')
  //  path.append("index.html");

  //// Attempt to open the file
  //boost::beast::error_code ec;
  //http::file_body::value_type body;
  //body.open(path.c_str(), boost::beast::file_mode::scan, ec);

  //// Handle the case where the file doesn't exist
  //if (ec == boost::system::errc::no_such_file_or_directory)
  //  return send(not_found(req.target()));

  //// Handle an unknown error
  //if (ec)
  //  return send(server_error(ec.message()));

  //// Respond to HEAD request
  //if (req.method() == http::verb::head)
  //{
  //  http::response<http::empty_body> res{ http::status::ok, req.version() };
  //  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  //  res.set(http::field::content_type, mime_type(path));
  //  res.content_length(body.size());
  //  res.keep_alive(req.keep_alive());
  //  return send(std::move(res));
  //}

  //// Respond to GET request
  //http::response<http::file_body> res{
  //    std::piecewise_construct,
  //    std::make_tuple(std::move(body)),
  //    std::make_tuple(http::status::ok, req.version()) };
  //res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  //res.set(http::field::content_type, mime_type(path));
  //res.content_length(body.size());
  //res.keep_alive(req.keep_alive());
  //return send(std::move(res));
}

//------------------------------------------------------------------------------

// Report a failure
void
fail(boost::system::error_code ec, char const* what)
{
  std::cerr << what << ": " << ec.message() << "\n";
}

// Echoes back all received WebSocket messages
class websocket_session : public std::enable_shared_from_this<websocket_session>
{
  websocket::stream<tcp::socket> ws_;
  boost::asio::strand<
    boost::asio::io_context::executor_type> strand_;
  boost::beast::multi_buffer buffer_;
  boost::beast::multi_buffer bufferForWrite_;

  node::inspector::InspectorSocketServer* inspectorServer_;

  std::mutex queueAccessMutex;
  std::queue<std::string> outQueue;

  boost::asio::steady_timer timer_;

public:
  // Take ownership of the socket
  explicit
    websocket_session(node::inspector::InspectorSocketServer* inspectorServer, tcp::socket socket)
    : inspectorServer_(inspectorServer)
    , ws_(std::move(socket))
    , strand_(ws_.get_executor())
    , timer_(ws_.get_executor().context(), (std::chrono::steady_clock::time_point::min)())
  {
  }

  // Start the asynchronous operation
  template<class Body, class Allocator>
  void
    run(http::request<Body, http::basic_fields<Allocator>> req)
  {
    // Accept the websocket handshake
    ws_.async_accept(
      req,
      boost::asio::bind_executor(
        strand_,
        std::bind(
          &websocket_session::on_accept,
          shared_from_this(),
          std::placeholders::_1)));
  }

  // Called when the timer expires.
  void
    on_accept(boost::system::error_code ec)
  {
    // Happens when the timer closes the socket
    if (ec == boost::asio::error::operation_aborted)
      return;

    if (ec)
      return fail(ec, "accept");

    // Read a message
    do_read();
  }

  void
    do_read()
  {
    // Read a message into our buffer
    ws_.async_read(
      buffer_,
      boost::asio::bind_executor(
        strand_,
        std::bind(
          &websocket_session::on_read,
          shared_from_this(),
          std::placeholders::_1,
          std::placeholders::_2)));
  }

  void
    on_read(
      boost::system::error_code ec,
      std::size_t bytes_transferred)
  {
    boost::ignore_unused(bytes_transferred);

    // Happens when the timer closes the socket
    if (ec == boost::asio::error::operation_aborted)
      return;

    // This indicates that the websocket_session was closed
    if (ec == websocket::error::closed)
      return;

    if (ec)
      fail(ec, "read");

    std::cout << "Received: " << boost::beast::buffers(buffer_.data()) << std::endl;

    // Clear the buffer
    buffer_.consume(buffer_.size());

    // Do another read
    do_read();

  }

  // This can be called from any thread.
  void write(std::string text) {
    queueAccessMutex.lock();
    bool first = outQueue.empty();
    outQueue.push(std::move(text));
    queueAccessMutex.unlock();

    // If freshly added into queue, start the timer which starts writing ...
    if (first) {
      timer_.async_wait(
        boost::asio::bind_executor(
          strand_,
          std::bind(
            &websocket_session::on_timer,
            shared_from_this(),
            std::placeholders::_1)));
    }
  }

  void on_timer(boost::system::error_code ec)
  {
    if (ec && ec != boost::asio::error::operation_aborted)
      return fail(ec, "timer");

    std::cout << "on_timer .. " << std::endl;


    do_write();
  }

  void do_write()
  {
    queueAccessMutex.lock();
    if (outQueue.empty()) return;
    std::string message = outQueue.front();
    outQueue.pop();
    queueAccessMutex.unlock();

    std::cout << "Writing: " << message << std::endl;

    size_t n = buffer_copy(bufferForWrite_.prepare(message.size()), boost::asio::buffer(message));
    bufferForWrite_.commit(n);

    ws_.text(true);
    ws_.async_write(
      bufferForWrite_.data(),
      boost::asio::bind_executor(
        strand_,
        std::bind(
          &websocket_session::on_write,
          shared_from_this(),
          std::placeholders::_1,
          std::placeholders::_2)));
  }

  void
    on_write(
      boost::system::error_code ec,
      std::size_t bytes_transferred)
  {
    boost::ignore_unused(bytes_transferred);



    // Happens when the timer closes the socket
    if (ec == boost::asio::error::operation_aborted)
      return;

    if (ec)
      return fail(ec, "write");

    std::cout << "Writing completed .. " << bytes_transferred << " bytes." << std::endl;

    // Clear the buffer
    // buffer_.consume(buffer_.size());

    // do another write
    do_write();

  }
};

// Handles an HTTP server connection
class http_session : public std::enable_shared_from_this<http_session>
{
  // This is the C++11 equivalent of a generic lambda.
  // The function object is used to send an HTTP message.
  struct send_lambda
  {
    http_session& self_;

    explicit
      send_lambda(http_session& self)
      : self_(self)
    {
    }

    template<bool isRequest, class Body, class Fields>
    void
      operator()(http::message<isRequest, Body, Fields>&& msg) const
    {
      // The lifetime of the message has to extend
      // for the duration of the async operation so
      // we use a shared_ptr to manage it.
      auto sp = std::make_shared<
        http::message<isRequest, Body, Fields>>(std::move(msg));

      // Store a type-erased version of the shared
      // pointer in the class to keep it alive.
      self_.res_ = sp;

      // Write the response
      http::async_write(
        self_.socket_,
        *sp,
        boost::asio::bind_executor(
          self_.strand_,
          std::bind(
            &http_session::on_write,
            self_.shared_from_this(),
            std::placeholders::_1,
            std::placeholders::_2,
            sp->need_eof())));
    }
  };

  tcp::socket socket_;
  boost::asio::strand<
    boost::asio::io_context::executor_type> strand_;
  boost::beast::flat_buffer buffer_;
  std::string const& doc_root_;
  http::request<http::string_body> req_;
  std::shared_ptr<void> res_;
  send_lambda lambda_;
  node::inspector::InspectorSocketServer* inspectorServer_;

public:
  // Take ownership of the socket
  explicit
    http_session(
      node::inspector::InspectorSocketServer* inspectorServer,
      tcp::socket socket,
      std::string const& doc_root)
    : inspectorServer_(inspectorServer)
    , socket_(std::move(socket))
    , strand_(socket_.get_executor())
    , doc_root_(doc_root)
    , lambda_(*this)
  {
  }

  // Start the asynchronous operation
  void
    run()
  {
    do_read();
  }

  void
    do_read()
  {
    // Read a request
    http::async_read(socket_, buffer_, req_,
      boost::asio::bind_executor(
        strand_,
        std::bind(
          &http_session::on_read,
          shared_from_this(),
          std::placeholders::_1)));
  }

  void on_read(boost::system::error_code ec)
  {
    // Happens when the timer closes the socket
    if (ec == boost::asio::error::operation_aborted)
      return;

    // This means they closed the connection
    if (ec == http::error::end_of_stream)
      return do_close();

    if (ec)
      return fail(ec, "read");

    // See if it is a WebSocket Upgrade
    if (websocket::is_upgrade(req_))
    {
      // server->SessionStarted(SocketSession::From(socket), id);

      // Create a WebSocket websocket_session by transferring the socket
      ws_session = std::make_shared<websocket_session>(
        inspectorServer_,
        std::move(socket_));
      ws_session->run(std::move(req_));
      return;
    }

    // Send the response
    handle_request(inspectorServer_, doc_root_, std::move(req_), lambda_);
  }

  void
    on_write(boost::system::error_code ec, std::size_t bytes_transferred, bool close)
  {
    boost::ignore_unused(bytes_transferred);

    // Happens when the timer closes the socket
    if (ec == boost::asio::error::operation_aborted)
      return;

    if (ec)
      return fail(ec, "write");

    if (close)
    {
      // This means we should close the connection, usually because
      // the response indicated the "Connection: close" semantic.
      return do_close();
    }

    // We're done with the response so delete it
    res_ = nullptr;

    // Read another request
    do_read();
  }

  void
    do_close()
  {
    // Send a TCP shutdown
    boost::system::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
  }
};

//------------------------------------------------------------------------------

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener>
{
  tcp::acceptor acceptor_;
  tcp::socket socket_;
  std::string const& doc_root_;
  node::inspector::InspectorSocketServer* inspectorServer_;

public:
  listener(
    node::inspector::InspectorSocketServer* inspectorServer,
    boost::asio::io_context& ioc,
    tcp::endpoint endpoint,
    std::string const& doc_root)
    : inspectorServer_(inspectorServer)
    , acceptor_(ioc)
    , socket_(ioc)
    , doc_root_(doc_root)
  {
    boost::system::error_code ec;

    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec)
    {
      fail(ec, "open");
      return;
    }

    // Bind to the server address
    acceptor_.bind(endpoint, ec);
    if (ec)
    {
      fail(ec, "bind");
      return;
    }

    // Start listening for connections
    acceptor_.listen(
      boost::asio::socket_base::max_listen_connections, ec);
    if (ec)
    {
      fail(ec, "listen");
      return;
    }
  }

  // Start accepting incoming connections
  void
    run()
  {
    if (!acceptor_.is_open())
      return;
    do_accept();
  }

  void
    do_accept()
  {
    acceptor_.async_accept(
      socket_,
      std::bind(
        &listener::on_accept,
        shared_from_this(),
        std::placeholders::_1));
  }

  void
    on_accept(boost::system::error_code ec)
  {
    if (ec)
    {
      fail(ec, "accept");
    }
    else
    {
      // Create the http_session and run it
      std::make_shared<http_session>(
        inspectorServer_,
        std::move(socket_),
        doc_root_)->run();
    }

    // Accept another connection
    // do_accept();
  }
};

//------------------------------------------------------------------------------

void WsServerStart(node::inspector::InspectorSocketServer* server, const char*host, unsigned short port)
{
    auto const address = boost::asio::ip::make_address(host);
    std::string const doc_root = ".";

    // The io_context is required for all I/O
    boost::asio::io_context ioc{ 1 };

    // Create and launch a listening port
    std::make_shared<listener>(server, ioc, tcp::endpoint{ address, port }, doc_root)->run();

    ioc.run();
}

void write_ws(std::string message) { ws_session->write(message); }