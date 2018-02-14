#ifndef NETWORK_CONNECTION_H
#define NETWORK_CONNECTION_H

#include "asio.hpp"

#include "amxexecutor.h"
#include "networkconnection.h"

using namespace asio::ip;

class NetworkConnection
  : public std::enable_shared_from_this<NetworkConnection>
{
  public:
    typedef std::shared_ptr<NetworkConnection> pointer;

    static pointer create(asio::io_service &, AMXExecutor *);

    tcp::socket& socket() { return socket_; };

    void start();

  private:
    NetworkConnection(asio::io_service &, AMXExecutor *);

    tcp::socket socket_;
    AMXExecutor *executor_;
};

#endif