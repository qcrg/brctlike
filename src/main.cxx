#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if_arp.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <cerrno>
#include <string>
#include <format>
#include <cassert>

const std::string br_name = "br0";

#define NLMSG_TAIL(nmsg) ((rtattr *) ((std::byte *)(nmsg) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

struct request
{
  nlmsghdr n;
  ifinfomsg i;
  char buf[1024];
};

int __rtnl_recvmsg(int fd, msghdr *msg, int flags)
{
  int len;
  do {
    len = recvmsg(fd, msg, flags);
  } while (len < 0 && (errno == EINTR || errno == EAGAIN));

  if (len < 0)
  {
    std::cerr << std::format("netlink receive error {}({})\n",
        strerror(errno), errno);
    return -1;
  }

  if (len == 0)
  {
    std::cerr << "EOF on netlink\n";
    return -1;
  }
  return len;
};

int rtnl_recvmsg(int fd, msghdr *msg, char **answer)
{
  iovec *iov = msg->msg_iov;
  char *buf;
  int len;

  iov->iov_base = nullptr;
  iov->iov_len = 0;

  len = __rtnl_recvmsg(fd, msg, MSG_PEEK | MSG_TRUNC);
  if (len < 32768)
    len = 32768;
  buf = (char *)malloc(len);
  if (!buf)
  {
    std::cerr << "malloc error: not enough buffer\n";
    return -ENOMEM;
  }

  iov->iov_base = buf;
  iov->iov_len = len;

  len = __rtnl_recvmsg(fd, msg, 0);
  if (len < 0)
  {
    free(buf);
    return len;
  }

  if (answer)
    *answer = buf;
  else
    free(buf);
  return len;
}

int main0()
{
  int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (fd == -1)
  {
    std::cerr << "socket(...): " << strerror(errno) << std::endl;
    return -1;
  }

  request req = {
    .n = {
      .nlmsg_len = NLMSG_LENGTH(sizeof(ifinfomsg)),
      .nlmsg_type = RTM_NEWLINK,
      .nlmsg_flags = NLM_F_REQUEST |
        NLM_F_CREATE |
        NLM_F_EXCL,
    },
    .i = {
      .ifi_family = AF_UNSPEC,
      .ifi_index = 0
    }
  };

  rtattr *rta = NLMSG_TAIL(&req.n);
  rta->rta_type = IFLA_IFNAME;
  int alen = br_name.size() + 1;
  int len = RTA_LENGTH(alen);
  rta->rta_len = len;
  ::memcpy(RTA_DATA(rta), br_name.data(), alen);
  req.n.nlmsg_len = NLMSG_ALIGN(req.n.nlmsg_len) + RTA_ALIGN(len);

  rta->rta_len = (std::byte *)NLMSG_TAIL(&req.n) - (std::byte *)rta;

  
  iovec iov = {
    .iov_base = &req.n,
    .iov_len = req.n.nlmsg_len
  };

  sockaddr_nl nladdr;
  std::memset(&nladdr, 0, sizeof(nladdr));
  nladdr.nl_family = AF_NETLINK;
  iovec riov;
  msghdr msg = {
    .msg_name = &nladdr,
    .msg_namelen = sizeof(nladdr),
    .msg_iov = &iov,
    .msg_iovlen = 1
  };

  req.n.nlmsg_seq = 1234;
  req.n.nlmsg_flags |= NLM_F_ACK;

  int status = sendmsg(fd, &msg, 0);
  if (status < 0)
  {
    std::cerr << "Cannot talk to rtnetlink\n";
    return -1;
  }

  msg.msg_iov = &riov;
  msg.msg_iovlen = 1;

  nlmsghdr *response;

  char *buf;
  int seq = 1234;

  for (int i = 0; true;)
  {
next:
    status = rtnl_recvmsg(fd, &msg, &buf);
    ++i;
    
    if (status < 0)
      exit(status);

    if (msg.msg_namelen != sizeof(nladdr))
    {
      std::cerr << std::format("sender address length == {}\n",
          msg.msg_namelen);
      exit(-1);
    }

    for (auto *h = (nlmsghdr *)buf; status >= sizeof(*h); )
    {
      int len = h->nlmsg_len;
      int l = len - sizeof(*h);

      if (l < 0 || len > status)
      {
        if (msg.msg_flags & MSG_TRUNC)
        {
          std::cerr << "Truncate message\n";
          free(buf);
          exit(-1);
        }
        std::cerr << std::format("!!!malformed message: len={}\n",
            len);
        exit(-1);
      }

      if (nladdr.nl_pid != 0 ||
          h->nlmsg_pid != getpid(),
          h->nlmsg_seq > seq || h->nlmsg_seq < seq - 1)
      {
        status -= NLMSG_ALIGN(len);
        h = (nlmsghdr *)((char *)h + NLMSG_ALIGN(len));
        continue;
      }

      if (h->nlmsg_type == NLMSG_ERROR)
      {
        nlmsgerr *err = (nlmsgerr *)NLMSG_DATA(h);
        int error = err->error;

        if (l < sizeof(nlmsgerr))
        {
          std::cerr << "ERROR truncated";
          free(buf);
          exit(-1);
        }

        if (i < 1)
        {
          free(buf);
          goto next;
        }

        if (error)
        {
          free(buf);
          std::cerr << std::format("Response error: {}\n",
            strerror(errno));
          exit(-1);
        }

        response = (nlmsghdr *)buf;
        goto exit_from_loops;
      }
    }
    free(buf);

    if (msg.msg_flags & MSG_TRUNC)
    {
      std::cerr << "Message truncated\n";
      continue;
    }

    if (status)
    {
      std::cerr << std::format("!!!Remnant of size {}\n",
          status);
      exit(-1);
    }
  }
exit_from_loops:


  return 0;
}

