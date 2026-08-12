// Dumps every distinct `<key>\t<value>` pair of the conditional way tags Valhalla parses, so the
// whole planet can be replayed through get_time_range offline.
#include <ankerl/unordered_dense.h>
#include <osmium/handler.hpp>
#include <osmium/io/pbf_input.hpp>
#include <osmium/visitor.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

// pbfgraphparser matches these with starts_with, so `access:conditional:foo` counts too
constexpr std::string_view kAccessPrefixes[] = {
    "access:conditional",     "motorcar:conditional",   "motor_vehicle:conditional",
    "bicycle:conditional",    "motorcycle:conditional", "foot:conditional",
    "pedestrian:conditional", "hgv:conditional",        "moped:conditional",
    "mofa:conditional",       "psv:conditional",        "taxi:conditional",
    "bus:conditional",        "hov:conditional",        "emergency:conditional",
};

// this one goes through tag_handlers_, an exact lookup by key
constexpr std::string_view kMaxSpeed = "maxspeed:conditional";

bool is_parsed_key(std::string_view key) {
  if (key == kMaxSpeed) {
    return true;
  }
  for (const std::string_view prefix : kAccessPrefixes) {
    if (key.starts_with(prefix)) {
      return true;
    }
  }
  return false;
}

// a tag value may hold anything, and the output is one pair per line
std::string escaped(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    switch (c) {
      case '\t':
        out += "\\t";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        out += c;
    }
  }
  return out;
}

struct handler : osmium::handler::Handler {
  ankerl::unordered_dense::set<std::string> pairs;
  uint64_t ways = 0, tags = 0;

  void way(const osmium::Way& way) {
    ++ways;
    for (const osmium::Tag& tag : way.tags()) {
      if (is_parsed_key(tag.key())) {
        ++tags;
        pairs.emplace(std::string(tag.key()) + '\t' + escaped(tag.value()));
      }
    }
  }
};

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: valhalla_dump_conditional_tags planet.osm.pbf > pairs.tsv\n";
    return EXIT_FAILURE;
  }

  handler h;
  // ways only, which skips the nodes that make up most of a planet file
  osmium::io::Reader reader(argv[1], osmium::osm_entity_bits::way);
  osmium::apply(reader, h);
  reader.close();

  std::vector<std::string> sorted(h.pairs.begin(), h.pairs.end());
  std::sort(sorted.begin(), sorted.end());
  for (const auto& pair : sorted) {
    std::cout << pair << "\n";
  }
  std::cerr << h.ways << " ways, " << h.tags << " conditional tags, " << sorted.size()
            << " distinct pairs\n";
  return EXIT_SUCCESS;
}
