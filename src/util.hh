#ifndef UTIL_HH
#define UTIL_HH

#include <type_traits>

namespace util {

/* type traits */

namespace detail {
    template<typename T> struct remove_ref { using type = T; };
    template<typename T> struct remove_ref<T &> { using type = T; };
    template<typename T> struct remove_ref<T &&> { using type = T; };
}

template<typename T> using remove_ref_t = typename detail::remove_ref<T>::type;

template<typename T>
struct is_signed {
    static constexpr bool value = std::is_signed<T>::value;
};

/* move semantics */

template<typename T>
constexpr remove_ref_t<T> &&move(T &&v) noexcept {
    return static_cast<remove_ref_t<T> &&>(v);
}

template<typename T>
constexpr T &&forward(remove_ref_t<T> &v) noexcept {
    return static_cast<T &&>(v);
}

template<typename T>
constexpr T &&forward(remove_ref_t<T> &&v) noexcept {
    return static_cast<T &&>(v);
}

template<typename T, typename U = T>
constexpr T exchange(T &v, U &&nv) {
    T ov = move(v);
    v = forward<U>(nv);
    return ov;
}

} /* namespace util */

#endif /* UTIL_HH */
