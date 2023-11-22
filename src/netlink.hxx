#pragma once

#include <vector>
#include <memory>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

namespace pnd
{
  void add_attr_l(nlmsghdr *h, int type, const void *data, int size);
  void add_attr_32(nlmsghdr *h, int type, int32_t val);
  rtattr *add_attr_nest(nlmsghdr *h, int type);
  void add_attr_nest_end(nlmsghdr *h, rtattr *nest);

  struct Response
  {
    std::unique_ptr<nlmsghdr> header;
    size_t len;
  };

  rtattr *get_first_attr(nlmsghdr *header);
  rtattr *get_attr(nlmsghdr *header, rtattr *first, int type);
  std::vector<nlmsghdr *> decompose_response(Response &resp);

  struct Socket
  {
    Socket();
    ~Socket();

    void send(nlmsghdr *h);
    Response recv();

    int fd;
  };

} //namespace pnd
