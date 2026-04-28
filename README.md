# shade

A C++23 callable wrapper. Header-only, move-only, no heap unless the stored target is too large to fit inline.

This covers the same ground as `std::function` but the storage, dispatch, and lifetime paths are written from scratch. No inheritance from a base interface, no internal use of `std::function`, no dependencies.

## how it works

When you store a callable, shade writes it into a 32-byte aligned buffer using placement new. If the callable is larger than 32 bytes, it heap-allocates. Either way, shade generates a small static vtable for that specific type with three function pointers: invoke, destroy, and move. The wrapper holds a pointer to that vtable and a pointer to the stored object. That is the entire dispatch mechanism.

Moving the wrapper is handled differently depending on where the target lives. Inline targets get move-constructed into the destination buffer and then destroyed at the source. Heap targets just transfer the pointer. There is no ref-counting, no shared ownership.

The `const`, `noexcept`, `&`, and `&&` qualifiers live in the signature type itself. The compiler enforces them at the call site. You do not need runtime checks for any of this.

## usage

```cpp
#include <shade/function.hpp>

// basic
shade::function<int(int)> f = [bias = 4](int x) { return x + bias; };
f(3); // 7

// const -- only accepts callables with a const call operator
// trying to store a mutable lambda here is a compile error
shade::function<int(int) const> g = [](int x) { return x * 2; };

// noexcept -- empty call terminates instead of throwing
shade::function<int(int) const noexcept> h = [](int x) noexcept { return x; };

// lvalue-ref qualified -- operator() only works on an lvalue
// calling on a moved-from wrapper is a compile error
shade::function<int(int) &> lv = [](int x) { return x; };
lv(3);              // ok
std::move(lv)(3);   // compile error

// rvalue-ref qualified -- one shot, operator() only works on an rvalue
// the stored callable is invoked as an rvalue so it can consume its own state
shade::function<int(int) &&> rv = [](int x) { return x; };
std::move(rv)(3);   // ok
rv(3);              // compile error

// in-place construction -- avoids a move
shade::function<int(int)> e(std::in_place_type<MyFunctor>, arg1, arg2);

// check storage location at runtime
f.uses_inline_storage(); // true if target fits in the 32-byte buffer

// reset and swap
f.reset();
shade::swap(f, g);
```

All eight signature forms:

```
R(Args...)
R(Args...) noexcept
R(Args...) const
R(Args...) const noexcept
R(Args...) &
R(Args...) & noexcept
R(Args...) &&
R(Args...) && noexcept
```

## empty call behavior

If you call a wrapper that holds no target, the behavior depends on the signature:

- non-noexcept: throws `std::bad_function_call`
- noexcept: calls `std::terminate`

## build

Requires CMake 3.20 and a C++23 compiler.

```
cmake --preset default
cmake --build build
```

Run tests:

```
.\build\shade_tests.exe
```

Run benchmark:

```
.\build\shade_bench.exe
```

## benchmark

Numbers from an ARM64 machine. Your numbers will differ. The benchmark compares direct calls, shade dispatch, and std::function dispatch on both small (inline) and large (heap) targets.

```
direct small call           1.06 ns/op
shade small call            1.70 ns/op
std small call              1.71 ns/op

direct large call           1.02 ns/op
shade large call            1.74 ns/op
std large call              1.72 ns/op

shade small build           3.20 ns/op
std small build             3.15 ns/op

shade large build           26.83 ns/op
std large build             26.80 ns/op
```

Dispatch cost for inline targets matches `std::function`. Dispatch cost for heap targets also matches. Construction cost is within noise for both sizes. The direct call baseline is there so you can see how much the type-erased dispatch adds over a plain function call.

## files

```
include/shade/function.hpp    the whole library, one header
tests/shade_tests.cpp         tests with no external dependencies
examples/demo.cpp             short usage example
benchmarks/dispatch_bench.cpp shade vs std::function on call and construction cost
```
