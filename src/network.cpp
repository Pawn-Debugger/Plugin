#include <iostream>
#include <thread>
#include <functional>
#include <vector>

#include "asio.hpp"

#include "amxexecutor.h"
#include "network.h"
#include "networkconnection.h"
#include "proto/task.pb.h"
#include "proto/response.pb.h"

using namespace asio::ip;

namespace {

inline std::shared_ptr<asio::streambuf> SerializeResponse(Response &response) {
  auto b = std::make_shared<asio::streambuf>();
  std::ostream os(b.get());
  response.SerializeToOstream(&os);
  return b;
}

}

Network::Network()
  : acceptor_(server_io_, tcp::endpoint(tcp::v4(), 7667))
{
  StartAccept();
}

void Network::Start() {
  // `io_service` has two templates for `run` and can't pass it directly without ugly casting
  auto bound = [this] { return server_io_.run(); };
  network_thread_ = std::thread(bound);
}

void Network::Stop() {
  server_io_.stop();
  network_thread_.join();
  server_io_.reset();
}

void Network::StartAccept() {
  NetworkConnection::pointer new_connection = NetworkConnection::Create(acceptor_.get_io_service(), this);

  acceptor_.async_accept(new_connection->socket(),
    std::bind(&Network::HandleAccept, this, new_connection, std::placeholders::_1));
}

void Network::HandleAccept(NetworkConnection::pointer connection, const std::error_code &error) {
  if (error) return;

  connections_.push_back(connection);

  connection->Start();
  StartAccept();
}

void Network::EndConnection(NetworkConnection::pointer connection) {
  connections_.erase(std::remove(connections_.begin(), connections_.end(), connection), connections_.end());
}

Task Network::GetTask() {
  return pending_tasks_.dequeue();
}

bool Network::HasTask() {
  return !pending_tasks_.empty();
}

void Network::AddTask(Task task) {
  pending_tasks_.enqueue(task);
}

void Network::SendSuccess() {
  Response response;
  response.set_type(Response::SUCCESS);
  SendResponseToAll(response);
}

void Network::SendRegisters(AMXScript amx) {
  Response response;
  response.set_type(Response::REGISTERS);

  Response::Registers *registers = response.mutable_registers();

  registers->set_pri(amx.GetPri());
  registers->set_alt(amx.GetAlt());
  registers->set_hlw(amx.GetHlw());
  registers->set_hea(amx.GetHea());
  registers->set_stp(amx.GetStp());
  registers->set_stk(amx.GetStk());
  registers->set_frm(amx.GetFrm());
  registers->set_cip(amx.GetCip());

  AMX_HEADER *header = amx.GetHeader();
  registers->set_cod(*(amx.amx()->base + header->cod));
  registers->set_dat(*(amx.amx()->base + header->dat));

  SendResponseToAll(response);
}

void Network::SendResponseToAll(Response &response) {
  std::shared_ptr<asio::streambuf> serializedBuffer = SerializeResponse(response);

  for (auto &connection : connections_) {
    connection->SendResponse(serializedBuffer);
  }
}

void Network::SendConfusion() {

}