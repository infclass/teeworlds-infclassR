#include <gtest/gtest.h>

#include <base/tl/ic_fifo.h>

TEST(IcFifoArray, BaseTest)
{
	icFifoArray<int, 3> Fifo1;
	EXPECT_EQ(Fifo1.Size(), 0);
	Fifo1.Add(0);
	EXPECT_EQ(Fifo1.Last(), 0);
	Fifo1.Add(1);
	EXPECT_EQ(Fifo1.Last(), 1);
	Fifo1.Add(2);
	EXPECT_EQ(Fifo1.Last(), 2);
	EXPECT_EQ(Fifo1.Size(), 3);

	EXPECT_EQ(Fifo1.At(0), 0);
	EXPECT_EQ(Fifo1.At(1), 1);
	EXPECT_EQ(Fifo1.At(2), 2);

	Fifo1.Add(3);
	EXPECT_EQ(Fifo1.Last(), 3);
	EXPECT_EQ(Fifo1.Size(), 3);

	EXPECT_EQ(Fifo1.At(0), 1);
	EXPECT_EQ(Fifo1.At(1), 2);
	EXPECT_EQ(Fifo1.At(2), 3);

	Fifo1.Add(4);
	EXPECT_EQ(Fifo1.Last(), 4);
	EXPECT_EQ(Fifo1.Size(), 3);

	EXPECT_EQ(Fifo1.At(0), 2);
	EXPECT_EQ(Fifo1.At(1), 3);
	EXPECT_EQ(Fifo1.At(2), 4);

	EXPECT_EQ(Fifo1.IndexOf(2), 0);
	EXPECT_EQ(Fifo1.IndexOf(3), 1);
	EXPECT_EQ(Fifo1.IndexOf(4), 2);

	Fifo1.Add(5);
	EXPECT_EQ(Fifo1.Last(), 5);
	EXPECT_EQ(Fifo1.Size(), 3);

	EXPECT_EQ(Fifo1.At(0), 3);
	EXPECT_EQ(Fifo1.At(1), 4);
	EXPECT_EQ(Fifo1.At(2), 5);

	Fifo1.Add(6);
	EXPECT_EQ(Fifo1.Last(), 6);
	EXPECT_EQ(Fifo1.Size(), 3);

	EXPECT_EQ(Fifo1.At(0), 4);
	EXPECT_EQ(Fifo1.At(1), 5);
	EXPECT_EQ(Fifo1.At(2), 6);
}

TEST(IcFifoArray, RemoveTest)
{
	icFifoArray<int, 3> Fifo1;
	EXPECT_EQ(Fifo1.Size(), 0);
	Fifo1.Add(0);
	Fifo1.Add(1);
	Fifo1.Add(2);
	EXPECT_EQ(Fifo1.Size(), 3);

	Fifo1.RemoveFirst();
	EXPECT_EQ(Fifo1.Size(), 2);

	EXPECT_EQ(Fifo1.At(0), 1);
	EXPECT_EQ(Fifo1.At(1), 2);
}

TEST(IcFifoArray, RemoveAtTest)
{
	icFifoArray<int, 3> Fifo1;
	EXPECT_EQ(Fifo1.Size(), 0);
	Fifo1.Add(0);
	Fifo1.Add(1);
	Fifo1.Add(2);
	EXPECT_EQ(Fifo1.Size(), 3);

	{
		Fifo1.RemoveAt(0);
		EXPECT_EQ(Fifo1.Size(), 2);

		EXPECT_EQ(Fifo1.At(0), 1);
		EXPECT_EQ(Fifo1.At(1), 2);
	}

	{
		Fifo1.Add(3);
		EXPECT_EQ(Fifo1.Size(), 3);

		EXPECT_EQ(Fifo1.At(0), 1);
		EXPECT_EQ(Fifo1.At(1), 2);
		EXPECT_EQ(Fifo1.At(2), 3);
	}

	{
		Fifo1.RemoveAt(1);
		EXPECT_EQ(Fifo1.Size(), 2);

		EXPECT_EQ(Fifo1.At(0), 1);
		EXPECT_EQ(Fifo1.At(1), 3);
	}

	{
		Fifo1.Add(4);
		EXPECT_EQ(Fifo1.Size(), 3);

		EXPECT_EQ(Fifo1.At(0), 1);
		EXPECT_EQ(Fifo1.At(1), 3);
		EXPECT_EQ(Fifo1.At(2), 4);
	}

	{
		Fifo1.RemoveAt(2);
		EXPECT_EQ(Fifo1.Size(), 2);

		EXPECT_EQ(Fifo1.At(0), 1);
		EXPECT_EQ(Fifo1.At(1), 3);
	}
}

int main(int argc, char *argv[])
{
	::testing::InitGoogleTest(&argc, const_cast<char **>(argv));

	int Result = RUN_ALL_TESTS();

	return Result;
}
