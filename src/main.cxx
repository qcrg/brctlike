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

  out <<
    "Usage: brctlike [commands]\n" <<
    "commands:\n" <<
    cmdf("addbr", br_name, "add bridge") <<
    cmdf("delbr", br_name, "delete bridge") <<
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

int main(int argc, const char *const *argv)
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
