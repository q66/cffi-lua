/* standard utilities used within the ffi
 *
 * these are mostly-compliant implementations of the standard c++ containers
 * and other utilities, meant to avoid the need for the c++ runtime/standard
 * library; they aren't meant to be general purpose and only implement what
 * we use, they don't provide documented exception safety guarantees and so
 * on either but that's fine since we barely use these at all
 */

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

template<typename T>
void swap(T &a, T &b) {
    auto o = move(a);
    a = move(b);
    b = move(o);
}

/* vector */

template<typename T>
struct vector {
    static constexpr size_t MIN_SIZE = 4;

    vector() {}
    ~vector() {
        drop();
    }

    vector(vector const &v) { *this = v; }
    vector(vector &&v) { *this = move(v); }

    vector &operator=(vector const &v) {
        resize(0);
        if (v.size() > capacity()) {
            reserve(v.size());
        }
        for (size_t i = 0; i < v.size(); ++i) {
            push_back(v[i]);
        }
        return *this;
    }

    vector &operator=(vector &&v) {
        swap(v);
        return *this;
    }

    void push_back(T const &v) {
        reserve(p_cap + 1);
        new (&p_buf[p_size++]) T(v);
    }

    void push_back(T &&v) {
        reserve(p_cap + 1);
        new (&p_buf[p_size++]) T(util::move(v));
    }

    void pop_back() {
        p_buf[p_size - 1].~T();
        --p_size;
    }

    template<typename ...A>
    T &emplace_back(A &&...args) {
        reserve(p_cap + 1);
        new (&p_buf[p_size]) T(util::forward<A>(args)...);
        return p_buf[p_size++];
    }

    void reserve(size_t n) {
        if (n <= p_cap) {
            return;
        }
        if (n < MIN_SIZE) {
            n = MIN_SIZE;
        }
        T *np = reinterpret_cast<T *>(new unsigned char[n * sizeof(T)]);
        if (p_cap) {
            for (size_t i = 0; i < p_size; ++i) {
                new (&np[i]) T(util::move(p_buf[i]));
                p_buf[i].~T();
            }
            delete[] reinterpret_cast<unsigned char *>(p_buf);
        }
        p_buf = np;
        p_cap = n;
    }

    void resize(size_t n) {
        for (size_t i = n; i < p_size; ++i) {
            p_buf[i].~T();
        }
        p_size = n;
    }

    void clear() {
        resize(0);
    }

    T &operator[](size_t i) {
        return p_buf[i];
    }

    T const &operator[](size_t i) const {
        return p_buf[i];
    }

    T &front() {
        return p_buf[0];
    }

    T const &front() const {
        return p_buf[0];
    }

    T &back() {
        return p_buf[p_size - 1];
    }

    T const &back() const {
        return p_buf[p_size - 1];
    }

    T *data() {
        return p_buf;
    }

    T const *data() const {
        return p_buf;
    }

    size_t size() const {
        return p_size;
    }

    size_t capacity() const {
        return p_cap;
    }

    bool empty() const {
        return p_size == 0;
    }

    void swap(vector &v) {
        util::swap(p_buf, v.p_buf);
        util::swap(p_size, v.p_size);
        util::swap(p_cap, v.p_cap);
    }

private:
    void drop() {
        resize(0);
        delete[] reinterpret_cast<unsigned char *>(p_buf);
    }

    T *p_buf = nullptr;
    size_t p_size = 0, p_cap = 0;
};

} /* namespace util */

#endif /* UTIL_HH */
