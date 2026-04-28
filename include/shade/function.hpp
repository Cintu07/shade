#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace shade {

template<class Signature, std::size_t InlineSize = 32, std::size_t InlineAlign = alignof(std::max_align_t)>
class move_only_function;

namespace detail {

template<class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template<bool Const, class T>
using callable_ref_t = std::conditional_t<Const, const T&, T&>;

template<bool Noexcept>
[[noreturn]] inline void empty_call() noexcept(Noexcept) {
    if constexpr (Noexcept) {
        std::terminate();
    } else {
        throw std::bad_function_call();
    }
}

template<class R, bool Noexcept, bool Const, bool Rvalue, std::size_t InlineSize, std::size_t InlineAlign, class... Args>
class function_base {
    static_assert(InlineSize > 0, "InlineSize must be greater than zero");
    static_assert(InlineAlign > 0, "InlineAlign must be greater than zero");
    static_assert((InlineAlign & (InlineAlign - 1)) == 0, "InlineAlign must be a power of two");

protected:
    struct storage_type {
        alignas(InlineAlign) std::byte bytes[InlineSize];
    };

    using invoke_object_type = std::conditional_t<Const, const void*, void*>;
    using invoke_fn = std::conditional_t<
        Noexcept,
        std::conditional_t<Const, R (*)(const void*, Args&&...) noexcept, R (*)(void*, Args&&...) noexcept>,
        std::conditional_t<Const, R (*)(const void*, Args&&...), R (*)(void*, Args&&...)>
    >;

    struct vtable {
        invoke_fn invoke;
        void (*destroy)(void*) noexcept;
        void (*move)(storage_type&, void*&, void*&);
    };

    template<class T>
    static constexpr bool fits_inline_v = sizeof(T) <= InlineSize && alignof(T) <= InlineAlign;

    template<class T>
    using callable_qual_t = std::conditional_t<Rvalue, T&&, callable_ref_t<Const, T>>;

    template<class T>
    static constexpr bool target_compatible_v =
        std::is_invocable_r_v<R, callable_qual_t<T>, Args...> &&
        (!Noexcept || std::is_nothrow_invocable_r_v<R, callable_qual_t<T>, Args...>);

public:
    static constexpr std::size_t inline_capacity = InlineSize;
    static constexpr std::size_t inline_alignment = InlineAlign;

    function_base() noexcept = default;
    function_base(std::nullptr_t) noexcept {}

    function_base(const function_base&) = delete;
    function_base& operator=(const function_base&) = delete;

    function_base(function_base&& other) {
        move_from(std::move(other));
    }

    function_base& operator=(function_base&& other) {
        if (this != &other) {
            reset();
            move_from(std::move(other));
        }
        return *this;
    }

    ~function_base() {
        reset();
    }

    bool has_value() const noexcept {
        return table_ != nullptr;
    }

    bool uses_inline_storage() const noexcept {
        return table_ != nullptr && uses_inline_;
    }

    void reset() noexcept {
        if (!table_) {
            return;
        }

        table_->destroy(object_);
        object_ = nullptr;
        table_ = nullptr;
        uses_inline_ = false;
    }

    void swap(function_base& other) {
        if (this == &other) {
            return;
        }

        function_base temp(std::move(other));
        other = std::move(*this);
        *this = std::move(temp);
    }

    template<class T, class... ConstructorArgs>
    requires(
        std::is_constructible_v<remove_cvref_t<T>, ConstructorArgs...> &&
        target_compatible_v<remove_cvref_t<T>>
    )
    remove_cvref_t<T>& emplace(ConstructorArgs&&... args) {
        using target_type = remove_cvref_t<T>;

        reset();

        object_ = construct<target_type>(std::forward<ConstructorArgs>(args)...);
        table_ = table_for<target_type>();
        uses_inline_ = fits_inline_v<target_type>;
        return *static_cast<target_type*>(object_);
    }

protected:
    template<class F>
    void assign(F&& f) {
        using target_type = remove_cvref_t<F>;
        emplace<target_type>(std::forward<F>(f));
    }

