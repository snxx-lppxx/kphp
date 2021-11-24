// Compiler for PHP (aka KPHP)
// Copyright (c) 2021 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#include <mysql/mysql.h>


#include "runtime/pdo/pdo.h"
#include "runtime/array_functions.h"

class_instance<C$PDO> f$PDO$$__construct(class_instance<C$PDO> const &v$this, const string &dsn,
                                         const Optional<string> &username, const Optional<string> &password, const Optional<array<mixed>> &options) noexcept {
  mysql_init(nullptr);
  (void)username, (void)password; (void)options;
  array<string> dsn_parts = explode(':', dsn);
  php_assert(dsn_parts.count() == 2);
  const auto &driver_name = dsn_parts[0];
  const auto &connection_string = dsn_parts[1];

  if (driver_name == string{"mysql"}) {
    v$this.get()->driver = &vk::singleton<pdo::mysql::MysqlPdoDriver>::get();
  } else {
    php_critical_error("Unknown PDO driver name: %s", driver_name.c_str());
  }

  v$this.get()->driver->connect(v$this, connection_string, username, password, options);

  return v$this;
}

