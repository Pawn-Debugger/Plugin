#include <array>
#include <iostream>

#include "asio.hpp"

#include "amxexecutor.h"
#include "networkconnection.h"

inline std::uint32_t bytes_to_uint(const std::array<unsigned char, 4> &bytes) {
  return bytes[0] << 24 |
         bytes[1] << 16 |
         bytes[2] << 8 |
         bytes[3];
}

NetworkConnection::NetworkConnection(asio::io_service& io_service, AMXExecutor *executor)
  : socket_(io_service),
    executor_(executor)
{
}

NetworkConnection::pointer NetworkConnection::create(asio::io_service& io_service, AMXExecutor *executor) {
  return NetworkConnection::pointer(new NetworkConnection(io_service, executor));
}

void NetworkConnection::start() {
  auto self(shared_from_this());
  std::array<unsigned char, 4> expected_bytes_bytes;
  asio::error_code error_code;
  size_t bytes_transferred = 0;

  while (true) {
    bytes_transferred = asio::read(socket_, asio::buffer(expected_bytes_bytes), asio::transfer_exactly(4), error_code);
    
    if (error_code == asio::error::eof) {
      return;
    } else if (error_code) {
      std::cerr << "Error: " << error_code.message() << std::endl;
      continue;
    }

    std::uint32_t excected_bytes = bytes_to_uint(expected_bytes_bytes);
    std::cout << "Expecting " << excected_bytes << " bytes" << std::endl;

    executor_->set_stopped(false);
  }
}