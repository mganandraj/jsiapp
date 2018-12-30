#pragma once

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include <queue>

class tcp_connection
  : public boost::enable_shared_from_this<tcp_connection>
{
public:
  typedef boost::shared_ptr<tcp_connection> pointer;

  static pointer create(boost::asio::io_service& io_service);
  boost::asio::ip::tcp::socket& socket();
  inline int port() { return port_; }

  void accept_sync();

  typedef void(*ReadCallback)(std::vector<char>, void*data);

  inline void registerReadCallback(ReadCallback callback) { readcallback_ = callback; }
  inline void registerReadCallbackData(void*data) { callbackData_ = data; }

  void read_loop_async();
  void write_async(std::vector<char>);
  void do_write(bool cont);

  void handle_read(const boost::system::error_code& ec);
  void handle_write(const boost::system::error_code& ec,
    std::size_t bytes_transferred);

private:
  inline tcp_connection(boost::asio::io_service& io_service)
    : socket_(io_service) {}

  void handle_write(const boost::system::error_code& /*error*/,
    size_t /*bytes_transferred*/);

  // boost::asio::io_service io_service_;

  int port_;
  boost::asio::ip::tcp::socket socket_;
  std::string message_;
  
  boost::asio::streambuf input_buffer_;

  void* callbackData_;
  ReadCallback readcallback_;

  std::queue<std::vector<char>> outQueue;

  std::mutex queueAccessMutex;
  std::queue<std::vector<char>> outQueue;

  bool writing_;
};

