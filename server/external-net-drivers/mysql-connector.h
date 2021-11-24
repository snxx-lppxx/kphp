// Compiler for PHP (aka KPHP)
// Copyright (c) 2020 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include <mysql/mysql.h>

#include "runtime/kphp_core.h"
#include "server/external-net-drivers/connector.h"

class MysqlConnector : public Connector {
public:
  MYSQL *ctx{};

  MysqlConnector(MYSQL *ctx, string host, string user, string password, string db_name, int port)
    : Connector()
    , ctx(ctx)
    , host(std::move(host))
    , user(std::move(user))
    , password(std::move(password))
    , db_name(std::move(db_name))
    , port(port) {}

  bool connect_async() noexcept final {
    if (is_connected) {
      return true;
    }
    net_async_status status = mysql_real_connect_nonblocking(ctx, host.c_str(), user.c_str(), password.c_str(), db_name.c_str(), port, nullptr, 0);
    return is_connected = (status == NET_ASYNC_COMPLETE);
  }

  void close() noexcept final {
    mysql_close(ctx);
  }

  int get_fd() const noexcept final {
    if (!is_connected) {
      return -1;
    }
    return ctx->net.fd;
  }

private:
  string host{};
  string user{};
  string password{};
  string db_name{};
  int port{};
};
