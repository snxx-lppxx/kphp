// Compiler for PHP (aka KPHP)
// Copyright (c) 2021 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include "runtime/kphp_core.h"
#include "runtime/pdo/abstract_pdo_driver.h"


namespace pdo::mysql {

class MysqlPdoDriver : public pdo::AbstractPdoDriver {
public:
  void connect(const class_instance<C$PDO> &pdo_instance, const string &connection_string,
               const Optional<string> &username, const Optional<string> &password, const Optional<array<mixed>> &options) const noexcept final;

private:
  MysqlPdoDriver() = default;
  friend class vk::singleton<MysqlPdoDriver>;
};

} // namespace pdo::mysql
