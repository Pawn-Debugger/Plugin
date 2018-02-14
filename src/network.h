#ifndef NETWORK_H
#define NETWORK_H

#include <thread>

#include "asio.hpp"

#include "amxexecutor.h"
#include "networkconnection.h"

class Network {
 public:
  Network(AMXExecutor* executor);

  void start();
  void stop();
 private:
  void start_accept();
  void handle_accept(NetworkConnection::pointer, const std::error_code&);

  std::thread network_thread_;
  asio::io_service server_io_;
  asio::ip::tcp::acceptor acceptor_;
  AMXExecutor* executor_;
};

#endif