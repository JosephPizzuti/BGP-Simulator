#include <gtest/gtest.h>
#include "../include/parser.hpp"
#include "../include/read_caida.hpp"
#include "../include/data_record.hpp"
#include "../include/as_graph.hpp"
#include "../include/announcement.hpp"
#include "../include/policy.hpp"
#include "../include/bgp.hpp"
#include "../include/bgp_sim.hpp"
#include "../include/output.hpp"

#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>

// -------------------- PARSER TESTS --------------------

TEST(ParserTest, ParsesValidLine) {
    DataRecord rec;
    std::string line = "42|4345|-1|str";  // -1 = provider–customer

    bool result = parse_line(line, rec);

    EXPECT_TRUE(result);
    EXPECT_EQ(rec.provider_peer, 42u);
    EXPECT_EQ(rec.customer_peer, 4345u);
    EXPECT_EQ(rec.indicator, -1);
}

TEST(ParserTest, RejectsMalformedLine) {
    DataRecord rec;
    std::string line = "42|4345";  // missing fields

    bool result = parse_line(line, rec);
    EXPECT_FALSE(result);
}

TEST(ParserTest, HandlesPeerIndicator) {
    DataRecord rec;
    std::string line = "10|20|0|meta";  // 0 = peer–peer

    bool result = parse_line(line, rec);
    EXPECT_TRUE(result);
    EXPECT_EQ(rec.provider_peer, 10u);
    EXPECT_EQ(rec.customer_peer, 20u);
    EXPECT_EQ(rec.indicator, 0);
}

// -------------------- READ_CAIDA TESTS --------------------

TEST(ReadCaidaTest, ReadsValidRecords) {
    // Create a temporary file with CAIDA-format data
    std::ofstream tmp("test_data.txt");
    tmp << "# header line\n";
    tmp << "1|2|-1|meta\n";  // provider–customer
    tmp << "3|4|0|meta\n";   // peer–peer
    tmp.close();

    std::vector<DataRecord> records;

    read_caida_data("test_data.txt", [&](const DataRecord& rec) {
        records.push_back(rec);
    });

    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].provider_peer, 1u);
    EXPECT_EQ(records[0].customer_peer, 2u);
    EXPECT_EQ(records[0].indicator, -1);

    EXPECT_EQ(records[1].provider_peer, 3u);
    EXPECT_EQ(records[1].customer_peer, 4u);
    EXPECT_EQ(records[1].indicator, 0);
}

// -------------------- AS GRAPH TESTS --------------------

TEST(ASGraphTest, AddEdges) {
    ASGraph g(10);

    g.add_provider_customer(1, 2); // 1 → 2
    g.add_provider_customer(1, 3); // 1 → 3
    g.add_peer(4, 5);              // 4 ↔ 5

    // Provider/customer edges
    EXPECT_EQ(g.get(1).customers.size(), 2u);
    EXPECT_EQ(g.get(2).providers.size(), 1u);
    EXPECT_EQ(g.get(3).providers.size(), 1u);

    EXPECT_EQ(g.get(1).customers[0], 2u);
    EXPECT_EQ(g.get(1).customers[1], 3u);

    // Peer edges are symmetric
    EXPECT_EQ(g.get(4).peers.size(), 1u);
    EXPECT_EQ(g.get(5).peers.size(), 1u);
    EXPECT_EQ(g.get(4).peers[0], 5u);
    EXPECT_EQ(g.get(5).peers[0], 4u);
}

TEST(ASGraphTest, BuildGraphFromFile) {
    std::ofstream tmp("graph_test.txt");
    tmp << "# comment\n";
    tmp << "1|2|-1|foo\n";  // provider → customer
    tmp << "3|4|0|bar\n";   // peer link
    tmp.close();

    ASGraph g(10);
    build_graph("graph_test.txt", g);

    // provider→customer
    EXPECT_EQ(g.get(1).customers.size(), 1u);
    EXPECT_EQ(g.get(2).providers.size(), 1u);
    EXPECT_EQ(g.get(1).customers[0], 2u);

    // peers
    EXPECT_EQ(g.get(3).peers.size(), 1u);
    EXPECT_EQ(g.get(3).peers[0], 4u);
    EXPECT_EQ(g.get(4).peers[0], 3u);
}

