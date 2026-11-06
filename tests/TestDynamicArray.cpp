#include <gtest/gtest.h>

#include <stdexcept>
#include <utility>

#include "DynamicArray.h"
#include "ShrdPtr.h"
#include "Tracked.h"

using DynamicArrayTest = LeakCheckedTest;

TEST_F(DynamicArrayTest, DefaultConstructedIsEmpty)
{
    DynamicArray<int> array;
    EXPECT_EQ(array.GetSize(), 0u);
    EXPECT_THROW(array.Get(0), std::out_of_range);
}

TEST_F(DynamicArrayTest, ConstructedWithSizeHasDefaultElements)
{
    DynamicArray<int> array(4);
    EXPECT_EQ(array.GetSize(), 4u);
    for (std::size_t i = 0; i < array.GetSize(); ++i)
    {
        EXPECT_EQ(array.Get(i), 0);
    }
}

TEST_F(DynamicArrayTest, SetAndGet)
{
    DynamicArray<int> array(3);
    array.Set(0, 10);
    array[2] = 30;

    EXPECT_EQ(array.Get(0), 10);
    EXPECT_EQ(array[1], 0);
    EXPECT_EQ(array.Get(2), 30);
}

TEST_F(DynamicArrayTest, OutOfRangeThrows)
{
    DynamicArray<int> array(2);
    EXPECT_THROW(array.Get(2), std::out_of_range);
    EXPECT_THROW(array.Set(5, 1), std::out_of_range);
    EXPECT_THROW(array[100], std::out_of_range);
}

TEST_F(DynamicArrayTest, AppendGrowsCapacity)
{
    DynamicArray<int> array;
    for (int i = 0; i < 100; ++i)
    {
        array.Append(i);
    }

    EXPECT_EQ(array.GetSize(), 100u);
    EXPECT_GE(array.GetCapacity(), 100u);
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_EQ(array.Get(static_cast<std::size_t>(i)), i);
    }
}

// Append(array[0]) при перевыделении: ссылка на старый буфер не должна «повиснуть»
TEST_F(DynamicArrayTest, AppendOwnElementDuringReallocation)
{
    DynamicArray<int> array;
    array.Append(7);
    ASSERT_EQ(array.GetSize(), array.GetCapacity());

    array.Append(array[0]);

    EXPECT_EQ(array.Get(1), 7);
}

TEST_F(DynamicArrayTest, ResizeKeepsPrefixAndClearsTail)
{
    DynamicArray<int> array(3);
    array[0] = 1;
    array[1] = 2;
    array[2] = 3;

    array.Resize(1);
    EXPECT_EQ(array.GetSize(), 1u);
    EXPECT_EQ(array.Get(0), 1);

    array.Resize(5);
    EXPECT_EQ(array.GetSize(), 5u);
    EXPECT_EQ(array.Get(0), 1);
    EXPECT_EQ(array.Get(1), 0);
    EXPECT_EQ(array.Get(4), 0);
}

TEST_F(DynamicArrayTest, CopyIsDeep)
{
    DynamicArray<int> original(2);
    original[0] = 1;

    DynamicArray<int> copy = original;
    copy[0] = 100;

    EXPECT_EQ(original.Get(0), 1);
    EXPECT_EQ(copy.Get(0), 100);
}

TEST_F(DynamicArrayTest, MoveLeavesSourceEmpty)
{
    DynamicArray<int> source(3);
    DynamicArray<int> target(std::move(source));

    EXPECT_EQ(source.GetSize(), 0u);
    EXPECT_EQ(target.GetSize(), 3u);

    source = std::move(target);
    EXPECT_EQ(source.GetSize(), 3u);
}

TEST_F(DynamicArrayTest, SelfAssignmentKeepsData)
{
    DynamicArray<int> array(2);
    array[1] = 5;
    DynamicArray<int>& alias = array;

    array = alias;

    EXPECT_EQ(array.GetSize(), 2u);
    EXPECT_EQ(array.Get(1), 5);
}

TEST_F(DynamicArrayTest, NoLeaksWithTrackedElements)
{
    {
        DynamicArray<Tracked> array;
        for (int i = 0; i < 50; ++i)
        {
            array.Append(Tracked(i));
        }
        DynamicArray<Tracked> copy = array;
        copy.Resize(10);
        EXPECT_EQ(copy.Get(9).GetValue(), 9);
    }
    EXPECT_EQ(Tracked::Alive(), 0);
}

// Массив разделяемых указателей: копирование контейнера увеличивает счётчики,
// уменьшение размера сразу отпускает ссылки
TEST_F(DynamicArrayTest, StoresSharedPointers)
{
    ShrdPtr<Tracked> shared = MakeShrd<Tracked>(1);
    {
        DynamicArray<ShrdPtr<Tracked>> array;
        array.Append(shared);
        array.Append(shared);
        EXPECT_EQ(shared.UseCount(), 3u);

        DynamicArray<ShrdPtr<Tracked>> copy = array;
        EXPECT_EQ(shared.UseCount(), 5u);

        copy.Resize(0);
        EXPECT_EQ(shared.UseCount(), 3u);
    }
    EXPECT_EQ(shared.UseCount(), 1u);
    EXPECT_EQ(Tracked::Alive(), 1);
}
