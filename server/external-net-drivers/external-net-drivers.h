// Compiler for PHP (aka KPHP)
// Copyright (c) 2020 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include "common/smart_ptrs/singleton.h"
#include "common/mixin/not_copyable.h"
#include "net/net-events.h"
#include "runtime/kphp_core.h"

struct php_query_connect_answer_t;

class Connector;

class ExternalNetDrivers : vk::not_copyable {
public:
  array<Connector *> connectors; // invariant: this array is empty <=> script allocator is disabled

  int register_connector(Connector *connector);
  void create_outbound_connections();
  php_query_connect_answer_t *php_query_connect(Connector *connector);
  void reset();

  static int epoll_gateway(int fd, void *data, event_t *ev);
private:
  ExternalNetDrivers() = default;
  friend class vk::singleton<ExternalNetDrivers>;
};
