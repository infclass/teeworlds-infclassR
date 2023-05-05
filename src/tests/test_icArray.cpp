#include <gtest/gtest.h>

#include <base/tl/ic_array.h>

TEST(ICArrayReverse, BaseTest)
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

	int Index = 0;
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

int main(int argc, char *argv[])
{
	::testing::InitGoogleTest(&argc, const_cast<char **>(argv));

	int Result = RUN_ALL_TESTS();

	return Result;
}
