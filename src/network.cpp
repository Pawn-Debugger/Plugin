#include <iostream>
#include <thread>
#include <chrono>
#include <functional>

#include "asio.hpp"

#include "amxexecutor.h"
#include "network.h"
#include "networkconnection.h"

using namespace asio::ip;

Network::Network(AMXExecutor* executor)
  : executor_(executor),
    acceptor_(server_io_, tcp::endpoint(tcp::v4(), 7667))
{
  start_accept();
}

void Network::start() {
  // `io_service` has two templates for `run` and can't pass it directly without ugly casting
  auto bound = [this] { return server_io_.run(); };
  network_thread_ = std::thread(bound);
}

void Network::stop() {
  server_io_.stop();
  network_thread_.join();
  server_io_.reset();
}

void Network::start_accept() {
  NetworkConnection::pointer new_connection = NetworkConnection::create(acceptor_.get_io_service(), executor_);

  acceptor_.async_accept(new_connection->socket(),
    std::bind(&Network::handle_accept, this, new_connection, std::placeholders::_1));
}

void Network::handle_accept(NetworkConnection::pointer connection, const std::error_code &error) {
  if (error) return;

  connection->start();
  start_accept();
}