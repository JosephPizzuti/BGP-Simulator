#include <gtest/gtest.h>
#include "../include/parser.hpp"
#include "../include/read_caida.hpp"
#include "../include/data_record.hpp"

#include <fstream>
#include <vector>
#include <string>

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
    tmp << "3|4|1|meta\n";
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
    EXPECT_EQ(records[1].indicator, 1);
}

