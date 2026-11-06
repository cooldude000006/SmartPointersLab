#pragma once

#include <cstddef>
#include <stdexcept>
#include <utility>

#include "UnqPtr.h"

// DynamicArray<T> - динамический массив, память которого принадлежит UnqPtr<T[]>.
// Контейнер для проверки умных указателей на практике (требование ТЗ):
// в нём нет ни одного явного delete - освобождением занимается UnqPtr.
//
// Инвариант: элементы с индексами [size_, capacity_) находятся в состоянии T(),
// поэтому при уменьшении размера ресурсы удалённых элементов сразу освобождаются.
template <typename T>
class DynamicArray
{
public:
    DynamicArray() noexcept : data_(), size_(0), capacity_(0)
    {
    }

    explicit DynamicArray(std::size_t size) : data_(), size_(size), capacity_(size)
    {
        if (size > 0)
        {
            data_ = MakeUnq<T[]>(size);
        }
    }

    // Глубокое копирование: у копии свой буфер
    DynamicArray(const DynamicArray& other) : DynamicArray(other.size_)
    {
        for (std::size_t i = 0; i < size_; ++i)
        {
            data_[i] = other.data_[i];
        }
    }

    DynamicArray(DynamicArray&& other) noexcept
        : data_(std::move(other.data_)), size_(other.size_), capacity_(other.capacity_)
    {
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // Принимает аргумент по значению (copy-and-swap): работает и для копирования,
    // и для перемещения, и для присваивания самому себе
    DynamicArray& operator=(DynamicArray other) noexcept
    {
        Swap(other);
        return *this;
    }

    ~DynamicArray() = default;

    const T& Get(std::size_t index) const
    {
        CheckIndex(index);
        return data_[index];
    }

    void Set(std::size_t index, const T& value)
    {
        CheckIndex(index);
        data_[index] = value;
    }

    T& operator[](std::size_t index)
    {
        CheckIndex(index);
        return data_[index];
    }

    const T& operator[](std::size_t index) const
    {
        CheckIndex(index);
        return data_[index];
    }

    std::size_t GetSize() const noexcept
    {
        return size_;
    }

    std::size_t GetCapacity() const noexcept
    {
        return capacity_;
    }

    // Добавление в конец; при нехватке места ёмкость удваивается (амортизированно O(1))
    void Append(const T& value)
    {
        if (size_ == capacity_)
        {
            // value может ссылаться на элемент этого же массива,
            // поэтому копируем его до перевыделения буфера
            T copy(value);
            Reallocate(capacity_ == 0 ? 1 : capacity_ * 2);
            data_[size_] = std::move(copy);
        }
        else
        {
            data_[size_] = value;
        }
        ++size_;
    }

    void Resize(std::size_t newSize)
    {
        if (newSize > capacity_)
        {
            Reallocate(newSize);
        }
        for (std::size_t i = newSize; i < size_; ++i)
        {
            data_[i] = T();
        }
        size_ = newSize;
    }

    void Swap(DynamicArray& other) noexcept
    {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }

private:
    void CheckIndex(std::size_t index) const
    {
        if (index >= size_)
        {
            throw std::out_of_range("DynamicArray: index out of range");
        }
    }

    // Новый буфер выделяется до изменения состояния: если new бросит
    // исключение, массив останется прежним. Старый буфер удалит UnqPtr.
    void Reallocate(std::size_t newCapacity)
    {
        UnqPtr<T[]> newData = MakeUnq<T[]>(newCapacity);
        for (std::size_t i = 0; i < size_; ++i)
        {
            newData[i] = std::move(data_[i]);
        }
        data_ = std::move(newData);
        capacity_ = newCapacity;
    }

    UnqPtr<T[]> data_;
    std::size_t size_;
    std::size_t capacity_;
};
