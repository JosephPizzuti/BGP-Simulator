#include <fstream>
#include <sstream>
#include <stdexcept>
#include "bgp_sim.hpp"

inline void write_routing_csv(const BGPSim& sim,
                              const std::string& filename)
{
    std::ofstream out(filename);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open output file: " + filename);
    }

    out << "asn,prefix,as_path\n";

    // assuming ASNs start at 1
    const uint32_t max = sim.max_asn();
    for (uint32_t asn = 1; asn <= max; ++asn) {
        const auto& rib = sim.policy(asn).local_rib();

        for (const auto& [prefix, ann] : rib) {
            out << asn << ',' << prefix << ',';

            const auto& path = ann.as_path;
            std::ostringstream path_ss;

            if (path.empty()) {
                path_ss << "()";
            } else if (path.size() == 1) {
                path_ss << '(' << path[0] << ",)";
            } else {
                path_ss << '(' << path[0];
                for (std::size_t i = 1; i < path.size(); ++i) {
                    path_ss << ", " << path[i];
                }
                path_ss << ')';
            }

            out << '"' << path_ss.str() << '"' << '\n';
        }
    }
}
