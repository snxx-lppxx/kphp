// Compiler for PHP (aka KPHP)
// Copyright (c) 2020 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#include "server/external-net-drivers/external-net-drivers.h"
#include "server/external-net-drivers/connector.h"
#include "server/php-queries-types.h"
#include "server/php-runner.h"

int ExternalNetDrivers::register_connector(Connector *connector) {
  int id = static_cast<int>(connectors.count());
  connectors.push_back(connector);
  return id;
}

void ExternalNetDrivers::create_outbound_connections() {
  // use const iterator to prevent unexpected mutate() and allocations
  for (auto it = connectors.cbegin(); it != connectors.cend(); ++it) {
    Connector *connector = it.get_value();
    if (!connector->connected()) {
      connector->connect_to_reactor_async();
    }
  }
}

php_query_connect_answer_t *ExternalNetDrivers::php_query_connect(Connector *connector) {
  assert (PHPScriptBase::is_running);

  //DO NOT use query after script is terminated!!!
  external_driver_connect q{connector};

  PHPScriptBase::current_script->ask_query(&q);

  return (php_query_connect_answer_t *)q.ans;
}


void ExternalNetDrivers::reset() { // TODO: call
  hard_reset_var(connectors);
}

int ExternalNetDrivers::epoll_gateway(int fd, void *data, event_t *ev) {
  (void)fd;
  (void)data;
  (void)ev;
  return 42;
}
