#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <sstream>
#include <cctype>

#include "parser.hpp"
#include "data_record.hpp"
#include "read_caida.hpp"
#include "as_graph.hpp"
#include "bgp_sim.hpp"
#include "output.hpp"

// ----------------- small helpers -----------------

static inline std::string trim(const std::string& s) {
    std::size_t start = 0;
    while (start < s.size() &&
           std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

static bool parse_bool(const std::string& raw) {
    std::string s = trim(raw);
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (s == "true" || s == "t" || s == "1")  return true;
    if (s == "false" || s == "f" || s == "0") return false;

    throw std::runtime_error("Cannot parse boolean value: '" + raw + "'");
}

// Load list of ROV ASNs from CSV (one ASN per line, header allowed)
static std::vector<uint32_t> load_rov_asns(const std::string& filename) {
    std::ifstream in(filename);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open ROV ASNs file: " + filename);
    }

    std::vector<uint32_t> rov_asns;
    std::string line;
    bool first = true;

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (!line.empty() && line[0] == '#') continue;

        // Handle possible header on first non-empty line
        if (first) {
            first = false;
            bool all_digits = !line.empty();
            for (char c : line) {
                if (!std::isdigit(static_cast<unsigned char>(c))) {
                    all_digits = false;
                    break;
                }
            }
            if (!all_digits) {
                // treat as header, skip
                continue;
            }
        }

        // If there are commas, take first field as ASN
        std::string asn_str;
        {
            std::stringstream ss(line);
            std::getline(ss, asn_str, ',');
        }
        asn_str = trim(asn_str);
        if (asn_str.empty()) continue;

        uint32_t asn = static_cast<uint32_t>(std::stoul(asn_str));
        rov_asns.push_back(asn);
    }

    return rov_asns;
}

// Load announcements CSV: ASN,prefix,rov_invalid
static void load_and_seed_announcements(const std::string& filename,
                                        BGPSim& sim)
{
    std::ifstream in(filename);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open announcements file: " + filename);
    }

    std::string line;
    bool first = true;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') continue;

        std::stringstream ss(line);
        std::string asn_str, prefix_str, rov_str;

        if (!std::getline(ss, asn_str, ',')) continue;
        if (!std::getline(ss, prefix_str, ',')) continue;
        if (!std::getline(ss, rov_str,  '\n'))  continue;

        if (first) {
            first = false;
            // Heuristic: if ASN column is not numeric, treat this as header and skip
            bool all_digits = true;
            std::string tmp = trim(asn_str);
            if (tmp.empty()) all_digits = false;
            for (char c : tmp) {
                if (!std::isdigit(static_cast<unsigned char>(c))) {
                    all_digits = false;
                    break;
                }
            }
            if (!all_digits) {
                // header line
                continue;
            }
        }

        uint32_t asn = static_cast<uint32_t>(std::stoul(trim(asn_str)));
        std::string prefix = trim(prefix_str);
        bool rov_invalid = parse_bool(rov_str);

        sim.seed_prefix(prefix, asn, rov_invalid);
    }
}

// Compute maximum ASN in CAIDA relationships (first pass)
static uint32_t find_max_asn(const std::string& rel_filename) {
    uint32_t max_asn = 0;

    read_caida_data(rel_filename, [&](const DataRecord& rec) {
        if (rec.provider_peer > max_asn) max_asn = rec.provider_peer;
        if (rec.customer_peer > max_asn) max_asn = rec.customer_peer;
    });

    return max_asn;
}

static void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog
        << " --relationships <as-rel-file>"
        << " --announcements <announcements.csv>"
        << " --rov-asns <rov_asns.csv>"
        << " [--output <ribs.csv>]\n";
}

// ----------------- main -----------------

int main(int argc, char** argv) {
    std::string rel_file;
    std::string ann_file;
    std::string rov_file;
    std::string out_file = "ribs.csv";  // default output name

    // Simple manual flag parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto need_value = [&](const std::string& flag) {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << "\n";
                print_usage(argv[0]);
                std::exit(1);
            }
        };

        if (arg == "--relationships") {
            need_value(arg);
            rel_file = argv[++i];
        } else if (arg == "--announcements") {
            need_value(arg);
            ann_file = argv[++i];
        } else if (arg == "--rov-asns") {
            need_value(arg);
            rov_file = argv[++i];
        } else if (arg == "--output") {
            need_value(arg);
            out_file = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (rel_file.empty() || ann_file.empty() || rov_file.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        // 1) First pass over CAIDA relationships: find maximum ASN
        uint32_t max_asn = find_max_asn(rel_file);
        if (max_asn == 0) {
            std::cerr << "Error: no ASNs found in relationships file.\n";
            return 1;
        }

        // 2) Build AS graph
        ASGraph graph(max_asn);
        build_graph(rel_file, graph);

        // 3) Build simulator with ROV ASNs
        auto rov_asns = load_rov_asns(rov_file);
        BGPSim sim(graph, rov_asns); // flatten_graph called inside

        // 4) Load announcements and seed them
        load_and_seed_announcements(ann_file, sim);

        // 5) Propagate
        sim.propagate_all();

        // 6) Write ribs.csv (or user-specified)
        write_routing_csv(sim, out_file);

        return 0;
    }
    catch (const std::runtime_error& ex) {
        std::string msg = ex.what();
        if (msg.find("cycle") != std::string::npos) {
            std::cerr << "Error: provider/customer cycle detected in AS relationships.\n";
            std::cerr << "Details: " << msg << "\n";
        } else {
            std::cerr << "Error: " << msg << "\n";
        }
        return 1;
    }
    catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
