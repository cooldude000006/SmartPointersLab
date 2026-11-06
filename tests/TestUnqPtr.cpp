#include <gtest/gtest.h>

#include <stdexcept>
#include <type_traits>
#include <utility>

#include "Tracked.h"
#include "UnqPtr.h"

// ---------------------------------------------------------------------------
// Проверки на этапе компиляции
// ---------------------------------------------------------------------------

// Копирование запрещено, перемещение разрешено
static_assert(!std::is_copy_constructible<UnqPtr<Tracked>>::value, "UnqPtr must not be copyable");
static_assert(!std::is_copy_assignable<UnqPtr<Tracked>>::value, "UnqPtr must not be copy-assignable");
static_assert(std::is_nothrow_move_constructible<UnqPtr<Tracked>>::value, "UnqPtr must be movable");
static_assert(!std::is_copy_constructible<UnqPtr<Tracked[]>>::value, "UnqPtr<T[]> must not be copyable");

// Подтипизация: Derived -> Base работает, обратно и между несвязанными типами - нет
static_assert(std::is_constructible<UnqPtr<Tracked>, UnqPtr<TrackedDerived>&&>::value, "Derived -> Base");
static_assert(std::is_assignable<UnqPtr<Tracked>&, UnqPtr<TrackedDerived>&&>::value, "Derived -> Base");
static_assert(!std::is_constructible<UnqPtr<TrackedDerived>, UnqPtr<Tracked>&&>::value, "Base -> Derived");
static_assert(!std::is_constructible<UnqPtr<Tracked>, UnqPtr<Unrelated>&&>::value, "Unrelated -> Tracked");
static_assert(!std::is_assignable<UnqPtr<Tracked>&, UnqPtr<Unrelated>&&>::value, "Unrelated -> Tracked");

// Для массивов подтипизации нет
static_assert(!std::is_constructible<UnqPtr<Tracked[]>, UnqPtr<TrackedDerived[]>&&>::value, "Derived[] -> Base[]");
static_assert(!std::is_constructible<UnqPtr<Tracked[]>, TrackedDerived*>::value, "Derived* -> Base[]");

// ---------------------------------------------------------------------------
// UnqPtr<T>
// ---------------------------------------------------------------------------
using UnqPtrTest = LeakCheckedTest;

TEST_F(UnqPtrTest, DefaultConstructedIsNull)
{
    UnqPtr<Tracked> pointer;
    EXPECT_FALSE(pointer);
    EXPECT_EQ(pointer.Get(), nullptr);

    UnqPtr<Tracked> fromNullptr = nullptr;
    EXPECT_FALSE(fromNullptr);
}

TEST_F(UnqPtrTest, OwnsObjectAndDeletesItAtScopeExit)
{
    {
        UnqPtr<Tracked> pointer(new Tracked(42));
        EXPECT_TRUE(pointer);
        EXPECT_EQ(Tracked::Alive(), 1);
        EXPECT_EQ((*pointer).GetValue(), 42);
        EXPECT_EQ(pointer->GetValue(), 42);
    }
    EXPECT_EQ(Tracked::Alive(), 0);
}

TEST_F(UnqPtrTest, MakeUnqForwardsArguments)
{
    UnqPtr<Tracked> pointer = MakeUnq<Tracked>(7);
    EXPECT_EQ(pointer->GetValue(), 7);
}

TEST_F(UnqPtrTest, DereferencingNullThrows)
{
    UnqPtr<Tracked> pointer;
    EXPECT_THROW(*pointer, std::logic_error);
    EXPECT_THROW(pointer->GetValue(), std::logic_error);
}

TEST_F(UnqPtrTest, MoveConstructorTransfersOwnership)
{
    UnqPtr<Tracked> source = MakeUnq<Tracked>(1);
    Tracked* raw = source.Get();

    UnqPtr<Tracked> target(std::move(source));

    EXPECT_FALSE(source);
    EXPECT_EQ(target.Get(), raw);
    EXPECT_EQ(Tracked::Alive(), 1);
}

TEST_F(UnqPtrTest, MoveAssignmentDeletesPreviousObject)
{
    UnqPtr<Tracked> source = MakeUnq<Tracked>(1);
    UnqPtr<Tracked> target = MakeUnq<Tracked>(2);

    target = std::move(source);

    EXPECT_FALSE(source);
    EXPECT_EQ(target->GetValue(), 1);
    EXPECT_EQ(Tracked::Alive(), 1);
}

TEST_F(UnqPtrTest, SelfMoveAssignmentKeepsObject)
{
    UnqPtr<Tracked> pointer = MakeUnq<Tracked>(5);
    UnqPtr<Tracked>& alias = pointer;

    pointer = std::move(alias);

    ASSERT_TRUE(pointer);
    EXPECT_EQ(pointer->GetValue(), 5);
    EXPECT_EQ(Tracked::Alive(), 1);
}

