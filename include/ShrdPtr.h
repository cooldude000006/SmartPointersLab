#pragma once

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "UnqPtr.h"

// ShrdPtr<T>, ShrdPtr<T[]> - разделяемый указатель (аналог std::shared_ptr).
// Несколько ShrdPtr владеют одним объектом; число владельцев хранится в общем
// счётчике size_t* referenceCount. Последний владелец удаляет объект и счётчик.
//
// Отличия от std::shared_ptr, за счёт которых ShrdPtr быстрее:
//  - счётчик обычный, а не атомарный (нет потокобезопасности);
//  - нет weak_ptr, пользовательских deleter'ов и aliasing-конструктора.
//
// У пустого ShrdPtr счётчик не выделяется: referenceCount == nullptr, UseCount() == 0.

// ---------------------------------------------------------------------------
// ShrdPtr<T> - разделяемое владение одиночным объектом
// ---------------------------------------------------------------------------
template <typename T>
class ShrdPtr
{
public:
    ShrdPtr() noexcept : pointer_(nullptr), referenceCount_(nullptr)
    {
    }

    ShrdPtr(std::nullptr_t) noexcept : pointer_(nullptr), referenceCount_(nullptr)
    {
    }

    // Берёт во владение объект, созданный через new.
    // Если не удалось выделить счётчик, объект удаляется и исключение пробрасывается.
    explicit ShrdPtr(T* pointer) : pointer_(pointer), referenceCount_(nullptr)
    {
        if (pointer_ == nullptr)
        {
            return;
        }
        try
        {
            referenceCount_ = new std::size_t(1);
        }
        catch (...)
        {
            delete pointer_;
            throw;
        }
    }

    ShrdPtr(const ShrdPtr& other) noexcept : pointer_(other.pointer_), referenceCount_(other.referenceCount_)
    {
        Retain();
    }

    // Подтипизация: ShrdPtr<Base> из ShrdPtr<Derived>; для несвязанных типов
    // конструктор исключается из перегрузки (SFINAE) - будет ошибка компиляции
    template <typename U, typename = std::enable_if_t<std::is_convertible<U*, T*>::value>>
    ShrdPtr(const ShrdPtr<U>& other) noexcept : pointer_(other.pointer_), referenceCount_(other.referenceCount_)
    {
        Retain();
    }

    ShrdPtr(ShrdPtr&& other) noexcept : pointer_(other.pointer_), referenceCount_(other.referenceCount_)
    {
        other.pointer_ = nullptr;
        other.referenceCount_ = nullptr;
    }

    template <typename U, typename = std::enable_if_t<std::is_convertible<U*, T*>::value>>
    ShrdPtr(ShrdPtr<U>&& other) noexcept : pointer_(other.pointer_), referenceCount_(other.referenceCount_)
    {
        other.pointer_ = nullptr;
        other.referenceCount_ = nullptr;
    }

    // Забирает объект у UnqPtr. Если выделить счётчик не удалось,
    // объектом по-прежнему владеет UnqPtr - утечки нет.
    template <typename U, typename = std::enable_if_t<std::is_convertible<U*, T*>::value>>
    ShrdPtr(UnqPtr<U>&& unique) : pointer_(nullptr), referenceCount_(nullptr)
    {
        if (unique)
        {
            referenceCount_ = new std::size_t(1);
            pointer_ = unique.Release();
        }
    }

    ~ShrdPtr()
    {
        Detach();
    }

    // Присваивание через копию и обмен (copy-and-swap).
    // Проверка this != &other: при p = p ничего делать не нужно.
    ShrdPtr& operator=(const ShrdPtr& other) noexcept
    {
        if (this != &other)
        {
            ShrdPtr copy(other);
            Swap(copy);
        }
        return *this;
    }

    template <typename U, typename = std::enable_if_t<std::is_convertible<U*, T*>::value>>
    ShrdPtr& operator=(const ShrdPtr<U>& other) noexcept
    {
        ShrdPtr copy(other);
        Swap(copy);
        return *this;
    }

