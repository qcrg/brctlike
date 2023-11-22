#include "netlink.hxx"

#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <cerrno>
#include <stdexcept>
#include <format>
#include <unistd.h>
#include <system_error>

namespace pnd
{
  Socket::Socket()
  {
    fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    if (fd < 0)
      throw std::system_error(std::error_code(errno, std::system_category()),
          "failed to open socket");

    sockaddr_nl addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = getpid();

    if (bind(fd, (sockaddr *)&addr, sizeof(addr)))
      throw std::system_error(std::error_code(errno, std::system_category()),
          "failed to bind socket");
  }

  Socket::~Socket()
  {
    if (fd != -1)
      close(fd);
  }

  void Socket::send(nlmsghdr *h)
  {
    msghdr msg;
    std::memset(&msg, 0, sizeof(msg));
    sockaddr_nl addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    iovec iov = {
      .iov_base = h,
      .iov_len = h->nlmsg_len
    };
    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    int status = sendmsg(fd, &msg, 0);
    if (status < 0)
      throw std::system_error(std::error_code(errno, std::system_category()),
          "failed to send message");
  }

  Response Socket::recv()
  {
    constexpr static const int buf_size = 32768;
    auto nlmsg = std::unique_ptr<nlmsghdr>(reinterpret_cast<nlmsghdr *>(new std::byte[buf_size]));
    msghdr msg;
    sockaddr_nl addr;
    std::memset(&msg, 0, sizeof(msg));
    std::memset(&addr, 0, sizeof(addr));

    addr.nl_family = AF_NETLINK;
    iovec iov = {
      .iov_base = nlmsg.get(),
      .iov_len = buf_size
    };

    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    int status;
    do {
      status = recvmsg(fd, &msg, 0);
    } while (status < 0 && (errno == EAGAIN || errno == EINTR));

    if (status < 0)
      throw std::system_error(std::error_code(errno, std::system_category()),
          "failed to receive message");

    if (msg.msg_flags & MSG_TRUNC)
      throw std::logic_error("message is truncated");

    return {std::move(nlmsg), static_cast<size_t>(status)};
  }

  static inline rtattr *get_tail(nlmsghdr *h)
  {
    return reinterpret_cast<rtattr *>(
        reinterpret_cast<std::byte *>(h) + NLMSG_ALIGN(h->nlmsg_len)
        );
  }

  void add_attr_l(nlmsghdr *h, int type, const void *data, int size)
  {
    int len = RTA_LENGTH(size);
    rtattr *rta = get_tail(h);
    rta->rta_len = len;
    rta->rta_type = type;
    h->nlmsg_len = NLMSG_ALIGN(h->nlmsg_len) + RTA_ALIGN(len);
    if (data)
      std::memcpy(RTA_DATA(rta), data, size);
  }

  void add_attr_32(nlmsghdr *h, int type, int32_t val)
  {
    add_attr_l(h, type, &val, sizeof(val));
  }

  rtattr *add_attr_nest(nlmsghdr *h, int type)
  {
    rtattr *nest = get_tail(h);
    add_attr_l(h, type, nullptr, 0);
    return nest;
  }

  void add_attr_nest_end(nlmsghdr *h, rtattr *nest)
  {
    nest->rta_len = reinterpret_cast<std::byte *>(get_tail(h)) -
      reinterpret_cast<std::byte *>(nest);
  }

  rtattr *get_first_attr(nlmsghdr *header)
  {
    return reinterpret_cast<rtattr *>(
        reinterpret_cast<std::byte *>(header) +
        NLMSG_LENGTH(sizeof(ifinfomsg)));
  }

  rtattr *get_attr(nlmsghdr *header, rtattr *first, int type)
  {
    int attr_buf_len = header->nlmsg_len - 
      (reinterpret_cast<std::byte *>(first) -
      reinterpret_cast<std::byte *>(header));
    if (!RTA_OK(first, attr_buf_len))
      return nullptr;
    while (first->rta_type != type)
    {
      first = RTA_NEXT(first, attr_buf_len);
      if (!RTA_OK(first, attr_buf_len))
        return nullptr;
    }
    return first;
  }

  std::vector<nlmsghdr *> decompose_response(Response &resp)
  {
    nlmsghdr *first = resp.header.get();
    int msg_buf_len = resp.len;
    std::vector<nlmsghdr *> messages;
    messages.reserve(2);
    while (NLMSG_OK(first, msg_buf_len))
    {
      messages.push_back(first);
      first = NLMSG_NEXT(first, msg_buf_len);
    }
    return messages;
  }
} //namespace pnd
