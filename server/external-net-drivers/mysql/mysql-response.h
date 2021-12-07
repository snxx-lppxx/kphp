// Compiler for PHP (aka KPHP)
// Copyright (c) 2021 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include <mysql/mysql.h>

#include "server/external-net-drivers/response.h"

class MysqlConnector;

class MysqlResponse : public Response {
public:
  MysqlConnector *connector{nullptr};
  MYSQL_RES *res{nullptr};

  bool fetch() noexcept final;
};
