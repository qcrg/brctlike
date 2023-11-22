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
  private:
    brctl_impl *impl;
  };
} //namespace pnd
