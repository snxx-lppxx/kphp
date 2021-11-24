// Compiler for PHP (aka KPHP)
// Copyright (c) 2021 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include "common/algorithms/hashes.h"
#include "common/wrappers/string_view.h"

#include "runtime/kphp_core.h"
#include "runtime/memory_usage.h"
#include "runtime/pdo/abstract_pdo_driver.h"
#include "runtime/pdo/mysql/mysql_pdo_driver.h"
#include "runtime/refcountable_php_classes.h"

namespace pdo {
  inline void init_pdo_lib() {

  }

  inline void free_pdo_lib() {

  }
} // namespace pdo


struct C$PDO : public refcountable_polymorphic_php_classes<abstract_refcountable_php_interface> {
  const pdo::AbstractPdoDriver *driver{nullptr};
  int connection_id{};

  virtual ~C$PDO() = default;

  virtual const char *get_class() const noexcept {
    return "PDO";
  }

  virtual int32_t get_hash() const noexcept {
    return static_cast<int32_t>(vk::std_hash(vk::string_view(C$PDO::get_class())));
  }

  virtual void accept(InstanceMemoryEstimateVisitor &) {}
};

class_instance<C$PDO> f$PDO$$__construct(class_instance<C$PDO> const &v$this, const string &$dsn, const Optional<string> &username = {},
                                                                const Optional<string> &password = {}, const Optional<array<mixed>> &options= {}) noexcept;
