#include <gtest/gtest.h>
#include "../include/parser.hpp"
#include "../include/read_caida.hpp"
#include "../include/data_record.hpp"
#include "../include/as_graph.hpp"

#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <iostream>

// -------------------- PARSER TESTS --------------------

TEST(ParserTest, ParsesValidLine) {
    DataRecord rec;
    std::string line = "42|4345|0|str";

    bool result = parse_line(line, rec);

    EXPECT_TRUE(result);
    EXPECT_EQ(rec.provider_peer, 42);
    EXPECT_EQ(rec.customer_peer, 4345);
    EXPECT_EQ(rec.indicator, 0);
}

TEST(ParserTest, RejectsMalformedLine) {
    DataRecord rec;
    std::string line = "42|4345";  // missing fields

    bool result = parse_line(line, rec);
    EXPECT_FALSE(result);
}

TEST(ParserTest, HandlesNegativeIndicator) {
    DataRecord rec;
    std::string line = "10|20|-1|meta";

    bool result = parse_line(line, rec);
    EXPECT_TRUE(result);
    EXPECT_EQ(rec.indicator, -1);
}

// -------------------- READ_CAIDA TESTS --------------------

TEST(ReadCaidaTest, ReadsValidRecords) {
    // Create a temporary file with CAIDA-format data
    std::ofstream tmp("test_data.txt");
    tmp << "# header line\n";
    tmp << "1|2|0|meta\n";
    tmp << "3|4|-1|meta\n";
    tmp.close();

    std::vector<DataRecord> records;

    read_caida_data("test_data.txt", [&](const DataRecord& rec) {
        records.push_back(rec);
    });

    ASSERT_EQ(records.size(), 2);
    EXPECT_EQ(records[0].provider_peer, 1);
    EXPECT_EQ(records[0].customer_peer, 2);
    EXPECT_EQ(records[0].indicator, 0);

    EXPECT_EQ(records[1].provider_peer, 3);
    EXPECT_EQ(records[1].customer_peer, 4);
    EXPECT_EQ(records[1].indicator, -1);
}


// -------------------- AS GRAPH TESTS --------------------

TEST(ASGraphTest, AddEdges) {
    ASGraph g(10);

    g.add_provider_customer(1, 2);
    g.add_provider_customer(1, 3);
    g.add_peer(4, 5);

    // Provider/customer edges
    EXPECT_EQ(g.get(1).customers.size(), 2);
    EXPECT_EQ(g.get(2).providers.size(), 1);
    EXPECT_EQ(g.get(3).providers.size(), 1);

    EXPECT_EQ(g.get(1).customers[0], 2);
    EXPECT_EQ(g.get(1).customers[1], 3);

    // Peer edges are symmetric
    EXPECT_EQ(g.get(4).peers.size(), 1);
    EXPECT_EQ(g.get(5).peers.size(), 1);
    EXPECT_EQ(g.get(4).peers[0], 5);
    EXPECT_EQ(g.get(5).peers[0], 4);
}

TEST(ASGraphTest, BuildGraphFromFile) {
    std::ofstream tmp("graph_test.txt");
    tmp << "# comment\n";
    tmp << "1|2|0|foo\n";   // provider -> customer
    tmp << "3|4|-1|bar\n";  // peer link
    tmp.close();

    ASGraph g(10);
    build_graph("graph_test.txt", g);

    // provider→customer
    EXPECT_EQ(g.get(1).customers.size(), 1);
    EXPECT_EQ(g.get(2).providers.size(), 1);

    // peers
    EXPECT_EQ(g.get(3).peers.size(), 1);
    EXPECT_EQ(g.get(3).peers[0], 4);
    EXPECT_EQ(g.get(4).peers[0], 3);
}

// -------------------- PROVIDER CYCLE TESTS --------------------

TEST(CycleTest, NoCycle) {
    ASGraph g(10);

    g.add_provider_customer(1, 2);
    g.add_provider_customer(2, 3);
    g.add_provider_customer(3, 4);

    EXPECT_FALSE(has_provider_cycle(g));
}

TEST(CycleTest, DetectsCycle) {
    ASGraph g(10);

    g.add_provider_customer(1, 2);
    g.add_provider_customer(2, 3);
    g.add_provider_customer(3, 1);   // closes the loop

    EXPECT_TRUE(has_provider_cycle(g));
}

// ----------- TEST ON CAIDA DATASET ---------------

TEST(CaidaRealDataTest, BuildsFullGraphSuccessfully) {
    const std::string filename = "../data/20250901.as-rel2.txt";   // UPDATE THIS

    ASSERT_TRUE(std::filesystem::exists(filename))
        << "Missing CAIDA dataset. Download it before running this test.";

    uint32_t max_asn = 0;
    size_t record_count = 0;

    // FIRST PASS → detect largest ASN
    read_caida_data(filename, [&](const DataRecord& rec) {
        max_asn = std::max(max_asn, rec.provider_peer);
        max_asn = std::max(max_asn, rec.customer_peer);
        record_count++;
    });

    ASSERT_GT(record_count, 50000)
        << "CAIDA dataset appears incomplete — parsed fewer than 50k lines.";

    std::cout << "[ INFO ] Max ASN detected = " << max_asn << "\n";
    ASSERT_GE(max_asn, 400000) << "Max ASN is unexpectedly small.";

    // Build graph with correct size
    ASGraph graph(max_asn);

    // SECOND PASS → build actual graph
    read_caida_data(filename, [&](const DataRecord& rec) {
        if (rec.indicator == 0)
            graph.add_provider_customer(rec.provider_peer, rec.customer_peer);
        else
            graph.add_peer(rec.provider_peer, rec.customer_peer);
    });

    EXPECT_FALSE(has_provider_cycle(graph))
        << "Provider loop detected — real CAIDA data may contain a customer/provider cycle!";
}
