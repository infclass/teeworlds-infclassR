#include <gtest/gtest.h>

#include <base/tl/ic_array.h>

TEST(ICArray, BaseTest)
{
	icArray<int, 10> Array1;
	EXPECT_EQ(Array1.Size(), 0);
	Array1.Add(0);
	EXPECT_EQ(Array1.First(), 0);
	EXPECT_EQ(Array1.Last(), 0);
	Array1.Add(1);
	EXPECT_EQ(Array1.First(), 0);
	EXPECT_EQ(Array1.Last(), 1);
	Array1.Add(2);
	EXPECT_EQ(Array1.First(), 0);
	EXPECT_EQ(Array1.Last(), 2);
	Array1.Add(3);
	EXPECT_EQ(Array1.First(), 0);
	EXPECT_EQ(Array1.Last(), 3);
	EXPECT_EQ(Array1.Size(), 4);

	Array1.RemoveAt(2);
	EXPECT_EQ(Array1.First(), 0);
	EXPECT_EQ(Array1.Last(), 3);
	EXPECT_EQ(Array1.Size(), 3);

	EXPECT_EQ(Array1.At(0), 0);
	EXPECT_EQ(Array1.At(1), 1);
	EXPECT_EQ(Array1.At(2), 3);

	std::size_t Index = 0;
	for(int Value : Array1)
	{
		EXPECT_EQ(Value, Array1.At(Index));
		Index++;
	}

	const auto Array2(Array1);
	EXPECT_EQ(Array1.Size(), Array2.Size());

	Index = 0;
	for(int Value : Array2)
	{
		EXPECT_EQ(Value, Array1.At(Index));
		EXPECT_EQ(Value, Array2.At(Index));
		Index++;
	}
}

TEST(ICArray, ReverseItTest)
{
	icArray<int, 10> Array1;

	for (int i = 0; i < 5; ++i)
	{
		Array1.Add(i);
		EXPECT_EQ(Array1.Size(), static_cast<std::size_t>(i + 1));
	}

	std::size_t Index = 0;
	for(int Value : Array1)
	{
		EXPECT_EQ(Value, Array1.At(Index));
		Index++;
	}

	EXPECT_EQ(Index, Array1.Size());

	Index -= 1;
	EXPECT_EQ(Index, 4); // Sanity check
	for (auto it = Array1.rbegin(); it != Array1.rend(); ++it) {
		EXPECT_EQ(*it, Array1.At(Index));
		Index--;
	}
}

TEST(ICArray, EraseIf)
{
	icArray<int, 10> Array1;

	for (int i = 0; i < 5; ++i)
	{
		Array1.Add(i);
		EXPECT_EQ(Array1.Size(), static_cast<std::size_t>(i + 1));
	}

	std::size_t RemovedCount = std::erase_if(Array1, [](int Value) { return Value % 2;});
	EXPECT_EQ(RemovedCount, 2);
	EXPECT_EQ(Array1.Size(), 3);

	EXPECT_EQ(Array1.At(0), 0);
	EXPECT_EQ(Array1.At(1), 2);
	EXPECT_EQ(Array1.At(2), 4);
}

int main(int argc, char *argv[])
{
	::testing::InitGoogleTest(&argc, const_cast<char **>(argv));

	int Result = RUN_ALL_TESTS();

	return Result;
}