    ShrdPtr& operator=(ShrdPtr&& other) noexcept
    {
        if (this != &other)
        {
            ShrdPtr moved(std::move(other));
            Swap(moved);
        }
        return *this;
    }

    template <typename U, typename = std::enable_if_t<std::is_convertible<U*, T*>::value>>
    ShrdPtr& operator=(ShrdPtr<U>&& other) noexcept
    {
        ShrdPtr moved(std::move(other));
        Swap(moved);
        return *this;
    }

    template <typename U, typename = std::enable_if_t<std::is_convertible<U*, T*>::value>>
    ShrdPtr& operator=(UnqPtr<U>&& unique)
    {
        ShrdPtr moved(std::move(unique));
        Swap(moved);
        return *this;
    }

    ShrdPtr& operator=(std::nullptr_t) noexcept
    {
        Detach();
        return *this;
    }

    T& operator*() const
    {
        CheckNotNull();
        return *pointer_;
    }

    T* operator->() const
    {
        CheckNotNull();
        return pointer_;
    }

    T* Get() const noexcept
    {
        return pointer_;
    }

    // Число ShrdPtr, владеющих объектом (0 для пустого указателя)
    std::size_t UseCount() const noexcept
    {
        return referenceCount_ != nullptr ? *referenceCount_ : 0;
    }

    explicit operator bool() const noexcept
    {
        return pointer_ != nullptr;
    }

    // Отпускает текущий объект и (если pointer != nullptr) берёт во владение новый
    void Reset(T* pointer = nullptr)
    {
        if (pointer == pointer_)
        {
            return;
        }
        ShrdPtr replacement(pointer);
        Swap(replacement);
    }

    void Swap(ShrdPtr& other) noexcept
    {
        std::swap(pointer_, other.pointer_);
        std::swap(referenceCount_, other.referenceCount_);
    }

private:
    // Доступ к полям ShrdPtr<U> нужен конструкторам подтипизации
    template <typename U>
    friend class ShrdPtr;

    void Retain() noexcept
    {
        if (referenceCount_ != nullptr)
        {
            ++*referenceCount_;
        }
    }

    // Уменьшает счётчик; последний владелец удаляет объект и счётчик
    void Detach() noexcept
    {
        if (referenceCount_ != nullptr && --*referenceCount_ == 0)
        {
            delete pointer_;
            delete referenceCount_;
        }
        pointer_ = nullptr;
        referenceCount_ = nullptr;
    }

    void CheckNotNull() const
    {
        if (pointer_ == nullptr)
        {
            throw std::logic_error("ShrdPtr: dereferencing null pointer");
        }
    }

    T* pointer_;
    std::size_t* referenceCount_;
};

// ---------------------------------------------------------------------------
// ShrdPtr<T[]> - разделяемое владение массивом, удаление через delete[].
// Подтипизации для массивов нет (как и у UnqPtr<T[]>).
// ---------------------------------------------------------------------------
template <typename T>
class ShrdPtr<T[]>
{
public:
    ShrdPtr() noexcept : pointer_(nullptr), referenceCount_(nullptr)
    {
    }

    ShrdPtr(std::nullptr_t) noexcept : pointer_(nullptr), referenceCount_(nullptr)
    {
    }

    explicit ShrdPtr(T* pointer) : pointer_(pointer), referenceCount_(nullptr)
    {
        if (pointer_ == nullptr)
        {
            return;
        }
        try
        {
            referenceCount_ = new std::size_t(1);
        }
        catch (...)
        {
            delete[] pointer_;
            throw;
        }
    }

    // Запрещает ShrdPtr<Base[]>(new Derived[n])
    template <typename U>
    explicit ShrdPtr(U* pointer) = delete;

    ShrdPtr(const ShrdPtr& other) noexcept : pointer_(other.pointer_), referenceCount_(other.referenceCount_)
    {
        Retain();
    }

