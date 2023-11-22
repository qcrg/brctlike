#pragma once

#include <string_view>
#include <vector>
#include <memory>

namespace pnd
{
  struct brctl_impl;

  class brctl
  {
  public:
    brctl();
    ~brctl();
    void show(const std::vector<std::string_view> &br_names = {});
    void addbr(std::string_view br_name);
    void delbr(std::string_view br_name);
    void addif(std::string_view br_name,
        std::string_view device_name);
    void delif(std::string_view br_name,
        std::string_view device_name);
  private:
    brctl_impl *impl;
  };
} //namespace pnd
