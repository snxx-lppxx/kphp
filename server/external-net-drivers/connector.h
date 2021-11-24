// Compiler for PHP (aka KPHP)
// Copyright (c) 2021 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include "server/external-net-drivers/external-net-drivers.h"
#include "net/net-events.h"

class Connector : public ManagedThroughDlAllocator {
public:
  virtual void close() noexcept = 0;
  virtual int get_fd() const noexcept = 0;

  bool connected() const noexcept {
    return is_connected;
  }

  void connect_to_reactor_async() noexcept {
    if (connected()) {
      return;
    }
    is_connected = connect_async();
    if (is_connected) {
      int fd = get_fd();
      epoll_insert(fd, EVT_WRITE | EVT_SPEC);
      epoll_sethandler(fd, 0, ExternalNetDrivers::epoll_gateway, nullptr);
    }
  }

protected:
  bool is_connected{};

private:
  virtual bool connect_async() noexcept = 0;
};
