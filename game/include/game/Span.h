#pragma once

#include <cstddef>

namespace ktd {

// A non-owning view over a contiguous run of T.
//
// It exists so World's accessors can stop naming std::vector. Every consumer --
// the renderer, the tests, the serializer -- only ever iterates, subscripts and
// asks for a size, so a view is all they were ever using. Naming the container
// in the signature meant that swapping std::vector for Pool's dense array would
// have been a breaking change at every call site; naming a view instead makes it
// a change to one line of World.h.
//
// std::span would do this job. It is not used because it arrived in C++20 with
// std::dynamic_extent and a template default that drags <span> and its
// machinery into every translation unit that includes World.h, for a type whose
// entire useful surface is four member functions. This is that surface.
template <typename T>
class Span {
public:
    constexpr Span() = default;
    constexpr Span(T* data, std::size_t size) : data_(data), size_(size) {}

    // Implicit from any contiguous container with data()/size(). Deliberately
    // not constrained further: the only things converted here are the game's
    // own storage types, and a concept would cost more to read than it saves.
    template <typename Container>
    constexpr Span(Container& c) : data_(c.data()),
                                   size_(c.size()) {}
    template <typename Container>
    constexpr Span(const Container& c) : data_(c.data()),
                                         size_(c.size()) {}

    constexpr T* begin() const { return data_; }
    constexpr T* end() const { return data_ + size_; }
    constexpr T* data() const { return data_; }
    constexpr std::size_t size() const { return size_; }
    constexpr bool empty() const { return size_ == 0; }
    constexpr T& operator[](std::size_t i) const { return data_[i]; }

private:
    T* data_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace ktd