int main1()
{
  struct {
    nlmsghdr h;
    ifinfomsg i;
    char attrbuf[1024];
  } req;

  rtattr *rta;
  const char name[] = "br0";
  int name_size = sizeof(name);
  const char type[] = "bridge";
  int type_size = sizeof(type);

  int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  sockaddr_nl local_addr;
  std::memset(&local_addr, 0, sizeof(local_addr));
  local_addr.nl_family = AF_NETLINK;
  assert(bind(sock, (sockaddr *)&local_addr, sizeof(local_addr)) == 0);

  assert(sock);

  std::memset(&req, 0, sizeof(req));
  req.h.nlmsg_len = NLMSG_LENGTH(sizeof(req.i));
  req.h.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK | NLM_F_EXCL;
  req.h.nlmsg_type = RTM_NEWLINK;
  req.h.nlmsg_seq = time(nullptr);
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = 0;

  rta = NLMSG_TAIL(&req.h);
  rta->rta_type = IFLA_IFNAME;
  rta->rta_len = RTA_LENGTH(name_size);
  req.h.nlmsg_len = NLMSG_ALIGN(req.h.nlmsg_len) +
    rta->rta_len;
  std::memcpy(RTA_DATA(rta), name, name_size);

  int alen;
  // rta = RTA_NEXT(rta, alen);
  rta = NLMSG_TAIL(&req.h);
  rta->rta_type = IFLA_LINKINFO;
  rta->rta_len = RTA_LENGTH(0);
  req.h.nlmsg_len = NLMSG_ALIGN(req.h.nlmsg_len) +
    rta->rta_len;
 
  auto nest = rta;

  // rta = RTA_NEXT(rta, alen);
  rta = NLMSG_TAIL(&req.h);
  rta->rta_type = IFLA_INFO_KIND;
  rta->rta_len = RTA_LENGTH(type_size);
  req.h.nlmsg_len = NLMSG_ALIGN(req.h.nlmsg_len) +
    rta->rta_len;
  std::memcpy(RTA_DATA(rta), type, type_size);

  nest->rta_len = (char *)NLMSG_TAIL(&req.h) - (char *)nest;
  // req.h.nlmsg_len = NLMSG_ALIGN(req.h.nlmsg_len) +
  //   nest->rta_len;
  req.h.nlmsg_len = NLMSG_ALIGN(req.h.nlmsg_len);

  msghdr msg;
  sockaddr_nl addr;
  std::memset(&msg, 0, sizeof(msg));
  std::memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;
  iovec iov = {
    .iov_base = &req,
    .iov_len = req.h.nlmsg_len
  };
  msg.msg_name = &addr;
  msg.msg_namelen = sizeof(addr);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  sendmsg(sock, &msg, 0);
  std::memset(&msg, 0, sizeof(msg));
  msg.msg_name = &addr;
  msg.msg_namelen = sizeof(addr);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  recvmsg(sock, &msg, 0);

  return 0;
}

