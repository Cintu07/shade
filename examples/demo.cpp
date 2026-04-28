#include <shade/function.hpp>

#include <array>
#include <cstddef>
#include <iostream>

struct HeavyStage {
    std::array<std::byte, 96> state{};

    int operator()(int value) const noexcept {
        return value * 4;
    }
};

int main() {
    shade::function<int(int)> fast = [bias = 2](int value) {
        return value + bias;
    };

    shade::function<int(int) const noexcept> heavy = HeavyStage{};

    std::cout << "fast inline: " << fast.uses_inline_storage() << '\n';
    std::cout << "fast result: " << fast(5) << '\n';
    std::cout << "heavy inline: " << heavy.uses_inline_storage() << '\n';
    std::cout << "heavy result: " << heavy(5) << '\n';

    return 0;
}