#pragma once

#include <exception>
#include <new>
#include <type_traits>
#include <utility>

namespace boost {

struct none_t {
    explicit constexpr none_t(int) {}
};

constexpr none_t none{0};

class bad_optional_access : public std::exception {
public:
    const char* what() const noexcept override { return "bad optional access"; }
};

template <typename T>
class optional {
public:
    using value_type = T;

    constexpr optional() noexcept : hasValue_{false} {}
    constexpr optional(none_t) noexcept : hasValue_{false} {}

    optional(const T& value) : hasValue_{false} {
        emplace(value);
    }

    optional(T&& value) noexcept(std::is_nothrow_move_constructible<T>::value)
        : hasValue_{false} {
        emplace(std::move(value));
    }

    optional(const optional& other) : hasValue_{false} {
        if (other.has_value()) {
            emplace(*other);
        }
    }

    optional(optional&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
        : hasValue_{false} {
        if (other.has_value()) {
            emplace(std::move(*other));
            other.reset();
        }
    }

    ~optional() { reset(); }

    optional& operator=(none_t) noexcept {
        reset();
        return *this;
    }

    optional& operator=(const optional& other) {
        if (this != &other) {
            if (other.has_value()) {
                emplace(*other);
            } else {
                reset();
            }
        }
        return *this;
    }

    optional& operator=(optional&& other) noexcept(std::is_nothrow_move_assignable<T>::value&&
            std::is_nothrow_move_constructible<T>::value) {
        if (this != &other) {
            if (other.has_value()) {
                emplace(std::move(*other));
                other.reset();
            } else {
                reset();
            }
        }
        return *this;
    }

    optional& operator=(const T& value) {
        emplace(value);
        return *this;
    }

    optional& operator=(T&& value) {
        emplace(std::move(value));
        return *this;
    }

    template <typename... Args>
    T& emplace(Args&&... args) {
        reset();
        ::new (static_cast<void*>(&storage_)) T(std::forward<Args>(args)...);
        hasValue_ = true;
        return **this;
    }

    constexpr bool has_value() const noexcept { return hasValue_; }

    constexpr explicit operator bool() const noexcept { return hasValue_; }

    T& value() {
        if (!hasValue_) {
            throw bad_optional_access{};
        }
        return **this;
    }

    const T& value() const {
        if (!hasValue_) {
            throw bad_optional_access{};
        }
        return **this;
    }

    T& operator*() { return *reinterpret_cast<T*>(&storage_); }
    const T& operator*() const { return *reinterpret_cast<const T*>(&storage_); }

    T* operator->() { return &**this; }
    const T* operator->() const { return &**this; }

    template <typename U>
    T value_or(U&& defaultValue) const {
        if (hasValue_) {
            return **this;
        }
        return static_cast<T>(std::forward<U>(defaultValue));
    }

    void reset() noexcept {
        if (hasValue_) {
            (**this).~T();
            hasValue_ = false;
        }
    }

private:
    typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_;
    bool hasValue_;
};

} // namespace boost