    R invoke_impl(Args... args) const noexcept(Noexcept) {
        if (!table_) {
            empty_call<Noexcept>();
        }

        if constexpr (std::is_void_v<R>) {
            table_->invoke(invoke_object(), std::forward<Args>(args)...);
            return;
        } else {
            return table_->invoke(invoke_object(), std::forward<Args>(args)...);
        }
    }

private:
    template<class T, class... ConstructorArgs>
    void* construct(ConstructorArgs&&... args) {
        if constexpr (fits_inline_v<T>) {
            auto* target = ::new (static_cast<void*>(storage_.bytes)) T(std::forward<ConstructorArgs>(args)...);
            return std::launder(target);
        } else {
            return new T(std::forward<ConstructorArgs>(args)...);
        }
    }

    template<class T>
    static auto table_for() noexcept -> const vtable* {
        static const vtable table{
            &invoke_target<T>,
            &destroy_target<T>,
            &move_target<T>,
        };

        return &table;
    }

    template<class T>
    static R invoke_target(invoke_object_type object, Args&&... args) noexcept(Noexcept) {
        if constexpr (Rvalue) {
            if constexpr (std::is_void_v<R>) {
                std::invoke(std::move(*static_cast<T*>(object)), std::forward<Args>(args)...);
                return;
            } else {
                return std::invoke(std::move(*static_cast<T*>(object)), std::forward<Args>(args)...);
            }
        } else {
            auto& target = *static_cast<std::conditional_t<Const, const T, T>*>(object);
            if constexpr (std::is_void_v<R>) {
                std::invoke(target, std::forward<Args>(args)...);
                return;
            } else {
                return std::invoke(target, std::forward<Args>(args)...);
            }
        }
    }

    template<class T>
    static void destroy_target(void* object) noexcept {
        if (!object) {
            return;
        }

        if constexpr (fits_inline_v<T>) {
            static_cast<T*>(object)->~T();
        } else {
            delete static_cast<T*>(object);
        }
    }

    template<class T>
    static void move_target(storage_type& destination, void*& destination_object, void*& source_object) {
        if constexpr (fits_inline_v<T>) {
            auto* source = static_cast<T*>(source_object);
            auto* target = ::new (static_cast<void*>(destination.bytes)) T(std::move(*source));
            destination_object = std::launder(target);
            source->~T();
        } else {
            destination_object = source_object;
        }

        source_object = nullptr;
    }

    void move_from(function_base&& other) {
        if (!other.table_) {
            return;
        }

        other.table_->move(storage_, object_, other.object_);
        table_ = other.table_;
        uses_inline_ = other.uses_inline_;

        other.table_ = nullptr;
        other.uses_inline_ = false;
    }

    invoke_object_type invoke_object() const noexcept {
        return object_;
    }

    storage_type storage_{};
    void* object_ = nullptr;
    const vtable* table_ = nullptr;
    bool uses_inline_ = false;
};

template<class Derived, class Base>
class function_interface : public Base {
public:
    using Base::Base;
    using Base::emplace;
    using Base::reset;

    function_interface() noexcept = default;
    function_interface(std::nullptr_t) noexcept : Base(nullptr) {}

    function_interface(function_interface&&) = default;
    function_interface& operator=(function_interface&&) = default;

    explicit operator bool() const noexcept {
        return this->has_value();
    }

    bool has_value() const noexcept {
        return Base::has_value();
    }

    bool uses_inline_storage() const noexcept {
        return Base::uses_inline_storage();
    }

    Derived& operator=(std::nullptr_t) noexcept {
        this->reset();
        return static_cast<Derived&>(*this);
    }

    void swap(Derived& other) {
        Base::swap(other);
    }

protected:
    template<class T>
    static constexpr bool target_compatible_v = Base::template target_compatible_v<T>;

