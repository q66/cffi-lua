#ifndef UTIL_HH
#define UTIL_HH

namespace util {

namespace detail {
    template<typename T> struct remove_ref { using type = T; };
    template<typename T> struct remove_ref<T &> { using type = T; };
    template<typename T> struct remove_ref<T &&> { using type = T; };
} /* namespace detail */

template<typename T> using remove_ref_t = typename detail::remove_ref<T>::type;

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
