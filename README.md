# shade

A C++23 callable wrapper. Header-only, no heap unless the target is too large to fit inline, move-only.

This covers the same ground as `std::function` but the storage, dispatch, and lifetime paths are written from scratch. There is no inheritance from a base interface and no internal use of `std::function`.

## what it does

Callables up to 32 bytes live in an inline buffer. Larger ones get heap-allocated. Each stored type gets a small static vtable with three slots: invoke, destroy, and move. The vtable pointer is the only overhead added to the stored object.

`const`, `noexcept`, and ref qualifiers live in the signature type itself, not in extra template flags.

```cpp
#include <shade/function.hpp>

// basic
shade::function<int(int)> f = [bias = 4](int x) { return x + bias; };

// const callable -- rejects mutable call operators at compile time
shade::function<int(int) const> g = [](int x) { return x * 2; };

// noexcept in the signature -- terminates on empty call instead of throwing
shade::function<int(int) const noexcept> h = [](int x) noexcept { return x; };

// lvalue-only -- calling on a moved-from wrapper is a compile error
shade::function<int(int) &> lv = [](int x) { return x; };
lv(3); // ok
std::move(lv)(3); // compile error

// one-shot -- operator() only works on an rvalue
shade::function<int(int) &&> rv = [](int x) { return x; };
std::move(rv)(3); // ok
rv(3); // compile error
```

All eight signature forms are supported:

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

Other things on the wrapper: `uses_inline_storage()`, `reset()`, `emplace<T>(...)`, `swap()`.

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

Numbers from an ARM64 machine. Your numbers will differ.

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

Small targets (fit inline) show the same dispatch cost as `std::function`. Large targets (heap-allocated) also match. Construction cost for both is within noise.

## files

```
include/shade/function.hpp   -- the whole library
tests/shade_tests.cpp        -- tests, no external deps
examples/demo.cpp            -- short usage example
benchmarks/dispatch_bench.cpp
```

## license

MIT
