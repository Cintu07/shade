#include <shade/function.hpp>

#include <cassert>
#include <memory>
#include <string>
#include <type_traits>

namespace {

int square(int value) {
    return value * value;
}

struct BigFunctor {
    char padding[128]{};
    int factor = 1;

    explicit BigFunctor(int value) : factor(value) {}

    int operator()(int value) const noexcept {
        return value * factor;
    }
};

struct MutableCounter {
    int count = 0;

    int operator()(int step) {
        count += step;
        return count;
    }
};

struct ConstCounter {
    int operator()(int value) const {
        return value + 7;
    }
};

struct MoveOnlyAdder {
    std::unique_ptr<int> bias;

    explicit MoveOnlyAdder(int value) : bias(std::make_unique<int>(value)) {}

    int operator()(int value) {
        return value + *bias;
    }
};

struct Tracking {
    static inline int live = 0;

    int value = 0;

    explicit Tracking(int initial) : value(initial) {
        ++live;
    }

    Tracking(Tracking&& other) noexcept : value(other.value) {
        other.value = 0;
        ++live;
    }

    Tracking& operator=(Tracking&&) = delete;

    ~Tracking() {
        --live;
    }

    int operator()(int step) {
        value += step;
        return value;
    }
};

static_assert(!std::is_copy_constructible_v<shade::function<int(int)>>);
static_assert(std::is_constructible_v<shade::function<int(int)>, decltype(&square)>);
static_assert(std::is_constructible_v<shade::function<int(int)>, MoveOnlyAdder>);
static_assert(std::is_constructible_v<shade::function<int(int) const>, ConstCounter>);
static_assert(!std::is_constructible_v<shade::function<int(int) const>, MutableCounter>);
static_assert(!std::is_invocable_v<const shade::function<int(int)>&, int>);
static_assert(std::is_invocable_v<const shade::function<int(int) const>&, int>);
static_assert(std::is_nothrow_invocable_v<shade::function<int(int) const noexcept>&, int>);
static_assert(std::is_invocable_v<shade::function<int(int) &>&, int>);
static_assert(!std::is_invocable_v<shade::function<int(int) &>&&, int>);
static_assert(!std::is_invocable_v<shade::function<int(int) &&>&, int>);
static_assert(std::is_invocable_v<shade::function<int(int) &&>&&, int>);

} // namespace

int main() {
    shade::function<int(int)> from_pointer = &square;
    assert(from_pointer.uses_inline_storage());
    assert(from_pointer(7) == 49);

    shade::function<int(int)> small = [bias = 3](int value) {
        return value + bias;
    };
    assert(small.uses_inline_storage());
    assert(small(4) == 7);

    shade::function<int(int) const noexcept> large = BigFunctor{5};
    assert(!large.uses_inline_storage());
    assert(large(6) == 30);

    shade::function<int(int)> counter = MutableCounter{};
    assert(counter(3) == 3);
    assert(counter(2) == 5);

    const shade::function<int(int) const> constant = ConstCounter{};
    assert(constant(5) == 12);

    shade::function<int(int)> move_only = MoveOnlyAdder{9};
    assert(move_only.uses_inline_storage());
    assert(move_only(1) == 10);

    auto moved = std::move(move_only);
    assert(!move_only);
    assert(moved(5) == 14);

    shade::function<void(std::string&)> mutate = [](std::string& value) {
        value += "-shade";
    };
    std::string text = "hot";
    mutate(text);
    assert(text == "hot-shade");

    {
        shade::function<int(int)> tracked(std::in_place_type<Tracking>, 4);
        assert(tracked.uses_inline_storage());
        assert(Tracking::live == 1);
        assert(tracked(2) == 6);

        auto transferred = std::move(tracked);
        assert(!tracked);
        assert(Tracking::live == 1);
        assert(transferred(3) == 9);

        transferred.reset();
        assert(Tracking::live == 0);
    }

    shade::function<int(int)> swapped_left = [bias = 1](int value) {
        return value + bias;
    };
    shade::function<int(int)> swapped_right = [bias = 8](int value) {
        return value + bias;
    };
    shade::swap(swapped_left, swapped_right);
    assert(swapped_left(2) == 10);
    assert(swapped_right(2) == 3);

    shade::function<int(int) &> lref_fn = [bias = 5](int value) { return value + bias; };
    assert(lref_fn(3) == 8);

    shade::function<int(int) &&> rref_fn = [bias = 5](int value) { return value + bias; };
    assert(std::move(rref_fn)(3) == 8);

    return 0;
}