#include "brctl.hxx"
#include "netlink.hxx"

#include <cassert>
#include <cstring>
#include <sys/socket.h>
#include <cstring>
#include <system_error>
#include <format>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <fstream>
#include <net/if.h>
#include <iomanip>

namespace pnd
{
  struct request
  {
    nlmsghdr h;
    ifinfomsg i;
    char buf[512];
    
    void clear();
    static request get_default();
  };

  struct brctl_impl
  {
    void addbr(std::string_view br_name);
    void delbr(std::string_view br_name);
    void addif(std::string_view br_name,
        std::string_view device_name);
    void delif(std::string_view br_name,
        std::string_view device_name);
    void show(const std::vector<std::string_view> &br_names);

  private:
    bool is_bridge(std::string_view br_name);
    bool is_bridge(const std::filesystem::path &br_path);
    void check_nlmsgerr(const std::unique_ptr<nlmsghdr> &header, const char *what_msg);

    Socket sock;
  };

  brctl::brctl()
    : impl{new brctl_impl}
  {
    assert(impl);

  }

  brctl::~brctl()
  {
    if (impl)
      delete impl;
  }

  void brctl::show(const std::vector<std::string_view> &br_names)
  {
    impl->show(br_names);
  }

  void brctl::addbr(std::string_view br_name)
  {
    impl->addbr(br_name);
  }

  void brctl::delbr(std::string_view br_name)
  {
    impl->delbr(br_name);
  }

  void brctl::addif(std::string_view br_name,
      std::string_view device_name)
  {
    impl->addif(br_name, device_name);
  }

  void brctl::delif(std::string_view br_name,
      std::string_view device_name)
  {
    impl->delif(br_name, device_name);
  }

  void brctl_impl::addbr(std::string_view br_name)
  {
    static const char type[] = "bridge";
    request req = request::get_default();

    req.h.nlmsg_flags |= NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    req.h.nlmsg_type = RTM_NEWLINK;

    add_attr_l(&req.h, IFLA_IFNAME, br_name.data(), br_name.size());
    rtattr *nest = add_attr_nest(&req.h, IFLA_LINKINFO);
    add_attr_l(&req.h, IFLA_INFO_KIND, type, sizeof(type));
    add_attr_nest_end(&req.h, nest);
    req.h.nlmsg_len = NLMSG_ALIGN(req.h.nlmsg_len);

    sock.send(&req.h);
    Response resp = sock.recv();
    check_nlmsgerr(resp.header, "failed to add bridge");
  }

  void brctl_impl::delbr(std::string_view br_name)
  {
    if (!is_bridge(br_name))
      throw std::logic_error(std::format("interface is not a bridge: {}", br_name));
    request req = request::get_default();
    req.h.nlmsg_flags |= NLM_F_ACK;
    req.h.nlmsg_type = RTM_DELLINK;
    add_attr_l(&req.h, IFLA_IFNAME, br_name.data(), br_name.size());
    sock.send(&req.h);
    Response resp = sock.recv();
    check_nlmsgerr(resp.header, "failed to delete bridge");
  }

  void brctl_impl::addif(std::string_view br_name,
      std::string_view device_name)
  {
    throw std::logic_error("Not impl");
  }

  void brctl_impl::delif(std::string_view br_name,
      std::string_view device_name)
  {
    throw std::logic_error("Not impl");
  }