TEST_F(UnqPtrTest, AssigningNullptrDeletesObject)
{
    UnqPtr<Tracked> pointer = MakeUnq<Tracked>();
    pointer = nullptr;
    EXPECT_FALSE(pointer);
    EXPECT_EQ(Tracked::Alive(), 0);
}

TEST_F(UnqPtrTest, ReleaseGivesUpOwnership)
{
    UnqPtr<Tracked> pointer = MakeUnq<Tracked>();
    Tracked* raw = pointer.Release();

    EXPECT_FALSE(pointer);
    EXPECT_EQ(Tracked::Alive(), 1);
    delete raw;
}

TEST_F(UnqPtrTest, ResetReplacesObject)
{
    UnqPtr<Tracked> pointer = MakeUnq<Tracked>(1);

    pointer.Reset(new Tracked(2));
    EXPECT_EQ(pointer->GetValue(), 2);
    EXPECT_EQ(Tracked::Alive(), 1);

    pointer.Reset();
    EXPECT_FALSE(pointer);
    EXPECT_EQ(Tracked::Alive(), 0);
}

TEST_F(UnqPtrTest, ResetWithSamePointerDoesNotDelete)
{
    UnqPtr<Tracked> pointer = MakeUnq<Tracked>(3);
    pointer.Reset(pointer.Get());

    ASSERT_TRUE(pointer);
    EXPECT_EQ(pointer->GetValue(), 3);
}

TEST_F(UnqPtrTest, SwapExchangesObjects)
{
    UnqPtr<Tracked> first = MakeUnq<Tracked>(1);
    UnqPtr<Tracked> second = MakeUnq<Tracked>(2);

    first.Swap(second);

    EXPECT_EQ(first->GetValue(), 2);
    EXPECT_EQ(second->GetValue(), 1);
}

TEST_F(UnqPtrTest, DerivedConvertsToBase)
{
    UnqPtr<TrackedDerived> derived = MakeUnq<TrackedDerived>(9);
    UnqPtr<Tracked> base(std::move(derived));

    EXPECT_FALSE(derived);
    EXPECT_EQ(base->GetKind(), 2);
    EXPECT_EQ(base->GetValue(), 9);

    UnqPtr<Tracked> other = MakeUnq<Tracked>();
    other = MakeUnq<TrackedDerived>();
    EXPECT_EQ(other->GetKind(), 2);
    EXPECT_EQ(Tracked::Alive(), 2);
}

// ---------------------------------------------------------------------------
// UnqPtr<T[]>
// ---------------------------------------------------------------------------
using UnqPtrArrayTest = LeakCheckedTest;

TEST_F(UnqPtrArrayTest, MakeUnqCreatesValueInitializedArray)
{
    UnqPtr<int[]> array = MakeUnq<int[]>(5);
    for (std::size_t i = 0; i < 5; ++i)
    {
        EXPECT_EQ(array[i], 0);
    }
}

TEST_F(UnqPtrArrayTest, IndexAccessReadsAndWrites)
{
    UnqPtr<int[]> array(new int[3]{1, 2, 3});
    array[1] = 20;

    EXPECT_EQ(array[0], 1);
    EXPECT_EQ(array[1], 20);
    EXPECT_EQ(array[2], 3);
}

TEST_F(UnqPtrArrayTest, DeletesAllElements)
{
    {
        UnqPtr<Tracked[]> array = MakeUnq<Tracked[]>(10);
        EXPECT_EQ(Tracked::Alive(), 10);
    }
    EXPECT_EQ(Tracked::created, 10);
    EXPECT_EQ(Tracked::destroyed, 10);
}

TEST_F(UnqPtrArrayTest, IndexingNullThrows)
{
    UnqPtr<int[]> array;
    EXPECT_THROW(array[0], std::logic_error);
}

TEST_F(UnqPtrArrayTest, MoveTransfersArray)
{
    UnqPtr<Tracked[]> source = MakeUnq<Tracked[]>(3);
    UnqPtr<Tracked[]> target = MakeUnq<Tracked[]>(2);

    target = std::move(source);

    EXPECT_FALSE(source);
    EXPECT_TRUE(target);
    EXPECT_EQ(Tracked::Alive(), 3);
}

TEST_F(UnqPtrArrayTest, ResetDeletesOldArray)
{
    UnqPtr<Tracked[]> array = MakeUnq<Tracked[]>(4);
    array.Reset(new Tracked[2]);
    EXPECT_EQ(Tracked::Alive(), 2);

    array = nullptr;
    EXPECT_EQ(Tracked::Alive(), 0);
}

TEST_F(UnqPtrArrayTest, EmptyArrayIsValid)
{
    UnqPtr<Tracked[]> array = MakeUnq<Tracked[]>(0);
    EXPECT_TRUE(array);
    EXPECT_EQ(Tracked::created, 0);
}