    template<class F>
    static constexpr bool accepted_input_v =
        !std::is_same_v<remove_cvref_t<F>, Derived> &&
        !std::is_same_v<remove_cvref_t<F>, std::nullptr_t> &&
        std::is_constructible_v<remove_cvref_t<F>, F> &&
        Base::template target_compatible_v<remove_cvref_t<F>>;
};

} // namespace detail

template<class R, class... Args, std::size_t InlineSize, std::size_t InlineAlign>
class move_only_function<R(Args...), InlineSize, InlineAlign>
    : public detail::function_interface<
          move_only_function<R(Args...), InlineSize, InlineAlign>,
          detail::function_base<R, false, false, false, InlineSize, InlineAlign, Args...>> {
    using base = detail::function_interface<
        move_only_function<R(Args...), InlineSize, InlineAlign>,
        detail::function_base<R, false, false, false, InlineSize, InlineAlign, Args...>>;

public:
    using base::base;

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function(F&& f) {
        this->template assign<F>(std::forward<F>(f));
    }

    template<class T, class... ConstructorArgs>
    requires(
        std::is_constructible_v<detail::remove_cvref_t<T>, ConstructorArgs...> &&
        base::template target_compatible_v<detail::remove_cvref_t<T>>
    )
    explicit move_only_function(std::in_place_type_t<T>, ConstructorArgs&&... args) {
        this->template emplace<T>(std::forward<ConstructorArgs>(args)...);
    }

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function& operator=(F&& f) {
        this->template assign<F>(std::forward<F>(f));
        return *this;
    }

    R operator()(Args... args) {
        return this->invoke_impl(std::forward<Args>(args)...);
    }
};

template<class R, class... Args, std::size_t InlineSize, std::size_t InlineAlign>
class move_only_function<R(Args...) noexcept, InlineSize, InlineAlign>
    : public detail::function_interface<
          move_only_function<R(Args...) noexcept, InlineSize, InlineAlign>,
          detail::function_base<R, true, false, false, InlineSize, InlineAlign, Args...>> {
    using base = detail::function_interface<
        move_only_function<R(Args...) noexcept, InlineSize, InlineAlign>,
        detail::function_base<R, true, false, false, InlineSize, InlineAlign, Args...>>;

public:
    using base::base;

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function(F&& f) {
        this->template assign<F>(std::forward<F>(f));
    }

    template<class T, class... ConstructorArgs>
    requires(
        std::is_constructible_v<detail::remove_cvref_t<T>, ConstructorArgs...> &&
        base::template target_compatible_v<detail::remove_cvref_t<T>>
    )
    explicit move_only_function(std::in_place_type_t<T>, ConstructorArgs&&... args) {
        this->template emplace<T>(std::forward<ConstructorArgs>(args)...);
    }

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function& operator=(F&& f) {
        this->template assign<F>(std::forward<F>(f));
        return *this;
    }

    R operator()(Args... args) noexcept {
        return this->invoke_impl(std::forward<Args>(args)...);
    }
};

template<class R, class... Args, std::size_t InlineSize, std::size_t InlineAlign>
class move_only_function<R(Args...) const, InlineSize, InlineAlign>
    : public detail::function_interface<
          move_only_function<R(Args...) const, InlineSize, InlineAlign>,
          detail::function_base<R, false, true, false, InlineSize, InlineAlign, Args...>> {
    using base = detail::function_interface<
        move_only_function<R(Args...) const, InlineSize, InlineAlign>,
        detail::function_base<R, false, true, false, InlineSize, InlineAlign, Args...>>;

public:
    using base::base;

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function(F&& f) {
        this->template assign<F>(std::forward<F>(f));
    }

    template<class T, class... ConstructorArgs>
    requires(
        std::is_constructible_v<detail::remove_cvref_t<T>, ConstructorArgs...> &&
        base::template target_compatible_v<detail::remove_cvref_t<T>>
    )
    explicit move_only_function(std::in_place_type_t<T>, ConstructorArgs&&... args) {
        this->template emplace<T>(std::forward<ConstructorArgs>(args)...);
    }

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function& operator=(F&& f) {
        this->template assign<F>(std::forward<F>(f));
        return *this;
    }

    R operator()(Args... args) const {
        return this->invoke_impl(std::forward<Args>(args)...);
    }
};

