// Compiler for PHP (aka KPHP)
// Copyright (c) 2021 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include "common/algorithms/hashes.h"
#include "common/wrappers/string_view.h"

#include "runtime/kphp_core.h"
#include "runtime/memory_usage.h"
#include "runtime/refcountable_php_classes.h"
#include "runtime/pdo/abstract_pdo_statement.h"


struct C$PDOStatement : public refcountable_polymorphic_php_classes<abstract_refcountable_php_interface> {
  pdo::AbstractPdoStatement *statement{nullptr};

  virtual ~C$PDOStatement() = default;

  virtual const char *get_class() const noexcept {
    return "PDOStatement";
  }

  virtual int32_t get_hash() const noexcept {
    return static_cast<int32_t>(vk::std_hash(vk::string_view(C$PDOStatement::get_class())));
  }

  virtual void accept(InstanceMemoryEstimateVisitor &) {}
};

bool f$PDOStatement$$execute(const class_instance<C$PDOStatement> &v$this, const Optional<array<mixed>> &params = {}) noexcept;

mixed f$PDOStatement$$fetch(const class_instance<C$PDOStatement> &v$this, int mode = 42, int cursorOrientation = 42, int cursorOffset = 0) noexcept; // TODO: maintain flags and constants
