#pragma once

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

// UnqPtr<T>, UnqPtr<T[]> - "ведущий" указатель, единоличный владелец объекта
// (аналог std::unique_ptr). Копирование запрещено, владение передаётся только
// перемещением (move), поэтому два UnqPtr никогда не указывают на один объект.

// ---------------------------------------------------------------------------
// UnqPtr<T> - владеет одиночным объектом, удаляет его через delete
// ---------------------------------------------------------------------------
template <typename T>
class UnqPtr
{
public:
    UnqPtr() noexcept : pointer_(nullptr)
    {
    }

    UnqPtr(std::nullptr_t) noexcept : pointer_(nullptr)
    {
    }

    // Берёт во владение объект, созданный через new
    explicit UnqPtr(T* pointer) noexcept : pointer_(pointer)
    {
    }

    UnqPtr(const UnqPtr&) = delete;
    UnqPtr& operator=(const UnqPtr&) = delete;

    UnqPtr(UnqPtr&& other) noexcept : pointer_(other.Release())
    {
    }

    // Подтипизация: UnqPtr<Base> можно получить из UnqPtr<Derived>,
    // для несвязанных типов конструктор исключается из перегрузки (SFINAE)
    template <typename U, typename = std::enable_if_t<std::is_convertible<U*, T*>::value>>
    UnqPtr(UnqPtr<U>&& other) noexcept : pointer_(other.Release())
    {
    }

    ~UnqPtr()
    {
        delete pointer_;
    }

    UnqPtr& operator=(UnqPtr&& other) noexcept
    {
        if (this != &other)
        {
            Reset(other.Release());
        }
        return *this;
    }

    template <typename U, typename = std::enable_if_t<std::is_convertible<U*, T*>::value>>
    UnqPtr& operator=(UnqPtr<U>&& other) noexcept
    {
        Reset(other.Release());
        return *this;
    }

    UnqPtr& operator=(std::nullptr_t) noexcept
    {
        Reset();
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

    explicit operator bool() const noexcept
    {
        return pointer_ != nullptr;
    }

    // Отказывается от владения: объект больше не будет удалён этим указателем
    T* Release() noexcept
    {
        T* released = pointer_;
        pointer_ = nullptr;
        return released;
    }

    // Удаляет текущий объект и берёт во владение новый
    void Reset(T* pointer = nullptr) noexcept
    {
        if (pointer == pointer_)
        {
            return;
        }
        T* old = pointer_;
        pointer_ = pointer;
        delete old;
    }

    void Swap(UnqPtr& other) noexcept
    {
        std::swap(pointer_, other.pointer_);
    }

private:
    void CheckNotNull() const
    {
        if (pointer_ == nullptr)
        {
            throw std::logic_error("UnqPtr: dereferencing null pointer");
        }
    }

    T* pointer_;
};

// ---------------------------------------------------------------------------
// UnqPtr<T[]> - владеет массивом, удаляет его через delete[].
// Подтипизации для массивов нет: Derived[] нельзя трактовать как Base[]
// (разные размеры элементов ломают индексацию и delete[]).
// ---------------------------------------------------------------------------
template <typename T>
class UnqPtr<T[]>
{
public:
    UnqPtr() noexcept : pointer_(nullptr)
    {
    }

    UnqPtr(std::nullptr_t) noexcept : pointer_(nullptr)
    {
    }

    // Берёт во владение массив, созданный через new T[n]
    explicit UnqPtr(T* pointer) noexcept : pointer_(pointer)
    {
    }

    // Запрещает UnqPtr<Base[]>(new Derived[n])
    template <typename U>
    explicit UnqPtr(U* pointer) = delete;

    UnqPtr(const UnqPtr&) = delete;
    UnqPtr& operator=(const UnqPtr&) = delete;

    UnqPtr(UnqPtr&& other) noexcept : pointer_(other.Release())
    {
    }

    ~UnqPtr()
    {
        delete[] pointer_;
    }

    UnqPtr& operator=(UnqPtr&& other) noexcept
    {
        if (this != &other)
        {
            Reset(other.Release());
        }
        return *this;
    }

    UnqPtr& operator=(std::nullptr_t) noexcept
    {
        Reset();
        return *this;
    }

    T& operator[](std::size_t index) const
    {
        if (pointer_ == nullptr)
        {
            throw std::logic_error("UnqPtr: indexing null array");
        }
        return pointer_[index];
    }

    T* Get() const noexcept
    {
        return pointer_;
    }

    explicit operator bool() const noexcept
    {
        return pointer_ != nullptr;
    }

    T* Release() noexcept
    {
        T* released = pointer_;
        pointer_ = nullptr;
        return released;
    }

    void Reset(T* pointer = nullptr) noexcept
    {
        if (pointer == pointer_)
        {
            return;
        }
        T* old = pointer_;
        pointer_ = pointer;
        delete[] old;
    }

    void Swap(UnqPtr& other) noexcept
    {
        std::swap(pointer_, other.pointer_);
    }

private:
    T* pointer_;
};

// ---------------------------------------------------------------------------
// Фабрики (аналоги std::make_unique)
// ---------------------------------------------------------------------------

// MakeUnq<T>(args...) - создаёт объект T(args...)
template <typename T, typename... Args>
std::enable_if_t<!std::is_array<T>::value, UnqPtr<T>> MakeUnq(Args&&... args)
{
    return UnqPtr<T>(new T(std::forward<Args>(args)...));
}

// MakeUnq<T[]>(size) - создаёт массив из size элементов, инициализированных по умолчанию
template <typename T>
std::enable_if_t<std::is_array<T>::value && std::extent<T>::value == 0, UnqPtr<T>> MakeUnq(std::size_t size)
{
    using ElementType = std::remove_extent_t<T>;
    return UnqPtr<T>(new ElementType[size]());
}
