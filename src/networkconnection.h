#ifndef NETWORK_CONNECTION_H
#define NETWORK_CONNECTION_H

#include <vector>
#include <istream>

#include "asio.hpp"

#include "networkconnection.h"
#include "proto/response.pb.h"
#include "proto/task.pb.h"

using namespace asio::ip;

class Network;

class NetworkConnection
  : public std::enable_shared_from_this<NetworkConnection>
{
  public:
    typedef std::shared_ptr<NetworkConnection> pointer;

    static pointer Create(asio::io_service &, Network *);
    tcp::socket& socket() { return socket_; };
    void Start();

    void SendResponse(std::shared_ptr<asio::streambuf>);

  private:
    NetworkConnection(asio::io_service &, Network *);
    int ReadTask();
    int ParseTask(std::istream &, Task &);

    Network *network_;
    tcp::socket socket_;
    asio::streambuf buffer_;
};

#endif