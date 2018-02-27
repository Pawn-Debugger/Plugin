#ifndef NETWORK_H
#define NETWORK_H

#include <thread>

#include "asio.hpp"

#include "amxexecutor.h"
#include "networkconnection.h"
#include "safequeue.h"
#include "proto/task.pb.h"
#include "proto/response.pb.h"

class Network {
 public:
  Network();

  void Start();
  void Stop();
  Task GetTask();
  void AddTask(Task);
  bool HasTask();
  void EndConnection(NetworkConnection::pointer);

  void SendSuccess();
  void SendRegisters(AMXScript amx);
  void SendConfusion();
 private:
  void StartAccept();
  void HandleAccept(NetworkConnection::pointer, const std::error_code&);
  void SendResponseToAll(Response &);

  SafeQueue<Task> pending_tasks_;
  std::thread network_thread_;
  asio::io_service server_io_;
  asio::ip::tcp::acceptor acceptor_;
  std::vector<NetworkConnection::pointer> connections_; 
};

#endif