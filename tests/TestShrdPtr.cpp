#include <gtest/gtest.h>

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "ShrdPtr.h"
#include "Tracked.h"

// ---------------------------------------------------------------------------
// Проверки на этапе компиляции
// ---------------------------------------------------------------------------

// Подтипизация: Derived -> Base работает, обратно и между несвязанными типами - нет
static_assert(std::is_constructible<ShrdPtr<Tracked>, const ShrdPtr<TrackedDerived>&>::value, "Derived -> Base");
static_assert(std::is_assignable<ShrdPtr<Tracked>&, const ShrdPtr<TrackedDerived>&>::value, "Derived -> Base");
static_assert(std::is_constructible<ShrdPtr<Tracked>, UnqPtr<TrackedDerived>&&>::value, "UnqPtr<Derived> -> Base");
static_assert(!std::is_constructible<ShrdPtr<TrackedDerived>, const ShrdPtr<Tracked>&>::value, "Base -> Derived");
static_assert(!std::is_constructible<ShrdPtr<Tracked>, const ShrdPtr<Unrelated>&>::value, "Unrelated -> Tracked");
static_assert(!std::is_assignable<ShrdPtr<Tracked>&, const ShrdPtr<Unrelated>&>::value, "Unrelated -> Tracked");
static_assert(!std::is_constructible<ShrdPtr<Tracked>, UnqPtr<Unrelated>&&>::value, "UnqPtr<Unrelated> -> Tracked");

// Из UnqPtr - только перемещением (UnqPtr не копируется)
static_assert(!std::is_constructible<ShrdPtr<Tracked>, const UnqPtr<Tracked>&>::value, "copy from UnqPtr");

// Для массивов подтипизации нет
static_assert(!std::is_constructible<ShrdPtr<Tracked[]>, const ShrdPtr<TrackedDerived[]>&>::value,
              "Derived[] -> Base[]");
static_assert(!std::is_constructible<ShrdPtr<Tracked[]>, TrackedDerived*>::value, "Derived* -> Base[]");

// ---------------------------------------------------------------------------
// ShrdPtr<T>: создание и подсчёт ссылок
// ---------------------------------------------------------------------------
using ShrdPtrTest = LeakCheckedTest;

TEST_F(ShrdPtrTest, DefaultConstructedIsEmpty)
{
    ShrdPtr<Tracked> pointer;
    EXPECT_FALSE(pointer);
    EXPECT_EQ(pointer.UseCount(), 0u);
    EXPECT_TRUE(pointer == nullptr);

    ShrdPtr<Tracked> fromNullRaw(static_cast<Tracked*>(nullptr));
    EXPECT_EQ(fromNullRaw.UseCount(), 0u);
}

TEST_F(ShrdPtrTest, OwnsObjectWithCountOne)
{
    ShrdPtr<Tracked> pointer(new Tracked(42));
    EXPECT_EQ(pointer.UseCount(), 1u);
    EXPECT_EQ(pointer->GetValue(), 42);
    EXPECT_EQ((*pointer).GetValue(), 42);
}

TEST_F(ShrdPtrTest, MakeShrdForwardsArguments)
{
    ShrdPtr<Tracked> pointer = MakeShrd<Tracked>(7);
    EXPECT_EQ(pointer->GetValue(), 7);
    EXPECT_EQ(pointer.UseCount(), 1u);
}

TEST_F(ShrdPtrTest, DereferencingNullThrows)
{
    ShrdPtr<Tracked> pointer;
    EXPECT_THROW(*pointer, std::logic_error);
    EXPECT_THROW(pointer->GetValue(), std::logic_error);
}

TEST_F(ShrdPtrTest, CopiesShareObjectAndCount)
{
    ShrdPtr<Tracked> first = MakeShrd<Tracked>(1);
    ShrdPtr<Tracked> second(first);
    ShrdPtr<Tracked> third = second;

    EXPECT_EQ(first.UseCount(), 3u);
    EXPECT_EQ(third.UseCount(), 3u);
    EXPECT_TRUE(first == third);
    EXPECT_EQ(Tracked::Alive(), 1);
}

TEST_F(ShrdPtrTest, ObjectDeletedOnlyByLastOwner)
{
    ShrdPtr<Tracked> outer = MakeShrd<Tracked>();
    {
        ShrdPtr<Tracked> inner = outer;
        EXPECT_EQ(outer.UseCount(), 2u);
    }
    EXPECT_EQ(outer.UseCount(), 1u);
    EXPECT_EQ(Tracked::Alive(), 1);

    outer = nullptr;
    EXPECT_EQ(Tracked::Alive(), 0);
}

