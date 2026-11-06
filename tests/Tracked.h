#pragma once

#include <gtest/gtest.h>

// Тестовые классы со счётчиком созданных и удалённых объектов.
// Утечки нет, если после теста created == destroyed.
class Tracked
{
public:
    inline static int created = 0;
    inline static int destroyed = 0;

    static void ResetCounters()
    {
        created = 0;
        destroyed = 0;
    }

    static int Alive()
    {
        return created - destroyed;
    }

    Tracked() : value_(0)
    {
        ++created;
    }

    explicit Tracked(int value) : value_(value)
    {
        ++created;
    }

    Tracked(const Tracked& other) : value_(other.value_)
    {
        ++created;
    }

    Tracked& operator=(const Tracked&) = default;

    // Виртуальный деструктор: удаление Derived через указатель на Base корректно
    virtual ~Tracked()
    {
        ++destroyed;
    }

    virtual int GetKind() const
    {
        return 1;
    }

    int GetValue() const
    {
        return value_;
    }

private:
    int value_;
};

class TrackedDerived : public Tracked
{
public:
    TrackedDerived() = default;

    explicit TrackedDerived(int value) : Tracked(value)
    {
    }

    int GetKind() const override
    {
        return 2;
    }
};

// Класс, не связанный наследованием с Tracked
class Unrelated
{
};

// Базовый класс для тестов: обнуляет счётчики и проверяет отсутствие утечек
class LeakCheckedTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Tracked::ResetCounters();
    }

    void TearDown() override
    {
        EXPECT_EQ(Tracked::created, Tracked::destroyed) << "memory leak: not all objects were deleted";
    }
};
