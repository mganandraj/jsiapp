#include "stdafx.h"

#include "tcp.h"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include "logger.h"

/*static*/ tcp_connection::pointer tcp_connection::create(boost::asio::io_service& io_service, int port)
{
  return pointer(new tcp_connection(io_service, port));
}

boost::asio::ip::tcp::socket& tcp_connection::socket()
{
  return socket_;
}

void tcp_connection::start() {
  acceptor_.async_accept(socket_,
    boost::bind(&tcp_connection::handle_accept, shared_from_this(),
      boost::asio::placeholders::error));
}

void tcp_connection::handle_accept(const boost::system::error_code& ec)
{
  if (ec)
  {
    Logger::log("accept failed.");
    start();
    // std::abort();
  }
  else {
    Logger::log("Connection accepted;");
    read_loop_async();
  }
}

void tcp_connection::read_loop_async() {
 /* boost::asio::async_read(socket_, input_buffer_,
    boost::bind(&tcp_connection::handle_read, shared_from_this(),
      boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));*/
  auto self(shared_from_this());
  socket_.async_read_some(boost::asio::buffer(buffer_),
    [this, self](boost::system::error_code ec, std::size_t bytes_transferred)
  {
    if (!ec)
    {
      /*request_parser::result_type result;
      std::tie(result, std::ignore) = request_parser_.parse(
        request_, buffer_.data(), buffer_.data() + bytes_transferred);

      if (result == request_parser::good)
      {
        request_handler_.handle_request(request_, reply_);
        do_write();
      }
      else if (result == request_parser::bad)
      {
        reply_ = reply::stock_reply(reply::bad_request);
        do_write();
      }
      else
      {
        do_read();
      }*/
      std::vector<char> vc;
      //const char* start = boost::asio::buffer_cast<const char*>(input_buffer_.data());
      vc.reserve(bytes_transferred);
      for (int c = 0; c < bytes_transferred; c++) {
        vc.push_back(buffer_.data()[c]);
      }

      std::string s((buffer_.data()), bytes_transferred);
      Logger::log("Socket receive :: ", s);

      readcallback_(vc, callbackData_);

      self->read_loop_async();
    }
    else if (ec == boost::asio::error::operation_aborted)
    {
      std::abort();
    }
    else
    {
      std::abort();
    }
  });
}

void tcp_connection::handle_read(const boost::system::error_code& ec, std::size_t bytes_transferred) {
  
//  if (ec)
//    return std::abort();
//
//  std::vector<char> vc;
//  const char* start = boost::asio::buffer_cast<const char*>(input_buffer_.data());
//  vc.reserve(input_buffer_.size());
//  for (int c = 0; c < vc.size(); c++) {
//    vc.push_back(start[c]);
//  }
//
//  std::string s(boost::asio::buffer_cast<const char*>(input_buffer_.data()), input_buffer_.size());
//  Logger::log("Socket receive :: ", s);
//
//  input_buffer_.consume(input_buffer_.size());
//
//  /*std::vector<char> vc;
//  std::transform(s.begin(), s.end(), std::back_inserter(vc), [](char c) { return c; });
//*/
//  // COPY!!
//  readcallback_(vc, callbackData_);
//
//  read_loop_async();
}

//void tcp_connection::handle_write(const boost::system::error_code& ec, std::size_t bytes_transferred) {
//  if (ec)
//    return std::abort();
//
//  std::ostringstream stream;
//  stream << "Writing completed .. " << bytes_transferred << " bytes.";
//  Logger::log(stream.str());
//
//  do_write(true);
//}

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

  auto self(shared_from_this());

  messageToWrite_ = std::move(message);

  std::string str;
  std::transform(messageToWrite_.begin(), messageToWrite_.end(), std::back_inserter(str), [](char c) { return c; });
  Logger::log("Writing: ", str);

  socket_.async_send(boost::asio::buffer(messageToWrite_),
    [this, self](boost::system::error_code ec, std::size_t bytes_transferred)
  {
    if (!ec)
    {
      std::ostringstream stream;
      stream << "Writing completed .. " << bytes_transferred << " bytes.";
      Logger::log(stream.str());

      self->do_write(true);
    }

    if (ec != boost::asio::error::operation_aborted)
    {
      // connection_manager_.stop(shared_from_this());
    }
  });

  /*boost::asio::async_write(socket_, boost::asio::buffer(message_),
    boost::bind(&tcp_connection::handle_write, shared_from_this(),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred));*/

}
