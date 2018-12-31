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

  static pointer create(boost::asio::io_service& io_service, int port);
  boost::asio::ip::tcp::socket& socket();
  inline int port() { return port_; }

  typedef void(*ReadCallback)(std::vector<char>&, void*data);

  inline void registerReadCallback(ReadCallback callback) { readcallback_ = callback; }
  inline void registerReadCallbackData(void*data) { callbackData_ = data; }

  void read_loop_async();
  void write_async(std::vector<char>);
  void do_write(bool cont);

  void start();

  void handle_accept(const boost::system::error_code& err);
  void handle_read(const boost::system::error_code& ec, std::size_t bytes_transferred);
  void handle_write(const boost::system::error_code& ec,
    std::size_t bytes_transferred);

private:
  inline tcp_connection(boost::asio::io_service& io_service, int port)
    : socket_(io_service), port_(port), acceptor_(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0)){}

  //void handle_write(const boost::system::error_code& /*error*/,
  //  size_t /*bytes_transferred*/);

  // boost::asio::io_service io_service_;

  int port_;

  boost::asio::ip::tcp::acceptor acceptor_;

  boost::asio::ip::tcp::socket socket_;
  std::string message_;
  
  /// Buffer for incoming data.
  std::array<char, 8192> buffer_;

  void* callbackData_;
  ReadCallback readcallback_;

  std::mutex queueAccessMutex;
  std::queue<std::vector<char>> outQueue;

  std::vector<char> messageToWrite_;
  bool writing_;
};