    ShrdPtr(ShrdPtr&& other) noexcept : pointer_(other.pointer_), referenceCount_(other.referenceCount_)
    {
        other.pointer_ = nullptr;
        other.referenceCount_ = nullptr;
    }

    ShrdPtr(UnqPtr<T[]>&& unique) : pointer_(nullptr), referenceCount_(nullptr)
    {
        if (unique)
        {
            referenceCount_ = new std::size_t(1);
            pointer_ = unique.Release();
        }
    }

    ~ShrdPtr()
    {
        Detach();
    }

    ShrdPtr& operator=(const ShrdPtr& other) noexcept
    {
        if (this != &other)
        {
            ShrdPtr copy(other);
            Swap(copy);
        }
        return *this;
    }

    ShrdPtr& operator=(ShrdPtr&& other) noexcept
    {
        if (this != &other)
        {
            ShrdPtr moved(std::move(other));
            Swap(moved);
        }
        return *this;
    }

    ShrdPtr& operator=(UnqPtr<T[]>&& unique)
    {
        ShrdPtr moved(std::move(unique));
        Swap(moved);
        return *this;
    }

    ShrdPtr& operator=(std::nullptr_t) noexcept
    {
        Detach();
        return *this;
    }

    T& operator[](std::size_t index) const
    {
        if (pointer_ == nullptr)
        {
            throw std::logic_error("ShrdPtr: indexing null array");
        }
        return pointer_[index];
    }

    T* Get() const noexcept
    {
        return pointer_;
    }

    std::size_t UseCount() const noexcept
    {
        return referenceCount_ != nullptr ? *referenceCount_ : 0;
    }

    explicit operator bool() const noexcept
    {
        return pointer_ != nullptr;
    }

    void Reset(T* pointer = nullptr)
    {
        if (pointer == pointer_)
        {
            return;
        }
        ShrdPtr replacement(pointer);
        Swap(replacement);
    }

    void Swap(ShrdPtr& other) noexcept
    {
        std::swap(pointer_, other.pointer_);
        std::swap(referenceCount_, other.referenceCount_);
    }

private:
    void Retain() noexcept
    {
        if (referenceCount_ != nullptr)
        {
            ++*referenceCount_;
        }
    }

    void Detach() noexcept
    {
        if (referenceCount_ != nullptr && --*referenceCount_ == 0)
        {
            delete[] pointer_;
            delete referenceCount_;
        }
        pointer_ = nullptr;
        referenceCount_ = nullptr;
    }

    T* pointer_;
    std::size_t* referenceCount_;
};

// ---------------------------------------------------------------------------
// Сравнение (указывают ли на один объект) и фабрики
// ---------------------------------------------------------------------------
template <typename T, typename U>
bool operator==(const ShrdPtr<T>& left, const ShrdPtr<U>& right) noexcept
{
    return left.Get() == right.Get();
}

template <typename T, typename U>
bool operator!=(const ShrdPtr<T>& left, const ShrdPtr<U>& right) noexcept
{
    return !(left == right);
}

template <typename T>
bool operator==(const ShrdPtr<T>& pointer, std::nullptr_t) noexcept
{
    return !pointer;
}

template <typename T>
bool operator!=(const ShrdPtr<T>& pointer, std::nullptr_t) noexcept
{
    return static_cast<bool>(pointer);
}

// MakeShrd<T>(args...) - создаёт объект T(args...)
template <typename T, typename... Args>
std::enable_if_t<!std::is_array<T>::value, ShrdPtr<T>> MakeShrd(Args&&... args)
{
    return ShrdPtr<T>(new T(std::forward<Args>(args)...));
}

// MakeShrd<T[]>(size) - создаёт массив из size элементов, инициализированных по умолчанию
template <typename T>
std::enable_if_t<std::is_array<T>::value && std::extent<T>::value == 0, ShrdPtr<T>> MakeShrd(std::size_t size)
{
    using ElementType = std::remove_extent_t<T>;
    return ShrdPtr<T>(new ElementType[size]());
}
