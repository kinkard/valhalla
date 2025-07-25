#include "argparse_utils.h"
#include "baldr/graphreader.h"
#include "baldr/pathlocation.h"
#include "baldr/rapidjson_utils.h"
#include "loki/worker.h"
#include "midgard/logging.h"
#include "odin/directionsbuilder.h"
#include "odin/util.h"
#include "sif/costfactory.h"
#include "thor/costmatrix.h"
#include "thor/optimizer.h"
#include "thor/timedistancematrix.h"
#include "worker.h"

#include <boost/property_tree/ptree.hpp>
#include <cxxopts.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace valhalla;
using namespace valhalla::midgard;
using namespace valhalla::baldr;
using namespace valhalla::loki;
using namespace valhalla::sif;
using namespace valhalla::thor;

// Format the time string
std::string GetFormattedTime(uint32_t secs) {
  if (secs == 0) {
    return "0";
  }
  uint32_t hours = secs / 3600;
  uint32_t minutes = (secs / 60) % 60;
  uint32_t seconds = secs % 60;
  return std::to_string(hours) + ":" + std::to_string(minutes) + ":" + std::to_string(seconds);
}

// Log results
void LogResults(const bool optimize,
                const valhalla::Options& options,
                const valhalla::Matrix& matrix,
                const bool log_details) {

  if (log_details) {
    LOG_INFO("Results:");
    uint32_t idx1 = 0;
    uint32_t idx2 = 0;
    for (uint32_t i = 0; i < matrix.times().size(); i++) {
      auto distance = matrix.distances().Get(i);
      LOG_INFO(std::to_string(idx1) + "," + std::to_string(idx2) + ": Distance= " +
               std::to_string(distance) + " Time= " + GetFormattedTime(matrix.times().Get(i)) +
               " secs = " + std::to_string(distance));
      idx2++;
      if (idx2 == options.sources_size()) {
        idx2 = 0;
        idx1++;
      }
    }
  }
  if (optimize) {
    // Optimize the path
    auto t10 = std::chrono::high_resolution_clock::now();
    std::vector<float> costs;
    costs.reserve(matrix.times().size());
    for (const auto& time : matrix.times()) {
      costs.push_back(time);
    }

    Optimizer opt;
    auto tour = opt.Solve(static_cast<uint32_t>(options.sources_size()), costs);
    LOG_INFO("Optimal Tour:");
    for (auto& loc : tour) {
      LOG_INFO("   : " + std::to_string(loc));
    }
    auto t11 = std::chrono::high_resolution_clock::now();
    uint32_t ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(t11 - t10).count();
    LOG_INFO("Optimization took " + std::to_string(ms1) + " ms");
  }
}