template<class R, class... Args, std::size_t InlineSize, std::size_t InlineAlign>
class move_only_function<R(Args...) const noexcept, InlineSize, InlineAlign>
    : public detail::function_interface<
          move_only_function<R(Args...) const noexcept, InlineSize, InlineAlign>,
          detail::function_base<R, true, true, false, InlineSize, InlineAlign, Args...>> {
    using base = detail::function_interface<
        move_only_function<R(Args...) const noexcept, InlineSize, InlineAlign>,
        detail::function_base<R, true, true, false, InlineSize, InlineAlign, Args...>>;

public:
    using base::base;

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function(F&& f) {
        this->template assign<F>(std::forward<F>(f));
    }

    template<class T, class... ConstructorArgs>
    requires(
        std::is_constructible_v<detail::remove_cvref_t<T>, ConstructorArgs...> &&
        base::template target_compatible_v<detail::remove_cvref_t<T>>
    )
    explicit move_only_function(std::in_place_type_t<T>, ConstructorArgs&&... args) {
        this->template emplace<T>(std::forward<ConstructorArgs>(args)...);
    }

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function& operator=(F&& f) {
        this->template assign<F>(std::forward<F>(f));
        return *this;
    }

    R operator()(Args... args) const noexcept {
        return this->invoke_impl(std::forward<Args>(args)...);
    }
};

template<class R, class... Args, std::size_t InlineSize, std::size_t InlineAlign>
class move_only_function<R(Args...) &, InlineSize, InlineAlign>
    : public detail::function_interface<
          move_only_function<R(Args...) &, InlineSize, InlineAlign>,
          detail::function_base<R, false, false, false, InlineSize, InlineAlign, Args...>> {
    using base = detail::function_interface<
        move_only_function<R(Args...) &, InlineSize, InlineAlign>,
        detail::function_base<R, false, false, false, InlineSize, InlineAlign, Args...>>;

public:
    using base::base;

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function(F&& f) {
        this->template assign<F>(std::forward<F>(f));
    }

    template<class T, class... ConstructorArgs>
    requires(
        std::is_constructible_v<detail::remove_cvref_t<T>, ConstructorArgs...> &&
        base::template target_compatible_v<detail::remove_cvref_t<T>>
    )
    explicit move_only_function(std::in_place_type_t<T>, ConstructorArgs&&... args) {
        this->template emplace<T>(std::forward<ConstructorArgs>(args)...);
    }

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function& operator=(F&& f) {
        this->template assign<F>(std::forward<F>(f));
        return *this;
    }

    R operator()(Args... args) & {
        return this->invoke_impl(std::forward<Args>(args)...);
    }
};

template<class R, class... Args, std::size_t InlineSize, std::size_t InlineAlign>
class move_only_function<R(Args...) & noexcept, InlineSize, InlineAlign>
    : public detail::function_interface<
          move_only_function<R(Args...) & noexcept, InlineSize, InlineAlign>,
          detail::function_base<R, true, false, false, InlineSize, InlineAlign, Args...>> {
    using base = detail::function_interface<
        move_only_function<R(Args...) & noexcept, InlineSize, InlineAlign>,
        detail::function_base<R, true, false, false, InlineSize, InlineAlign, Args...>>;

public:
    using base::base;

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function(F&& f) {
        this->template assign<F>(std::forward<F>(f));
    }

    template<class T, class... ConstructorArgs>
    requires(
        std::is_constructible_v<detail::remove_cvref_t<T>, ConstructorArgs...> &&
        base::template target_compatible_v<detail::remove_cvref_t<T>>
    )
    explicit move_only_function(std::in_place_type_t<T>, ConstructorArgs&&... args) {
        this->template emplace<T>(std::forward<ConstructorArgs>(args)...);
    }

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function& operator=(F&& f) {
        this->template assign<F>(std::forward<F>(f));
        return *this;
    }

    R operator()(Args... args) & noexcept {
        return this->invoke_impl(std::forward<Args>(args)...);
    }
};