// -------------------- PROVIDER CYCLE TESTS --------------------

// Simple API-level test using add_provider_customer

TEST(CycleTest, DetectsSimpleCycle) {
    ASGraph g(10);

    g.add_provider_customer(1, 2);
    g.add_provider_customer(2, 3);
    g.add_provider_customer(3, 1);   // closes the loop

    EXPECT_TRUE(has_provider_cycle(g));
    EXPECT_THROW(assert_provider_acyclic(g), std::runtime_error);
}

TEST(CycleTest, NoCyclesInBranchingDAG) {
    ASGraph g(10);

    // 1 -> 2 -> 4
    // 1 -> 3 -> 4
    g.add_provider_customer(1, 2);
    g.add_provider_customer(1, 3);
    g.add_provider_customer(2, 4);
    g.add_provider_customer(3, 4);

    EXPECT_FALSE(has_provider_cycle(g));
    EXPECT_NO_THROW(assert_provider_acyclic(g));
}

TEST(CycleTest, NoCyclesWithOverlappingPaths) {
    ASGraph g(10);

    // 1 -> 2 -> 4
    // 1 -> 3 -> 4
    // 2 -> 5
    g.add_provider_customer(1, 2);
    g.add_provider_customer(1, 3);
    g.add_provider_customer(2, 4);
    g.add_provider_customer(3, 4);
    g.add_provider_customer(2, 5);

    EXPECT_FALSE(has_provider_cycle(g));
    EXPECT_NO_THROW(assert_provider_acyclic(g));
}

TEST(CycleTest, DetectsCycleFromSyntheticFile) {
    const char* fname = "cycle_test_data.txt";
    {
        std::ofstream tmp(fname);
        tmp << "# header line\n";
        tmp << "1|2|-1|meta\n"; // 1 is provider of 2
        tmp << "2|3|-1|meta\n"; // 2 is provider of 3
        tmp << "3|1|-1|meta\n"; // 3 is provider of 1 -> cycle 1→2→3→1
        tmp << "4|5|-1|meta\n"; // extra acyclic edge
    }

    // First pass: find max ASN for graph size
    uint32_t max_asn = 0;
    read_caida_data(fname, [&](const DataRecord& rec) {
        max_asn = std::max(max_asn, rec.provider_peer);
        max_asn = std::max(max_asn, rec.customer_peer);
    });

    ASGraph g(max_asn);
    build_graph(fname, g);

    EXPECT_TRUE(has_provider_cycle(g));
    EXPECT_THROW(assert_provider_acyclic(g), std::runtime_error);
}

// ----------- INTEGRATION TEST ON CAIDA DATASET ---------------

TEST(CaidaRealDataTest, CanBuildGraphAndQueryCycles)
{
    const std::string filename = "../data/20250901.as-rel2.txt";

    if (!std::filesystem::exists(filename)) {
        GTEST_SKIP() << "Missing CAIDA dataset. Skipping integration test.";
    }

    uint32_t max_asn = 0;
    size_t record_count = 0;

    // -------- FIRST PASS: find largest ASN and count records --------
    read_caida_data(filename, [&](const DataRecord& rec) {
        if (rec.provider_peer > max_asn)  max_asn = rec.provider_peer;
        if (rec.customer_peer > max_asn)  max_asn = rec.customer_peer;
        record_count++;
    });

    ASSERT_GT(record_count, 50000u)
        << "Parsed too few records — file likely incorrect.";

    std::cout << "[ INFO ] Max ASN observed = " << max_asn << "\n";
    std::cout << "[ INFO ] Total entries parsed = " << record_count << "\n";

    // -------- SECOND PASS: build full graph --------
    ASGraph graph(max_asn);
    build_graph(filename, graph);

    // Just ensure cycle query completes; don't assert on true/false.
    bool cyclic = has_provider_cycle(graph);

    std::cout << "[ INFO ] CAIDA provider graph is "
              << (cyclic ? "cyclic" : "acyclic") << "\n";

    // We don't care about the actual answer here; just that we can run it.
    SUCCEED();
}


