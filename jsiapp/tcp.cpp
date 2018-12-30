#include "tcp.h"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include "logger.h"

/*static*/ tcp_connection::pointer tcp_connection::create(boost::asio::io_service& io_service)
{
  return pointer(new tcp_connection(io_service));
}

boost::asio::ip::tcp::socket& tcp_connection::socket()
{
  return socket_;
}

void tcp_connection::accept_sync() {
  boost::asio::ip::tcp::acceptor acceptor(socket_.get_io_service(), boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port_));
  acceptor.accept();
  Logger::log("Connection accepted;");
}

void tcp_connection::read_loop_async() {
  // Start an asynchronous operation to read a newline-delimited message.
  boost::asio::async_read(socket_, input_buffer_,
    boost::bind(&handle_read, this, _1));
}

void tcp_connection::handle_read(const boost::system::error_code& ec) {
  
  if (ec)
    return std::abort();

  std::vector<char> vc;
  const char* start = boost::asio::buffer_cast<const char*>(input_buffer_.data());
  vc.reserve(input_buffer_.size());
  for (int c = 0; c < vc.size(); c++) {
    vc.push_back(start[c]);
  }

  std::string s(boost::asio::buffer_cast<const char*>(input_buffer_.data()), input_buffer_.size());
  Logger::log("Socket receive :: ", s);

  input_buffer_.consume(input_buffer_.size());

  std::vector<char> vc;
  std::transform(s.begin(), s.end(), std::back_inserter(vc), [](char c) { return c; });

  // COPY!!
  readcallback_(vc, callbackData_);

  read_loop_async();
}

void tcp_connection::handle_write(const boost::system::error_code& ec, std::size_t bytes_transferred) {
  if (ec)
    return std::abort();

  std::ostringstream stream;
  stream << "Writing completed .. " << bytes_transferred << " bytes.";
  Logger::log(stream.str());

  do_write(true);
}

// !!COPY
void tcp_connection::write_async(std::vector<char> message_) {
  
  {
    std::lock_guard<std::mutex> guard(queueAccessMutex);
    outQueue.push(std::move(message_));
  }

  do_write(false);
}

void tcp_connection::do_write(bool cont) {

  std::vector<char> message;
  {
    std::lock_guard<std::mutex> guard(queueAccessMutex);

    // New message but the last write is going on.
    if (!cont && writing_) return;

    if (outQueue.empty()) {
      writing_ = false;
      return;
    }
    message = outQueue.front();
    outQueue.pop();

    writing_ = true;
  }

  boost::asio::async_write(socket_, boost::asio::buffer(message_),
    boost::bind(&tcp_connection::handle_write, shared_from_this(),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred));

}