// one-shot: operator() requires rvalue context; stored callable is invoked as rvalue
template<class R, class... Args, std::size_t InlineSize, std::size_t InlineAlign>
class move_only_function<R(Args...) &&, InlineSize, InlineAlign>
    : public detail::function_interface<
          move_only_function<R(Args...) &&, InlineSize, InlineAlign>,
          detail::function_base<R, false, false, true, InlineSize, InlineAlign, Args...>> {
    using base = detail::function_interface<
        move_only_function<R(Args...) &&, InlineSize, InlineAlign>,
        detail::function_base<R, false, false, true, InlineSize, InlineAlign, Args...>>;

public:
    using base::base;

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function(F&& f) {
        this->template assign<F>(std::forward<F>(f));
    }

    template<class T, class... ConstructorArgs>
    requires(
        std::is_constructible_v<detail::remove_cvref_t<T>, ConstructorArgs...> &&
        base::template target_compatible_v<detail::remove_cvref_t<T>>
    )
    explicit move_only_function(std::in_place_type_t<T>, ConstructorArgs&&... args) {
        this->template emplace<T>(std::forward<ConstructorArgs>(args)...);
    }

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function& operator=(F&& f) {
        this->template assign<F>(std::forward<F>(f));
        return *this;
    }

    R operator()(Args... args) && {
        return this->invoke_impl(std::forward<Args>(args)...);
    }
};

template<class R, class... Args, std::size_t InlineSize, std::size_t InlineAlign>
class move_only_function<R(Args...) && noexcept, InlineSize, InlineAlign>
    : public detail::function_interface<
          move_only_function<R(Args...) && noexcept, InlineSize, InlineAlign>,
          detail::function_base<R, true, false, true, InlineSize, InlineAlign, Args...>> {
    using base = detail::function_interface<
        move_only_function<R(Args...) && noexcept, InlineSize, InlineAlign>,
        detail::function_base<R, true, false, true, InlineSize, InlineAlign, Args...>>;

public:
    using base::base;

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function(F&& f) {
        this->template assign<F>(std::forward<F>(f));
    }

    template<class T, class... ConstructorArgs>
    requires(
        std::is_constructible_v<detail::remove_cvref_t<T>, ConstructorArgs...> &&
        base::template target_compatible_v<detail::remove_cvref_t<T>>
    )
    explicit move_only_function(std::in_place_type_t<T>, ConstructorArgs&&... args) {
        this->template emplace<T>(std::forward<ConstructorArgs>(args)...);
    }

    template<class F>
    requires(base::template accepted_input_v<F>)
    move_only_function& operator=(F&& f) {
        this->template assign<F>(std::forward<F>(f));
        return *this;
    }

    R operator()(Args... args) && noexcept {
        return this->invoke_impl(std::forward<Args>(args)...);
    }
};

template<class Signature, std::size_t InlineSize = 32, std::size_t InlineAlign = alignof(std::max_align_t)>
using function = move_only_function<Signature, InlineSize, InlineAlign>;

template<class Signature, std::size_t InlineSize, std::size_t InlineAlign>
void swap(move_only_function<Signature, InlineSize, InlineAlign>& left,
          move_only_function<Signature, InlineSize, InlineAlign>& right) {
    left.swap(right);
}

} // namespace shade