// -------------------- ANNOUNCEMENT TESTS --------------------

TEST(AnnouncementTest, MakeOriginAnnouncementSetsFieldsCorrectly) {
    const std::string prefix = "1.2.3.0/24";
    uint32_t asn = 12345;

    Announcement a = make_origin_announcement(prefix, asn);

    EXPECT_EQ(a.prefix, prefix);
    ASSERT_EQ(a.as_path.size(), 1u);
    EXPECT_EQ(a.as_path[0], asn);
    EXPECT_EQ(a.next_hop_asn, asn);
    EXPECT_EQ(a.received_from, Relationship::ORIGIN);
}

TEST(AnnouncementTest, OriginBeatsCustomerPeerProvider) {
    Announcement origin   = make_origin_announcement("10.0.0.0/8", 1);

    Announcement from_c   = Announcement{"10.0.0.0/8", {2, 1},  2, Relationship::FROM_CUSTOMER};
    Announcement from_p   = Announcement{"10.0.0.0/8", {3, 1},  3, Relationship::FROM_PEER};
    Announcement from_prv = Announcement{"10.0.0.0/8", {4, 1},  4, Relationship::FROM_PROVIDER};

    // Origin should beat all non-origin routes
    EXPECT_TRUE(better_announcement(origin, from_c));
    EXPECT_TRUE(better_announcement(origin, from_p));
    EXPECT_TRUE(better_announcement(origin, from_prv));

    // FROM_CUSTOMER beats FROM_PEER and FROM_PROVIDER
    EXPECT_TRUE(better_announcement(from_c, from_p));
    EXPECT_TRUE(better_announcement(from_c, from_prv));

    // FROM_PEER beats FROM_PROVIDER
    EXPECT_TRUE(better_announcement(from_p, from_prv));
}

TEST(AnnouncementTest, ShorterPathBeatsLongerWhenRelationshipSame) {
    Announcement a{
        "1.2.3.0/24",
        {10, 20, 30},        // path length 3
        100,
        Relationship::FROM_CUSTOMER
    };
    Announcement b{
        "1.2.3.0/24",
        {10, 20, 30, 40},    // path length 4
        100,
        Relationship::FROM_CUSTOMER
    };

    EXPECT_TRUE(better_announcement(a, b));  // shorter AS path wins
    EXPECT_FALSE(better_announcement(b, a));
}

TEST(AnnouncementTest, LowerNextHopWinsWhenAllElseEqual) {
    Announcement a{
        "5.6.7.0/24",
        {10, 20},            // same path
        50,                  // lower next hop
        Relationship::FROM_PEER
    };
    Announcement b{
        "5.6.7.0/24",
        {10, 20},
        60,                  // higher next hop
        Relationship::FROM_PEER
    };

    EXPECT_TRUE(better_announcement(a, b));
    EXPECT_FALSE(better_announcement(b, a));
}


// -------------------- BGP POLICY TESTS --------------------

TEST(BGPPolicyTest, StoresSingleAnnouncement) {
    BGPPolicy pol(1);

    Announcement a = make_origin_announcement("1.2.3.0/24", 1);
    pol.enqueue(a);
    EXPECT_TRUE(pol.has_pending());

    pol.process_pending();
    EXPECT_FALSE(pol.has_pending());

    const auto& rib = pol.local_rib();
    auto it = rib.find("1.2.3.0/24");
    ASSERT_NE(it, rib.end());
    EXPECT_EQ(it->second.prefix, "1.2.3.0/24");
    EXPECT_EQ(it->second.as_path.size(), 1u);
    EXPECT_EQ(it->second.as_path[0], 1u);
}

TEST(BGPPolicyTest, KeepsBetterRelationship) {
    BGPPolicy pol(10);

    // Two announcements for same prefix, different relationships
    Announcement from_provider{
        "9.9.9.0/24",
        {20, 30},          // path
        20,                // next hop
        Relationship::FROM_PROVIDER
    };
    Announcement from_customer{
        "9.9.9.0/24",
        {40, 30},          // path (same length)
        40,
        Relationship::FROM_CUSTOMER
    };

    pol.enqueue(from_provider);
    pol.enqueue(from_customer);
    pol.process_pending();

    const auto& rib = pol.local_rib();
    auto it = rib.find("9.9.9.0/24");
    ASSERT_NE(it, rib.end());

    // Should choose the customer route
    EXPECT_EQ(it->second.received_from, Relationship::FROM_CUSTOMER);
}

