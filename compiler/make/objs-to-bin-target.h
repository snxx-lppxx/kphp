// Compiler for PHP (aka KPHP)
// Copyright (c) 2020 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

class Objs2BinTarget : public Target {
public:
  explicit Objs2BinTarget(bool need_libdl = false) noexcept :
    need_libdl_{need_libdl} {
  }

  string get_cmd() final {
#if defined(__APPLE__)
    vk::string_view open_dep{" -Wl,-all_load "};
    vk::string_view close_dep;
#else
    vk::string_view open_dep{" -Wl,--whole-archive -Wl,--start-group "};
    vk::string_view close_dep{" -Wl,--end-group -Wl,--no-whole-archive "};
#endif
    std::stringstream ss;
    ss << settings->cxx.get() << " -o " << target() << open_dep << dep_list() << close_dep << settings->ld_flags.get();
    if (need_libdl_) {
      ss << " -ldl";
    }
    return ss.str();
  }

private:
  bool need_libdl_{false};
};