#include "brctl.hxx"

#include <unordered_map>
#include <iostream>
#include <format>
#include <iomanip>
#include <string>
#include <sstream>
#include <stdexcept>
#include <vector>

constexpr size_t hash(const char *arg)
{
  size_t res = 0;
  while (*arg)
  {
    res <<= 1;
    res ^= static_cast<size_t>(*arg);
    arg++;
  }
  return res;
}

void print_usage(std::ostream &out)
{
  auto cmdf = [](const char *cmd, const char *args, const char *desc) {
    std::ostringstream res;
    res << '\t'
      << std::setw(10) << std::left << cmd
      << std::setw(20) << args
      << desc << '\n';
    return res.str();
  };

  const char *br_name = "<bridge>";
  const char *if_args = "<bridge> <device>";

  out <<
    "Usage: brctlike [commands]\n" <<
    "commands:\n" <<
    cmdf("addbr", br_name, "add bridge") <<
    cmdf("delbr", br_name, "delete bridge") <<
    cmdf("addif", if_args, "add interface to bridge") <<
    cmdf("delif", if_args, "delete interface from bridge") <<
    cmdf("show", (std::string("[ ") + br_name + " ]").data(),
        "show a list of bridges");
}

std::string_view get_br_name(int argc, const char *const *argv)
{
  if (argc < 3)
    throw std::invalid_argument(
        "bridge name is not set");
  return argv[2];
}

std::vector<std::string_view> get_br_names(int argc, const char *const *argv)
{
  (void)argc;
  std::vector<std::string_view> names;
  argv += 2;
  while (*argv)
  {
    names.emplace_back(*argv);
    argv++;
  }
  return names;
}

std::string_view get_if_name(int argc, const char *const *argv)
{
  if (argc < 4)
    throw std::invalid_argument(
        "interface name is not set");
  return argv[3];
}

int main3(int argc, const char *const *argv)
{
  using namespace pnd;

  brctl ctl;

  if (argc < 2)
  {
    print_usage(std::cerr);
    return -1;
  }

  try {
    switch (hash(argv[1]))
    {
      case hash("addbr"):
        ctl.addbr(get_br_name(argc, argv));
      break;

      case hash("delbr"):
        ctl.delbr(get_br_name(argc, argv));
      break;

      case hash("addif"):
        ctl.addif(get_br_name(argc, argv), get_if_name(argc, argv));
      break;

      case hash("delif"):
        ctl.delif(get_br_name(argc, argv), get_if_name(argc, argv));
      break;

      case hash("show"):
        ctl.show(get_br_names(argc, argv));
      break;

      default:
        std::cerr << "Undefined command [" << argv[1] << "]\n";
        print_usage(std::cerr);
        return -1;
    }

  } catch (const std::exception &ex) {
    std::cerr << ex.what() << "\n";
    if (typeid(ex) == typeid(std::invalid_argument))
      print_usage(std::cerr);
    return -1;
  }

  return 0;
}

int main(int argc, const char *const *argv)
{
  return main3(argc, argv);
}
