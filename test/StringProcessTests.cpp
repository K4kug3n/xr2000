#include "StringProcess.hpp"
#include <gtest/gtest.h>

#include <algorithm>

TEST(StringProcessTests, is_alpha) {
	EXPECT_TRUE(is_alpha("foobar"));

	EXPECT_FALSE(is_alpha("#####"));
	EXPECT_FALSE(is_alpha(".,!;:-_"));
	EXPECT_FALSE(is_alpha("123456"));
	EXPECT_FALSE(is_alpha("foo123"));
	EXPECT_FALSE(is_alpha("123foo"));
	EXPECT_FALSE(is_alpha(".,!;:-_#a"));
}

TEST(StringProcessTests, has_alphanumeric) {
	EXPECT_TRUE(has_alphanumeric("foobar"));
	EXPECT_TRUE(has_alphanumeric("123456"));
	EXPECT_TRUE(has_alphanumeric("foo123"));
	EXPECT_TRUE(has_alphanumeric("123foo"));

	EXPECT_FALSE(has_alphanumeric("#####"));
	EXPECT_FALSE(has_alphanumeric(".,!;:-_"));
	// Not optimal, regression test
	EXPECT_TRUE(has_alphanumeric(".,!;:-_#a"));
}

TEST(StringProcessTests, get_unique_words) {
	const std::string text1 = "foo. bar, baz:\nfoo-bar";
	std::vector<std::string> words1 = { "foo", "bar", "baz" };
	// Not optimal, should just check existance
	std::sort(words1.begin(), words1.end());
	EXPECT_EQ(words1, get_unique_words(text1));

	const std::string text2 = "### ##2 foo_ bar-baz";
	std::vector<std::string> words2 = { "bar", "baz" };
	// Not optimal, should just check existance
	std::sort(words2.begin(), words2.end());
	EXPECT_EQ(words2, get_unique_words(text2));

	const std::string text3 = "Foo fOo FOO foo FOO";
	std::vector<std::string> words3 = { "foo" };
	// Not optimal, should just check existance
	std::sort(words3.begin(), words3.end());
	EXPECT_EQ(words3, get_unique_words(text3));
}

TEST(StringProcessTests, translate) {
	const std::string text1 = "foo bar baz";
	const std::unordered_map<std::string, std::string> mapping1 {
		{ "foo", "doo" },
		{ "bar", "dar" },
		{ "baz", "daz" },
	};
	EXPECT_EQ(translate(text1, mapping1), "doo dar daz");

	const std::string text2 = "foo, bar: baz. foo-bar";
	const std::unordered_map<std::string, std::string> mapping2 {
		{ "foo", "doo" },
		{ "bar", "dar" },
		{ "baz", "daz" },
	};
	EXPECT_EQ(translate(text2, mapping2), "doo, dar: daz. doo-dar");

	const std::string text3 = "Foo, bAr: BAZ. FOO-bar";
	const std::unordered_map<std::string, std::string> mapping3 {
		{ "foo", "doo" },
		{ "bar", "dar" },
		{ "baz", "daz" },
	};
	EXPECT_EQ(translate(text2, mapping2), "doo, dar: daz. doo-dar");
}