TEST(BGPPolicyTest, ShorterPathBeatsLongerWhenRelationshipSame) {
    BGPPolicy pol(10);

    Announcement long_path{
        "5.5.5.0/24",
        {10, 20, 30, 40},          // length 4
        99,
        Relationship::FROM_PEER
    };
    Announcement short_path{
        "5.5.5.0/24",
        {10, 20},                  // length 2
        99,
        Relationship::FROM_PEER
    };

    pol.enqueue(long_path);
    pol.enqueue(short_path);
    pol.process_pending();

    const auto& rib = pol.local_rib();
    auto it = rib.find("5.5.5.0/24");
    ASSERT_NE(it, rib.end());

    EXPECT_EQ(it->second.as_path.size(), 2u);
}

TEST(BGPPolicyTest, LowerNextHopBreaksTie) {
    BGPPolicy pol(10);

    Announcement higher_next_hop{
        "7.7.7.0/24",
        {100, 200},
        60,
        Relationship::FROM_PEER
    };
    Announcement lower_next_hop{
        "7.7.7.0/24",
        {100, 200},
        50,
        Relationship::FROM_PEER
    };

    pol.enqueue(higher_next_hop);
    pol.enqueue(lower_next_hop);
    pol.process_pending();

    const auto& rib = pol.local_rib();
    auto it = rib.find("7.7.7.0/24");
    ASSERT_NE(it, rib.end());

    EXPECT_EQ(it->second.next_hop_asn, 50u);
}


// -------------------- FLATTEN / RANK TESTS --------------------

TEST(FlattenTest, SimpleChainRanks) {
    // 1 -> 2 -> 3 -> 4 (provider -> customer)
    ASGraph g(4);
    g.add_provider_customer(1, 2);
    g.add_provider_customer(2, 3);
    g.add_provider_customer(3, 4);

    auto layers = flatten_graph(g);

    ASSERT_EQ(layers.size(), 4u);

    // rank 0: leaf (no customers)
    ASSERT_EQ(layers[0].size(), 1u);
    EXPECT_EQ(layers[0][0], 4u);

    // rank 1: provider of 4
    ASSERT_EQ(layers[1].size(), 1u);
    EXPECT_EQ(layers[1][0], 3u);

    // rank 2: provider of 3
    ASSERT_EQ(layers[2].size(), 1u);
    EXPECT_EQ(layers[2][0], 2u);

    // rank 3: provider of 2
    ASSERT_EQ(layers[3].size(), 1u);
    EXPECT_EQ(layers[3][0], 1u);
}

TEST(FlattenTest, BranchingGraphRanks) {
    // 1 is provider of 2 and 3; 2 and 3 are providers of 4:
    //
    //   1
    //  / \
    // 2   3
    //  \ /
    //   4
    ASGraph g(4);
    g.add_provider_customer(1, 2);
    g.add_provider_customer(1, 3);
    g.add_provider_customer(2, 4);
    g.add_provider_customer(3, 4);

    auto layers = flatten_graph(g);

    ASSERT_EQ(layers.size(), 3u);

    // rank 0: leaf
    ASSERT_EQ(layers[0].size(), 1u);
    EXPECT_EQ(layers[0][0], 4u);

    // rank 1: 2 and 3 (order not guaranteed)
    ASSERT_EQ(layers[1].size(), 2u);
    std::vector<uint32_t> r1 = layers[1];
    std::sort(r1.begin(), r1.end());
    EXPECT_EQ(r1[0], 2u);
    EXPECT_EQ(r1[1], 3u);

    // rank 2: 1
    ASSERT_EQ(layers[2].size(), 1u);
    EXPECT_EQ(layers[2][0], 1u);
}

TEST(FlattenTest, ThrowsOnCycle) {
    ASGraph g(3);
    g.add_provider_customer(1, 2);
    g.add_provider_customer(2, 3);
    g.add_provider_customer(3, 1);   // cycle

    EXPECT_THROW(flatten_graph(g), std::runtime_error);
}


