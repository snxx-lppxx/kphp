// Compiler for PHP (aka KPHP)
// Copyright (c) 2021 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include "common/mixin/not_copyable.h"
#include "common/smart_ptrs/singleton.h"
#include "runtime/kphp_core.h"

struct C$PDO;

namespace pdo {

class AbstractPdoDriver : vk::not_copyable {
public:
  AbstractPdoDriver() = default;
  virtual void connect(const class_instance<C$PDO> &pdo_instance, const string &connection_string,
                       const Optional<string> &username, const Optional<string> &password, const Optional<array<mixed>> &options) const = 0;
};

} // namespace pdo