TEST_F(ShrdPtrTest, MoveDoesNotChangeCount)
{
    ShrdPtr<Tracked> source = MakeShrd<Tracked>();
    ShrdPtr<Tracked> keeper = source;

    ShrdPtr<Tracked> target(std::move(source));

    EXPECT_FALSE(source);
    EXPECT_EQ(source.UseCount(), 0u);
    EXPECT_EQ(target.UseCount(), 2u);
}

// ---------------------------------------------------------------------------
// ShrdPtr<T>: присваивание
// ---------------------------------------------------------------------------
TEST_F(ShrdPtrTest, CopyAssignmentReleasesOldObject)
{
    ShrdPtr<Tracked> first = MakeShrd<Tracked>(1);
    ShrdPtr<Tracked> second = MakeShrd<Tracked>(2);

    second = first;

    EXPECT_EQ(Tracked::Alive(), 1);
    EXPECT_EQ(second->GetValue(), 1);
    EXPECT_EQ(first.UseCount(), 2u);
}

// p = p при единственном владельце: без проверки this != &other объект
// был бы удалён до того, как на него снова сошлются
TEST_F(ShrdPtrTest, SelfCopyAssignmentKeepsObject)
{
    ShrdPtr<Tracked> pointer = MakeShrd<Tracked>(5);
    ShrdPtr<Tracked>& alias = pointer;

    pointer = alias;

    EXPECT_EQ(pointer.UseCount(), 1u);
    EXPECT_EQ(pointer->GetValue(), 5);
    EXPECT_EQ(Tracked::Alive(), 1);
}

// Присваивание между двумя владельцами одного объекта не меняет счётчик
TEST_F(ShrdPtrTest, AssignmentBetweenOwnersOfSameObject)
{
    ShrdPtr<Tracked> first = MakeShrd<Tracked>();
    ShrdPtr<Tracked> second = first;

    first = second;

    EXPECT_EQ(first.UseCount(), 2u);
    EXPECT_EQ(Tracked::Alive(), 1);
}

TEST_F(ShrdPtrTest, SelfMoveAssignmentKeepsObject)
{
    ShrdPtr<Tracked> pointer = MakeShrd<Tracked>(5);
    ShrdPtr<Tracked>& alias = pointer;

    pointer = std::move(alias);

    ASSERT_TRUE(pointer);
    EXPECT_EQ(pointer.UseCount(), 1u);
}

TEST_F(ShrdPtrTest, MoveAssignmentReleasesOldObject)
{
    ShrdPtr<Tracked> source = MakeShrd<Tracked>(1);
    ShrdPtr<Tracked> target = MakeShrd<Tracked>(2);

    target = std::move(source);

    EXPECT_FALSE(source);
    EXPECT_EQ(target->GetValue(), 1);
    EXPECT_EQ(target.UseCount(), 1u);
    EXPECT_EQ(Tracked::Alive(), 1);
}

TEST_F(ShrdPtrTest, ResetReleasesOwnership)
{
    ShrdPtr<Tracked> first = MakeShrd<Tracked>(1);
    ShrdPtr<Tracked> second = first;

    first.Reset(new Tracked(2));
    EXPECT_EQ(first.UseCount(), 1u);
    EXPECT_EQ(second.UseCount(), 1u);
    EXPECT_EQ(Tracked::Alive(), 2);

    first.Reset();
    second.Reset();
    EXPECT_EQ(Tracked::Alive(), 0);
}

TEST_F(ShrdPtrTest, ResetWithSamePointerDoesNotDelete)
{
    ShrdPtr<Tracked> pointer = MakeShrd<Tracked>(3);
    pointer.Reset(pointer.Get());

    EXPECT_EQ(pointer.UseCount(), 1u);
    EXPECT_EQ(pointer->GetValue(), 3);
}

TEST_F(ShrdPtrTest, SwapExchangesObjectsAndCounts)
{
    ShrdPtr<Tracked> first = MakeShrd<Tracked>(1);
    ShrdPtr<Tracked> firstCopy = first;
    ShrdPtr<Tracked> second = MakeShrd<Tracked>(2);

    first.Swap(second);

    EXPECT_EQ(first->GetValue(), 2);
    EXPECT_EQ(first.UseCount(), 1u);
    EXPECT_EQ(second->GetValue(), 1);
    EXPECT_EQ(second.UseCount(), 2u);
}

// ---------------------------------------------------------------------------
// ShrdPtr<T>: связь с UnqPtr и подтипизация
// ---------------------------------------------------------------------------
TEST_F(ShrdPtrTest, TakesOwnershipFromUnqPtr)
{
    UnqPtr<Tracked> unique = MakeUnq<Tracked>(8);
    ShrdPtr<Tracked> shared(std::move(unique));

    EXPECT_FALSE(unique);
    EXPECT_EQ(shared.UseCount(), 1u);
    EXPECT_EQ(shared->GetValue(), 8);

    shared = MakeUnq<Tracked>(9);
    EXPECT_EQ(shared->GetValue(), 9);
    EXPECT_EQ(Tracked::Alive(), 1);
}