  void brctl_impl::show(const std::vector<std::string_view> &br_names)
  {
    for (const auto &br : br_names)
      try {
        if (!is_bridge(br))
          std::cerr << "interface is not bridge: " << br << "\n";
      } catch (const std::system_error &ex) {
        std::cerr << "interface not found: " << br;
      }

    struct Bridge {
      std::string id;
      bool stp_enabled;
      std::vector<std::string> eth_ifs;
    };
    namespace sf = std::filesystem;
    const sf::path ifs_path("/sys/class/net");
    std::unordered_map<std::string, Bridge> bridges;

    auto get_name = [](const sf::path &if_path) {
      char buf[IF_NAMESIZE];
      std::ifstream in(if_path/"ifindex");
      int id;
      in >> id;
      assert(if_indextoname(id, buf) != nullptr);
      return std::string(buf);
    };
    auto get_bridge_id = [](const sf::path &if_path) {
      std::ifstream in(if_path/"bridge/bridge_id");
      std::string id;
      in >> id;
      return id;
    };
    auto get_stp = [](const sf::path &if_path) {
      std::ifstream in(if_path/"bridge/stp_state");
      bool has_stp;
      in >> has_stp;
      return has_stp;
    };

    for (const auto &entry : sf::directory_iterator{ifs_path})
    {
      sf::path path = entry.path();
      sf::path master_path = path/"master";
      if (is_bridge(path))
      {
        Bridge &br = bridges[get_name(path)];
        br.id = get_bridge_id(path);
        br.stp_enabled = get_stp(path);
      }
      else if (sf::exists(master_path) && is_bridge(master_path))
        bridges[get_name(master_path)].eth_ifs.push_back(get_name(path));
    }

    if (bridges.empty())
      return;

    auto print_line = [](const auto &name, const auto &id,
        const auto &stp, const auto &interfaces) {
      std::cout << std::left
        << std::setw(IF_NAMESIZE + 2) << name
        << std::setw(19) << id
        << std::setw(16) << stp
        << std::setw(10) << interfaces
        << "\n";
    };
    auto print_bridge = [&print_line](const std::string &name,
        const Bridge &data) {
      std::string ifs;
      bool first = true;
      for (const auto &eth_name : data.eth_ifs)
      {
        if (first)
          first = false;
        else
          ifs.append(" ");
        ifs.append(eth_name);
      }
      print_line(name, data.id, data.stp_enabled ? "yes" : "no", ifs);
    };

    print_line("bridge name", "bridge id", "STP enabled", "interfaces");

    if (!br_names.empty())
    {
      for (std::string_view br_name : br_names)
      {
        auto it = bridges.find(std::string(br_name));
        if (it != bridges.end())
          print_bridge(it->first, it->second);
      }
      return;
    }

    for (const auto &br : bridges)
      print_bridge(br.first, br.second);
  }

  bool brctl_impl::is_bridge(std::string_view br_name)
  {
    using namespace std::filesystem;
    path dir(std::format("/sys/class/net/{}/bridge", br_name));
    path if_path(std::format("/sys/class/net/{}", br_name));
    if (!exists(if_path))
      throw std::system_error(std::error_code(ENOENT, std::system_category()),
          "interface not found");
    return exists(dir) && is_directory(dir);
  }

  bool brctl_impl::is_bridge(const std::filesystem::path &br_path)
  {
    namespace sf = std::filesystem;
    sf::path dir = br_path/"bridge";
    return sf::exists(dir) && sf::is_directory(dir);
  }

  void brctl_impl::check_nlmsgerr(const std::unique_ptr<nlmsghdr> &header, const char *what_msg)
  {
    if (header->nlmsg_type != NLMSG_ERROR)
      throw std::logic_error(std::string(what_msg) + "message type is not NLMSG_ERROR");

    nlmsgerr *err = reinterpret_cast<nlmsgerr *>(NLMSG_DATA(header.get()));
    if (err->error != 0)
      throw std::system_error(
          std::error_code(-err->error, std::system_category()),
          what_msg);
  }

  void request::clear()
  {
    std::memset(this, 0, sizeof(*this));
  }

  request request::get_default()
  {
    request req;
    req.clear();
    req.h.nlmsg_len = NLMSG_LENGTH(sizeof(req.i));
    req.h.nlmsg_flags = NLM_F_REQUEST;
    req.h.nlmsg_seq = time(nullptr);
    req.i.ifi_family = AF_UNSPEC;
    return req;
  }
} //namespace pnd
