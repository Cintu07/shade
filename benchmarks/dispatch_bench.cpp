#include <shade/function.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace {

#if defined(_MSC_VER)
#define SHADE_NOINLINE __declspec(noinline)
#else
#define SHADE_NOINLINE __attribute__((noinline))
#endif

inline volatile int sink = 0;
constexpr int iterations = 12'000'000;

struct SmallAdder {
    int bias = 0;

    int operator()(int value) const noexcept {
        return value + bias;
    }
};

struct LargeAdder {
    std::array<std::byte, 96> padding{};
    int bias = 0;

    int operator()(int value) const noexcept {
        return value + bias;
    }
};

template<class F>
SHADE_NOINLINE int call_once(F& function, int value) {
    return function(value);
}

template<class Factory>
SHADE_NOINLINE auto make_one(Factory& factory) {
    return factory();
}

template<class F>
double bench_calls(F& function) {
    int total = 0;
    const auto start = std::chrono::steady_clock::now();

    for (int index = 0; index < iterations; ++index) {
        total += call_once(function, index);
    }

    const auto finish = std::chrono::steady_clock::now();
    sink = total;

    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count();
    return static_cast<double>(elapsed) / static_cast<double>(iterations);
}

template<class Factory>
double bench_construction(Factory factory) {
    using wrapper_type = std::invoke_result_t<Factory&>;

    int total = 0;
    const auto start = std::chrono::steady_clock::now();

    for (int index = 0; index < 5'000'000; ++index) {
        wrapper_type wrapper = make_one(factory);
        total += call_once(wrapper, index);
    }

    const auto finish = std::chrono::steady_clock::now();
    sink = total;

    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count();
    return static_cast<double>(elapsed) / 5'000'000.0;
}

void print_result(std::string_view label, double ns_per_op) {
    std::cout << std::left << std::setw(28) << label
              << std::right << std::fixed << std::setprecision(2)
              << ns_per_op << " ns/op\n";
}

} // namespace

int main() {
    const SmallAdder small{4};
    const LargeAdder large{ {}, 4 };

    shade::function<int(int)> shade_small = small;
    shade::function<int(int)> shade_large = large;

    std::function<int(int)> std_small = small;
    std::function<int(int)> std_large = large;

    std::cout << "SHADE benchmark\n";
    std::cout << "small inline: " << shade_small.uses_inline_storage() << '\n';
    std::cout << "large inline: " << shade_large.uses_inline_storage() << "\n\n";

    print_result("direct small call", bench_calls(small));
    print_result("shade small call", bench_calls(shade_small));
    print_result("std small call", bench_calls(std_small));
    print_result("direct large call", bench_calls(large));
    print_result("shade large call", bench_calls(shade_large));
    print_result("std large call", bench_calls(std_large));

    std::cout << '\n';

    print_result("shade small build", bench_construction([&]() {
        return shade::function<int(int)>{small};
    }));
    print_result("std small build", bench_construction([&]() {
        return std::function<int(int)>{small};
    }));
    print_result("shade large build", bench_construction([&]() {
        return shade::function<int(int)>{large};
    }));
    print_result("std large build", bench_construction([&]() {
        return std::function<int(int)>{large};
    }));

    return 0;
}