TEST_F(ShrdPtrTest, EmptyUnqPtrGivesEmptyShrdPtr)
{
    ShrdPtr<Tracked> shared = UnqPtr<Tracked>();
    EXPECT_FALSE(shared);
    EXPECT_EQ(shared.UseCount(), 0u);
}

TEST_F(ShrdPtrTest, DerivedConvertsToBaseAndSharesCount)
{
    ShrdPtr<TrackedDerived> derived = MakeShrd<TrackedDerived>(4);
    ShrdPtr<Tracked> base = derived;

    EXPECT_EQ(derived.UseCount(), 2u);
    EXPECT_EQ(base->GetKind(), 2);
    EXPECT_TRUE(base == derived);

    ShrdPtr<Tracked> moved(std::move(derived));
    EXPECT_EQ(moved.UseCount(), 2u);
    EXPECT_FALSE(derived);
}

TEST_F(ShrdPtrTest, DerivedAssignsToBase)
{
    ShrdPtr<Tracked> base = MakeShrd<Tracked>(1);
    ShrdPtr<TrackedDerived> derived = MakeShrd<TrackedDerived>(2);

    base = derived;
    EXPECT_EQ(base->GetKind(), 2);
    EXPECT_EQ(Tracked::Alive(), 1);

    base = MakeShrd<TrackedDerived>(3);
    EXPECT_EQ(base->GetValue(), 3);
    EXPECT_EQ(derived.UseCount(), 1u);
}

TEST_F(ShrdPtrTest, ManyCopiesKeepCorrectCount)
{
    const std::size_t copyCount = 100000;
    ShrdPtr<Tracked> original = MakeShrd<Tracked>();
    {
        std::vector<ShrdPtr<Tracked>> copies(copyCount, original);
        EXPECT_EQ(original.UseCount(), copyCount + 1);
    }
    EXPECT_EQ(original.UseCount(), 1u);
    EXPECT_EQ(Tracked::Alive(), 1);
}

// ---------------------------------------------------------------------------
// ShrdPtr<T[]>
// ---------------------------------------------------------------------------
using ShrdPtrArrayTest = LeakCheckedTest;

TEST_F(ShrdPtrArrayTest, CopiesShareArray)
{
    ShrdPtr<int[]> first = MakeShrd<int[]>(3);
    ShrdPtr<int[]> second = first;

    second[1] = 10;

    EXPECT_EQ(first[1], 10);
    EXPECT_EQ(first.UseCount(), 2u);
}

TEST_F(ShrdPtrArrayTest, LastOwnerDeletesAllElements)
{
    ShrdPtr<Tracked[]> first = MakeShrd<Tracked[]>(5);
    {
        ShrdPtr<Tracked[]> second = first;
        EXPECT_EQ(Tracked::Alive(), 5);
    }
    EXPECT_EQ(Tracked::Alive(), 5);

    first = nullptr;
    EXPECT_EQ(Tracked::destroyed, 5);
}

TEST_F(ShrdPtrArrayTest, TakesOwnershipFromUnqPtrArray)
{
    UnqPtr<Tracked[]> unique = MakeUnq<Tracked[]>(3);
    ShrdPtr<Tracked[]> shared(std::move(unique));

    EXPECT_FALSE(unique);
    EXPECT_EQ(shared.UseCount(), 1u);
    EXPECT_EQ(Tracked::Alive(), 3);
}

TEST_F(ShrdPtrArrayTest, SelfAssignmentKeepsArray)
{
    ShrdPtr<Tracked[]> array = MakeShrd<Tracked[]>(2);
    ShrdPtr<Tracked[]>& alias = array;

    array = alias;
    array = std::move(alias);

    EXPECT_EQ(array.UseCount(), 1u);
    EXPECT_EQ(Tracked::Alive(), 2);
}

TEST_F(ShrdPtrArrayTest, IndexingNullThrows)
{
    ShrdPtr<int[]> array;
    EXPECT_THROW(array[0], std::logic_error);
}

TEST_F(ShrdPtrArrayTest, ResetReplacesArray)
{
    ShrdPtr<Tracked[]> array = MakeShrd<Tracked[]>(4);
    array.Reset(new Tracked[1]);
    EXPECT_EQ(Tracked::Alive(), 1);
    EXPECT_EQ(array.UseCount(), 1u);
}