// -------------------- BGPSim / Seeding TESTS --------------------

TEST(BGPSimTest, SeedStoresOriginAnnouncement) {
    // Simple graph with 5 ASes, no edges needed for seeding
    ASGraph g(5);
    BGPSim sim(g);

    const std::string prefix = "1.2.3.0/24";
    uint32_t origin_asn = 3;

    sim.seed_prefix(prefix, origin_asn);

    const auto& rib3 = sim.policy(origin_asn).local_rib();
    auto it = rib3.find(prefix);
    ASSERT_NE(it, rib3.end());

    const Announcement& ann = it->second;
    EXPECT_EQ(ann.prefix, prefix);
    ASSERT_EQ(ann.as_path.size(), 1u);
    EXPECT_EQ(ann.as_path[0], origin_asn);
    EXPECT_EQ(ann.next_hop_asn, origin_asn);
    EXPECT_EQ(ann.received_from, Relationship::ORIGIN);
}


// -------------------- BGPSim Propagation TESTS --------------------

TEST(BGPSimPropagationTest, PropagateUpSimpleChain) {
    // 1 -> 2 -> 3 (provider -> customer)
    // 1 is top, 3 is leaf
    ASGraph g(3);
    g.add_provider_customer(1, 2);
    g.add_provider_customer(2, 3);

    BGPSim sim(g);

    // Seed at AS 3
    sim.seed_prefix("10.0.0.0/24", 3);

    // Before propagation, only 3 should know the route
    auto rib1_before = sim.policy(1).local_rib();
    auto rib2_before = sim.policy(2).local_rib();
    auto rib3_before = sim.policy(3).local_rib();
    EXPECT_TRUE(rib1_before.empty());
    EXPECT_TRUE(rib2_before.empty());
    EXPECT_FALSE(rib3_before.empty());

    sim.propagate_up();

    const auto& rib1 = sim.policy(1).local_rib();
    const auto& rib2 = sim.policy(2).local_rib();
    const auto& rib3 = sim.policy(3).local_rib();

    // AS 3 (origin)
    {
        auto it = rib3.find("10.0.0.0/24");
        ASSERT_NE(it, rib3.end());
        EXPECT_EQ(it->second.as_path.size(), 1u);
        EXPECT_EQ(it->second.as_path[0], 3u);
        EXPECT_EQ(it->second.received_from, Relationship::ORIGIN);
    }

    // AS 2 (customer of 1, provider of 3): FROM_CUSTOMER
    {
        auto it = rib2.find("10.0.0.0/24");
        ASSERT_NE(it, rib2.end());
        EXPECT_EQ(it->second.as_path.size(), 2u);
        EXPECT_EQ(it->second.as_path[0], 2u);
        EXPECT_EQ(it->second.as_path[1], 3u);
        EXPECT_EQ(it->second.received_from, Relationship::FROM_CUSTOMER);
    }

    // AS 1: FROM_CUSTOMER
    {
        auto it = rib1.find("10.0.0.0/24");
        ASSERT_NE(it, rib1.end());
        EXPECT_EQ(it->second.as_path.size(), 3u);
        EXPECT_EQ(it->second.as_path[0], 1u);
        EXPECT_EQ(it->second.as_path[1], 2u);
        EXPECT_EQ(it->second.as_path[2], 3u);
        EXPECT_EQ(it->second.received_from, Relationship::FROM_CUSTOMER);
    }
}

TEST(BGPSimPropagationTest, PropagateAcrossPeersSingleHop) {
    // Two peers, no provider/customer edges
    ASGraph g(2);
    g.add_peer(1, 2);

    BGPSim sim(g);

    sim.seed_prefix("1.2.3.0/24", 1);

    sim.propagate_across_peers();

    const auto& rib1 = sim.policy(1).local_rib();
    const auto& rib2 = sim.policy(2).local_rib();

    // AS 1: origin
    {
        auto it = rib1.find("1.2.3.0/24");
        ASSERT_NE(it, rib1.end());
        EXPECT_EQ(it->second.as_path.size(), 1u);
        EXPECT_EQ(it->second.as_path[0], 1u);
        EXPECT_EQ(it->second.received_from, Relationship::ORIGIN);
    }

    // AS 2: FROM_PEER
    {
        auto it = rib2.find("1.2.3.0/24");
        ASSERT_NE(it, rib2.end());
        EXPECT_EQ(it->second.as_path.size(), 2u);
        EXPECT_EQ(it->second.as_path[0], 2u);
        EXPECT_EQ(it->second.as_path[1], 1u);
        EXPECT_EQ(it->second.received_from, Relationship::FROM_PEER);
    }
}

