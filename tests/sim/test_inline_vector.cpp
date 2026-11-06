#include <gtest/gtest.h>

#include <type_traits>
#include <vector>

#include "poly/inline_vector.hpp"

namespace {

std::vector<int> items(const poly::InlineVector<int, 8>& v) { return {v.begin(), v.end()}; }

// Rules depend on list order ("last unit" moves, city-list order): removal must keep the rest in
// order, like Java's ArrayList.
TEST(InlineVector, EraseKeepsOrder) {
    poly::InlineVector<int, 8> v;
    for (int i : {5, 6, 7, 8}) {
        v.push_back(i);
    }
    v.erase_at(1);
    EXPECT_EQ(items(v), (std::vector<int>{5, 7, 8}));
    EXPECT_TRUE(v.erase_value(8));
    EXPECT_FALSE(v.erase_value(42));
    EXPECT_EQ(items(v), (std::vector<int>{5, 7}));
    EXPECT_EQ(v.back(), 7);
}

TEST(InlineVector, EraseValueRemovesOnlyTheFirstMatch) {
    poly::InlineVector<int, 8> v;
    for (int i : {1, 2, 1}) {
        v.push_back(i);
    }
    v.erase_value(1);
    EXPECT_EQ(items(v), (std::vector<int>{2, 1}));
}

TEST(InlineVector, IsTriviallyCopyable) {
    EXPECT_TRUE((std::is_trivially_copyable_v<poly::InlineVector<int, 8>>));
}

// ADR-005: overflow aborts loudly, never drops an element.
TEST(InlineVectorDeathTest, OverflowAborts) {
    poly::InlineVector<int, 2> v;
    v.push_back(1);
    v.push_back(2);
    EXPECT_DEATH(v.push_back(3), "capacity exceeded");
}

TEST(InlineVectorDeathTest, OutOfRangeAborts) {
    poly::InlineVector<int, 2> v;
    EXPECT_DEATH(v.erase_at(0), "out of range");
}

}  // namespace
