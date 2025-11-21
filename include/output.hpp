#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "bgp_sim.hpp"

inline void write_routing_csv(const BGPSim& sim,
                              const std::string& filename)
{
  std::ofstream out(filename);
  if (!out.is_open())
    throw std::runtime_error("Failed to open output CSV file: " + filename);

  out << "asn,prefix,as_path\n";

  const uint32_t max = sim.max_asn();
  for (uint32_t asn = 1; asn <= max; ++asn)
  {
    const auto& rib = sim.policy(asn).local_rib();
    if (rib.empty()) continue;

    for (const auto& kv : rib)
    {
      const std::string& prefix = kv.first;
      const Announcement& ann = kv.second;

      std::ostringstream path_ss;
      for (std::size_t i = 0; i < ann.as_path.size(); ++i)
      {
        if (i > 0) path_ss << ' ';
        path_ss << ann.as_path[i];
      }

      out << asn << ','
          << prefix << ','
          << path_ss.str()
          << '\n';
    }
  }
}