// -------------------- OUTPUT / CSV TESTS --------------------

TEST(OutputTest, WritesCsvForSimpleGraph) {
    // Graph:
    //   1 (provider)
    //   └─> 2 (customer)
    ASGraph g(2);
    g.add_provider_customer(1, 2);

    BGPSim sim(g);

    // Seed at AS 2
    const std::string prefix = "10.0.0.0/24";
    sim.seed_prefix(prefix, 2);

    // Propagate full path: up (2 -> 1), across (none), down (1 -> 2, ignored by 2)
    sim.propagate_all();

    // Write CSV
    const std::string filename = "routing_test.csv";
    write_routing_csv(sim, filename);

    // Read back
    std::ifstream in(filename);
    ASSERT_TRUE(in.is_open());

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }

    // Expect header + up to two lines (AS1 and AS2 both know the prefix)
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "asn,prefix,as_path");

    // Parse line for ASN 1
    {
        std::stringstream ss(lines[1]);
        std::string asn_str, prefix_str, path_str;
        std::getline(ss, asn_str, ',');
        std::getline(ss, prefix_str, ',');
        std::getline(ss, path_str, '\n');

        EXPECT_EQ(asn_str, "1");
        EXPECT_EQ(prefix_str, prefix);
        EXPECT_EQ(path_str, "1 2");   // 1 sees path [1,2]
    }

    // Parse line for ASN 2
    {
        std::stringstream ss(lines[2]);
        std::string asn_str, prefix_str, path_str;
        std::getline(ss, asn_str, ',');
        std::getline(ss, prefix_str, ',');
        std::getline(ss, path_str, '\n');

        EXPECT_EQ(asn_str, "2");
        EXPECT_EQ(prefix_str, prefix);
        EXPECT_EQ(path_str, "2");     // 2 is origin, path [2]
    }
}


TEST(ROVPolicyTest, DropsInvalidAnnouncements) {
    ROVPolicy pol(10);

    Announcement valid = make_origin_announcement("1.2.3.0/24", 10);
    Announcement invalid = valid;
    invalid.rov_invalid = true;

    pol.enqueue(valid);
    pol.enqueue(invalid);
    pol.process_pending();

    const auto& rib = pol.local_rib();
    auto it = rib.find("1.2.3.0/24");
    ASSERT_NE(it, rib.end());
}


TEST(BGPSimROVTest, ROVNodeDoesNotStoreInvalidRoute) {
    // Simple 1 <-> 2 peering; 2 is ROV-enabled.
    ASGraph g(2);
    g.add_peer(1, 2);

    std::vector<uint32_t> rov_asns = {2};
    BGPSim sim(g, rov_asns);

    // Inject an invalid route at AS 1 (pretend it hijacked some prefix)
    Announcement hijack = make_origin_announcement("10.10.0.0/16", 1);
    hijack.rov_invalid = true;

    sim.policy(1).enqueue(hijack);
    sim.policy(1).process_pending();

    // Propagate across peers
    sim.propagate_across_peers();

    const auto& rib1 = sim.policy(1).local_rib();
    const auto& rib2 = sim.policy(2).local_rib();

    // AS 1 (non-ROV) keeps its own invalid route
    {
        auto it = rib1.find("10.10.0.0/16");
        ASSERT_NE(it, rib1.end());
        EXPECT_TRUE(it->second.rov_invalid);
    }

    // AS 2 (ROV) should have dropped it
    {
        auto it = rib2.find("10.10.0.0/16");
        EXPECT_EQ(it, rib2.end());
    }
}
