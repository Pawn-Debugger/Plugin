#include <array>
#include <iostream>

#include "asio.hpp"

#include "amxexecutor.h"
#include "network.h"
#include "networkconnection.h"
#include "proto/task.pb.h"
#include "proto/response.pb.h"

namespace {
inline std::uint32_t BytesToUInt(const std::array<unsigned char, 4> &bytes) {
  return bytes[0] << 24 |
         bytes[1] << 16 |
         bytes[2] << 8 |
         bytes[3];
}

inline std::array<unsigned char, 4> UIntToBytes(const std::uint32_t value) {
  std::array<unsigned char, 4> bytes;
  for (size_t i = 0; i != 4; ++i) {
    bytes[3 - i] = value >> (i * 8);
  }

  return bytes;
}
}

NetworkConnection::NetworkConnection(asio::io_service& io_service, Network *network)
  : socket_(io_service),
    network_(network)
{
}

NetworkConnection::pointer NetworkConnection::Create(asio::io_service& io_service, Network *network) {
  return NetworkConnection::pointer(new NetworkConnection(io_service, network));
}

void NetworkConnection::Start() {
  while (true) {
    if (ReadTask()) break;
  }
}

int NetworkConnection::ReadTask() {
  std::array<unsigned char, 4> expected_bytes_bytes;
  asio::error_code error_code;
  size_t bytes_transferred = 0;

  bytes_transferred = asio::read(socket_, asio::buffer(expected_bytes_bytes), asio::transfer_exactly(4), error_code);
  
  if (error_code) {
    std::cerr << "Error: " << error_code.message() << std::endl;

    return 1;
  }

  std::uint32_t expected_bytes = BytesToUInt(expected_bytes_bytes);

  bytes_transferred = asio::read(socket_, buffer_, asio::transfer_exactly(expected_bytes), error_code);

  if (error_code) {
    std::cerr << "Error: " << error_code.message() << std::endl;

    return 1;
  }

  Task task;
  if (!task.ParseFromIstream(&std::istream(&buffer_))) {
    std::cerr << "Could not parse task" << std::endl;

    return 1; 
  }

  network_->AddTask(task);

  return 0;
}

void NetworkConnection::SendResponse(std::shared_ptr<asio::streambuf> response) {
  std::error_code ec;

  asio::write(socket_, asio::buffer(UIntToBytes((*response).size())), ec);
  asio::write(socket_, *response, ec);
}