// Main method for testing time and distance matrix methods
int main(int argc, char* argv[]) {
  const auto program = std::filesystem::path(__FILE__).stem().string();
  // args
  std::string json_str;
  uint32_t iterations;
  bool log_details;
  bool optimize;
  boost::property_tree::ptree config;

  try {
    // clang-format off
    cxxopts::Options options(
      program,
      program + " " + VALHALLA_PRINT_VERSION + "\n\n"
      "a command line test tool for time+distance matrix routing.\n"
      "Use the -j option for specifying source to target locations.");

    options.add_options()
      ("h,help", "Print this help message.")
      ("v,version", "Print the version of this software.")
      ("j,json", "JSON Example: "
        "'{\"locations\":[{\"lat\":40.748174,\"lon\":-73.984984,\"type\":\"break\",\"heading\":200,"
        "\"name\":\"Empire State Building\",\"street\":\"350 5th Avenue\",\"city\":\"New "
        "York\",\"state\":\"NY\",\"postal_code\":\"10118-0110\",\"country\":\"US\"},{\"lat\":40."
        "749231,\"lon\":-73.968703,\"type\":\"break\",\"name\":\"United Nations "
        "Headquarters\",\"street\":\"405 East 42nd Street\",\"city\":\"New "
        "York\",\"state\":\"NY\",\"postal_code\":\"10017-3507\",\"country\":\"US\"}],\"costing\":"
        "\"auto\",\"directions_options\":{\"units\":\"miles\"}}'", cxxopts::value<std::string>())
      ("m,multi-run", "Generate the route N additional times before exiting.", cxxopts::value<uint32_t>()->default_value("1"))
      ("l,log-details", "Logs details about the solution", cxxopts::value<bool>()->default_value("false"))
      ("o,optimize", "Run optimization", cxxopts::value<bool>()->default_value("false"))
      ("c,config", "Valhalla configuration file", cxxopts::value<std::string>())
      ("i,inline-config", "Inline JSON config", cxxopts::value<std::string>());
    // clang-format on

    auto result = options.parse(argc, argv);
    if (!parse_common_args(program, options, result, &config, "mjolnir.logging"))
      return EXIT_SUCCESS;

    if (!result.count("json")) {
      throw cxxopts::exceptions::exception("A JSON format request must be present.\n\n" +
                                           options.help());
    }

    json_str = result["json"].as<std::string>();
    iterations = result["multi-run"].as<uint32_t>();
    log_details = result["log-details"].as<bool>();
    optimize = result["optimize"].as<bool>();
  } catch (cxxopts::exceptions::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (std::exception& e) {
    std::cerr << "Unable to parse command line options because: " << e.what() << "\n"
              << "This is a bug, please report it at " PACKAGE_BUGREPORT << "\n";
    return EXIT_FAILURE;
  }

  Api request;
  ParseApi(json_str, valhalla::Options::sources_to_targets, request);
  auto& options = *request.mutable_options();

  // Get something we can use to fetch tiles
  valhalla::baldr::GraphReader reader(config.get_child("mjolnir"));

  // Construct costing
  CostFactory factory;

  // Get type of route - this provides the costing method to use.
  const std::string& routetype = valhalla::Costing_Enum_Name(options.costing_type());
  LOG_INFO("routetype: " + routetype);

  // Get the costing method - pass the JSON configuration
  sif::TravelMode mode;
  auto mode_costing = factory.CreateModeCosting(options, mode);

  // Find path locations (loki) for sources and targets
  auto t0 = std::chrono::high_resolution_clock::now();
  loki_worker_t lw(config);
  lw.matrix(request);
  auto t1 = std::chrono::high_resolution_clock::now();
  uint32_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  LOG_INFO("Location Processing took " + std::to_string(ms) + " ms");

  // Get the max matrix distances for construction of the CostMatrix and TimeDistanceMatrix classes
  std::unordered_map<std::string, float> max_matrix_distance;
  for (const auto& kv : config.get_child("service_limits")) {
    // Skip over any service limits that are not for a costing method
    if (kv.first == "allow_hard_exclusions" || kv.first == "centroid" ||
        kv.first == "hierarchy_limits" || kv.first == "isochrone" || kv.first == "max_alternates" ||
        kv.first == "max_distance_disable_hierarchy_culling" || kv.first == "max_exclude_locations" ||
        kv.first == "max_exclude_polygons_length" || kv.first == "max_radius" ||
        kv.first == "max_reachability" || kv.first == "max_timedep_distance" ||
        kv.first == "max_timedep_distance_matrix" || kv.first == "skadi" || kv.first == "status" ||
        kv.first == "trace") {
      continue;
    }
    max_matrix_distance.emplace(kv.first, config.get<float>("service_limits." + kv.first +
                                                            ".max_matrix_distance"));
  }

  if (max_matrix_distance.empty()) {
    throw std::runtime_error("Missing max_matrix_distance configuration");
  }
  auto m = max_matrix_distance.find(routetype);
  float max_distance;
  if (m == max_matrix_distance.end()) {
    LOG_ERROR("Could not find max_matrix_distance for " + routetype + ". Using 4000 km.");
    max_distance = 4000000.0f;
  } else {
    max_distance = m->second;
  }

  // If the sources and targets are equal we can run optimize
  if (optimize && options.sources_size() == options.targets_size()) {
    for (uint32_t i = 0; i < options.sources_size(); ++i) {
      if (options.sources(i).ll().lat() != options.targets(i).ll().lat() ||
          options.sources(i).ll().lng() != options.targets(i).ll().lng()) {
        LOG_WARN("Targets differ from sources, skipping optimization...");
        optimize = false;
        break;
      }
    }
  } else {
    optimize = false;
  }
  if (optimize) {
    LOG_INFO("Find the optimal path");
  }

  // Timing with CostMatrix
  CostMatrix matrix(config.get_child("thor"));
  hierarchy_limits_config_t hl_config =
      parse_hierarchy_limits_from_config(config, "costmatrix", false);
  check_hierarchy_limits(mode_costing[int(mode)]->GetHierarchyLimits(), mode_costing[int(mode)],
                         options.costings().find(options.costing_type())->second.options(), hl_config,
                         true, mode_costing[int(mode)]->UseHierarchyLimits());
  t0 = std::chrono::high_resolution_clock::now();
  for (uint32_t n = 0; n < iterations; n++) {
    request.clear_matrix();
    matrix.SourceToTarget(request, reader, mode_costing, mode, max_distance);
    matrix.Clear();
  }
  t1 = std::chrono::high_resolution_clock::now();
  ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  float avg = (static_cast<float>(ms) / static_cast<float>(iterations)) * 0.001f;
  LOG_INFO("CostMatrix average time to compute: " + std::to_string(avg) + " sec");
  LogResults(optimize, options, request.matrix(), log_details);

  // Run with TimeDistanceMatrix
  TimeDistanceMatrix tdm(config.get_child("thor"));
  for (uint32_t n = 0; n < iterations; n++) {
    request.clear_matrix();
    tdm.SourceToTarget(request, reader, mode_costing, mode, max_distance);
    tdm.Clear();
  }
  t1 = std::chrono::high_resolution_clock::now();
  ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  avg = (static_cast<float>(ms) / static_cast<float>(iterations)) * 0.001f;
  LOG_INFO("TimeDistanceMatrix average time to compute: " + std::to_string(avg) + " sec");
  LogResults(optimize, options, request.matrix(), log_details);

  // Shutdown protocol buffer library
  google::protobuf::ShutdownProtobufLibrary();

  return EXIT_SUCCESS;
}
