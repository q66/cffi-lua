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

#include "platform.hh"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cfloat>

/* allocation */

#ifndef _MSC_VER
inline void *operator new(size_t, void *p) noexcept { return p; }
inline void *operator new[](size_t, void *p) noexcept { return p; }
inline void operator delete(void *, void *) noexcept {}
inline void operator delete[](void *, void *) noexcept {}
#endif

namespace util {

/* type traits */

namespace detail {
    template<typename T> struct remove_ref { using type = T; };
    template<typename T> struct remove_ref<T &> { using type = T; };
    template<typename T> struct remove_ref<T &&> { using type = T; };
}

template<typename T> using remove_ref_t = typename detail::remove_ref<T>::type;

namespace detail {
    template<typename T> struct remove_const { using type = T; };
    template<typename T> struct remove_const<T const> { using type = T; };

    template<typename T> struct remove_volatile { using type = T; };
    template<typename T> struct remove_volatile<T volatile> { using type = T; };
}

template<typename T> using remove_const_t =
    typename detail::remove_const<T>::type;
template<typename T> using remove_volatile_t =
    typename detail::remove_volatile<T>::type;
template<typename T> using remove_cv_t = typename detail::remove_volatile<
    typename detail::remove_const<T>::type
>::type;

namespace detail {
    template<bool B, typename T, typename F>
    struct conditional { using type = T; };

    template<typename T, typename F>
    struct conditional<false, T, F> { using type = F; };
}

template<bool B, typename T, typename F>
using conditional_t = typename detail::conditional<B, T, F>::type;

namespace detail {
    template<typename> struct integral {
        static constexpr bool value = false;
    };
    template<> struct integral<bool> {
        static constexpr bool value = true;
    };
    template<> struct integral<char> {
        static constexpr bool value = true;
    };
    template<> struct integral<char16_t> {
        static constexpr bool value = true;
    };
    template<> struct integral<char32_t> {
        static constexpr bool value = true;
    };
    template<> struct integral<wchar_t> {
        static constexpr bool value = true;
    };
    template<> struct integral<short> {
        static constexpr bool value = true;
    };
    template<> struct integral<int> {
        static constexpr bool value = true;
    };
    template<> struct integral<long> {
        static constexpr bool value = true;
    };
    template<> struct integral<long long> {
        static constexpr bool value = true;
    };
    template<> struct integral<signed char> {
        static constexpr bool value = true;
    };
    template<> struct integral<unsigned char> {
        static constexpr bool value = true;
    };
    template<> struct integral<unsigned short> {
        static constexpr bool value = true;
    };
    template<> struct integral<unsigned int> {
        static constexpr bool value = true;
    };
    template<> struct integral<unsigned long> {
        static constexpr bool value = true;
    };
    template<> struct integral<unsigned long long> {
        static constexpr bool value = true;
    };
}

template<typename T>
struct is_int {
    static constexpr bool value = detail::integral<remove_cv_t<T>>::value;
};

namespace detail {
    template<typename> struct fpoint {
        static constexpr bool value = false;
    };
    template<> struct fpoint<float> {
        static constexpr bool value = true;
    };
    template<> struct fpoint<double> {
        static constexpr bool value = true;
    };
    template<> struct fpoint<long double> {
        static constexpr bool value = true;
    };
}

template<typename T>
struct is_float {
    static constexpr bool value = detail::fpoint<remove_cv_t<T>>::value;
};

template<typename T>
struct is_arith {
    static constexpr bool value = is_int<T>::value || is_float<T>::value;
};

template<typename T, bool = is_arith<T>::value>
struct is_signed {
    static constexpr bool value = T(-1) < T(0);
};

template<typename T>
struct is_signed<T, false> {
    static constexpr bool value = false;
};

/* move semantics */

template<typename T>
constexpr inline remove_ref_t<T> &&move(T &&v) noexcept {
    return static_cast<remove_ref_t<T> &&>(v);
}

template<typename T>
constexpr inline T &&forward(remove_ref_t<T> &v) noexcept {
    return static_cast<T &&>(v);
}

template<typename T>
constexpr inline T &&forward(remove_ref_t<T> &&v) noexcept {
    return static_cast<T &&>(v);
}

/* assorted utils */

template<typename T, typename U = T>
constexpr inline T exchange(T &v, U &&nv) {
    T ov = move(v);
    v = forward<U>(nv);
    return ov;
}

template<typename T>
inline void swap(T &a, T &b) {
    auto o = move(a);
    a = move(b);
    b = move(o);
}

template<typename T>
inline T min(T a, T b) {
    return (a < b) ? a : b;
}

template<typename T>
inline T max(T a, T b) {
    return (a > b) ? a : b;
}

/* basic limits interface */

namespace detail {
    template<typename T>
    struct int_limits;

    template<>
    struct int_limits<char> {
        static constexpr char min = CHAR_MIN;
        static constexpr char max = CHAR_MAX;
    };

    template<>
    struct int_limits<signed char> {
        static constexpr signed char min = SCHAR_MIN;
        static constexpr signed char max = SCHAR_MAX;
    };

    template<>
    struct int_limits<unsigned char> {
        static constexpr unsigned char min = 0;
        static constexpr unsigned char max = UCHAR_MAX;
    };

    template<>
    struct int_limits<short> {
        static constexpr short min = SHRT_MIN;
        static constexpr short max = SHRT_MAX;
    };

    template<>
    struct int_limits<unsigned short> {
        static constexpr unsigned short min = 0;
        static constexpr unsigned short max = USHRT_MAX;
    };

    template<>
    struct int_limits<int> {
        static constexpr int min = INT_MIN;
        static constexpr int max = INT_MAX;
    };

    template<>
    struct int_limits<unsigned int> {
        static constexpr unsigned int min = 0;
        static constexpr unsigned int max = UINT_MAX;
    };

    template<>
    struct int_limits<long> {
        static constexpr long min = LONG_MIN;
        static constexpr long max = LONG_MAX;
    };

    template<>
    struct int_limits<unsigned long> {
        static constexpr unsigned long min = 0;
        static constexpr unsigned long max = ULONG_MAX;
    };

    template<>
    struct int_limits<long long> {
        static constexpr long long min = LLONG_MIN;
        static constexpr long long max = LLONG_MAX;
    };

    template<>
    struct int_limits<unsigned long long> {
        static constexpr unsigned long long min = 0;
        static constexpr unsigned long long max = ULLONG_MAX;
    };

    template<typename T, bool I, bool F>
    struct limits_base {};

    template<typename T>
    struct limits_base<T, true, false> {
        static constexpr int radix = 2;
        static constexpr int digits = CHAR_BIT * sizeof(T) - is_signed<T>::value;
        static constexpr T min = int_limits<T>::min;
        static constexpr T max = int_limits<T>::max;
    };

    template<>
    struct limits_base<float, false, true> {
        static constexpr int radix = FLT_RADIX;
        static constexpr int digits = FLT_MANT_DIG;
        static constexpr float min = FLT_MIN;
        static constexpr float max = FLT_MAX;
    };

    template<>
    struct limits_base<double, false, true> {
        static constexpr int radix = FLT_RADIX;
        static constexpr int digits = DBL_MANT_DIG;
        static constexpr double min = DBL_MIN;
        static constexpr double max = DBL_MAX;
    };

    template<>
    struct limits_base<long double, false, true> {
        static constexpr int radix = FLT_RADIX;
        static constexpr int digits = LDBL_MANT_DIG;
        static constexpr long double min = LDBL_MIN;
        static constexpr long double max = LDBL_MAX;
    };

    template<typename T>
    struct limits: limits_base<T, is_int<T>::value, is_float<T>::value> {};
}

template<typename T>
inline constexpr int limit_radix() {
    return detail::limits<T>::radix;
}

template<typename T>
inline constexpr int limit_digits() {
    return detail::limits<T>::digits;
}

template<typename T>
inline constexpr T limit_min() {
    return detail::limits<T>::min;
}

template<typename T>
inline constexpr T limit_max() {
    return detail::limits<T>::max;
}

/* a refernce counted object; manages its own memory,
 * so it can avoid separately allocating the refcount
 */

template<typename T>
struct rc_obj {
    struct construct {};

    template<typename ...A>
    rc_obj(construct, A &&...cargs) {
        auto *np = new unsigned char[sizeof(T) + RC_SIZE];
        /* initial acquire */
        *reinterpret_cast<size_t *>(np) = 1;
        /* store */
        p_ptr = reinterpret_cast<T *>(np + RC_SIZE);
        /* construct */
        new (p_ptr) T(util::forward<A>(cargs)...);
    }

    rc_obj(rc_obj const &op): p_ptr{op.p_ptr} {
        incr();
    }

    ~rc_obj() {
        decr();
    }

    rc_obj &operator=(rc_obj op) {
        swap(op);
        return *this;
    }

    T &operator*() const {
        return *p_ptr;
    }

    T *operator->() const {
        return p_ptr;
    }

    T *get() const {
        return p_ptr;
    }

    explicit operator bool() const {
        return (count() > 0);
    }

    size_t count() const {
        return *counter();
    }

    bool unique() const {
        return (count() == 1);
    }

    void release() {
        decr();
    }

    void swap(rc_obj &op) {
        util::swap(p_ptr, op.p_ptr);
    }

private:
    static constexpr size_t RC_SIZE =
        (alignof(T) > sizeof(size_t)) ? alignof(T) : sizeof(size_t);

    size_t *counter() const {
        auto *op = reinterpret_cast<unsigned char *>(p_ptr) - RC_SIZE;
        return reinterpret_cast<size_t *>(op);
    }

    void incr() {
        ++*counter();
    }

    void decr() {
        auto *ptr = counter();
        if (!--*ptr) {
            p_ptr->~T();
            delete[] reinterpret_cast<unsigned char *>(ptr);
        }
    }

    T *p_ptr;
};

template<typename T, typename ...A>
rc_obj<T> make_rc(A &&...args) {
    return rc_obj<T>{typename rc_obj<T>::construct{}, util::forward<A>(args)...};
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
        shrink(0);
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

    void shrink(size_t n) {
        for (size_t i = n; i < p_size; ++i) {
            p_buf[i].~T();
        }
        p_size = n;
    }

    void clear() {
        shrink(0);
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

    void setlen(size_t len) {
        p_size = len;
    }

    void setbuf(T const *data, size_t len) {
        memcpy(p_buf, data, len);
        p_size = len;
    }

private:
    void drop() {
        shrink(0);
        delete[] reinterpret_cast<unsigned char *>(p_buf);
    }

    T *p_buf = nullptr;
    size_t p_size = 0, p_cap = 0;
};

/* string buffer */

struct strbuf {
    strbuf() {
        /* always terminated */
        p_buf.push_back('\0');
    }

    strbuf(strbuf const &b) {
        set(b);
    }

    strbuf(strbuf &&b): p_buf(util::move(b.p_buf)) {}

    strbuf(char const *str, size_t n) {
        set(str, n);
    }

    strbuf(char const *str): strbuf(str, strlen(str)) {}

    ~strbuf() {}

    strbuf &operator=(char const *str) {
        set(str, strlen(str));
        return *this;
    }

    strbuf &operator=(strbuf const &b) {
        set(b);
        return *this;
    }

    strbuf &operator=(strbuf &&b) {
        p_buf = util::move(b.p_buf);
        return *this;
    }

    void push_back(char c) {
        p_buf.back() = c;
        p_buf.push_back('\0');
    }

    void pop_back() {
        p_buf.pop_back();
        p_buf.back() = '\0';
    }

    void append(char const *str, size_t n) {
        auto sz = p_buf.size();
        p_buf.reserve(sz + n);
        memcpy(&p_buf[sz - 1], str, n);
        p_buf[n + sz - 1] = '\0';
        p_buf.setlen(sz + n);
    }

    void append(char const *str) {
        append(str, strlen(str));
    }

    void append(char c) {
        push_back(c);
    }

    void append(strbuf const &b) {
        append(b.data(), b.size());
    }

    void prepend(char const *str, size_t n) {
        auto sz = p_buf.size();
        p_buf.reserve(sz + n);
        memmove(&p_buf[n], &p_buf[0], sz);
        memcpy(&p_buf[0], str, n);
        p_buf.setlen(sz + n);
    }

    void prepend(char const *str) {
        prepend(str, strlen(str));
    }

    void prepend(char c) {
        auto sz = p_buf.size();
        p_buf.reserve(sz + 1);
        memmove(&p_buf[1], &p_buf[0], sz);
        p_buf[0] = c;
        p_buf.setlen(sz + 1);
    }

    void prepend(strbuf const &b) {
        prepend(b.data(), b.size());
    }

    void insert(char const *str, size_t n, size_t idx) {
        auto sz = p_buf.size();
        p_buf.reserve(sz + n);
        memmove(&p_buf[idx + n], &p_buf[idx], sz - idx);
        memcpy(&p_buf[idx], str, n);
        p_buf.setlen(sz + n);
    }

    void insert(char const *str, size_t idx) {
        insert(str, strlen(str), idx);
    }

    void insert(strbuf const &b, size_t idx) {
        insert(b.data(), b.size(), idx);
    }

    void set(char const *str, size_t n) {
        p_buf.reserve(n + 1);
        memcpy(&p_buf[0], str, n);
        p_buf[n] = '\0';
        p_buf.setlen(n + 1);
    }

    void set(char const *str) {
        set(str, strlen(str));
    }

    void set(strbuf const &b) {
        set(b.data(), b.size());
    }

    void reserve(size_t n) {
        p_buf.reserve(n + 1);
    }

    void clear() {
        p_buf.clear();
        p_buf[0] = '\0';
        p_buf.setlen(1);
    }

    char  operator[](size_t i) const { return p_buf[i]; }
    char &operator[](size_t i) { return p_buf[i]; }

    char  front() const { return p_buf.front(); }
    char &front() { return p_buf.front(); }

    char  back() const { return p_buf[size() - 1]; }
    char &back() { return p_buf[size() - 1]; }

    char const *data() const { return p_buf.data(); }
    char *data() { return p_buf.data(); }

    size_t size() const { return p_buf.size() - 1; }
    size_t capacity() const { return p_buf.capacity() - 1; }

    bool empty() const { return (size() == 0); }

    void swap(strbuf &b) {
        p_buf.swap(b.p_buf);
    }

    void setlen(size_t n) {
        p_buf.setlen(n + 1);
    }

    util::vector<char> const &raw() const { return p_buf; }
    util::vector<char> &raw() { return p_buf; }

private:
    util::vector<char> p_buf{};
};

/* hashtable */

template<typename K, typename V, typename HF, typename CF>
struct map {
private:
    static constexpr size_t CHUNK_SIZE = 64;
    static constexpr size_t DEFAULT_SIZE = 1024;

    struct entry {
        K key;
        V data;
    };

    struct bucket {
        entry value;
        bucket *next;
    };

public:
    map(size_t sz = DEFAULT_SIZE): p_size{sz} {
        p_buckets = new bucket *[sz];
        memset(p_buckets, 0, sz * sizeof(bucket *));
    }

    ~map() {
        delete[] p_buckets;
        drop_chunks();
    }

    bool empty() const {
        return p_nelems == 0;
    }

    V &operator[](K const &key) {
        size_t h;
        bucket *b = find_bucket(key, h);
        if (!b) {
            b = add(h);
            b->value.key = key;
        }
        return b->value.data;
    }

    V *find(K const &key) const {
        size_t h;
        bucket *b = find_bucket(key, h);
        if (!b) {
            return nullptr;
        }
        return &b->value.data;
    }

    V &insert(K const &key, V const &value) {
        size_t h;
        bucket *b = find_bucket(key, h);
        if (!b) {
            b = add(h);
            b->value.key = key;
            b->value.data = value;
        }
        return b->value.data;
    }

    void clear() {
        if (!p_nelems) {
            return;
        }
        p_nelems = 0;
        p_unused = nullptr;
        memset(p_buckets, 0, p_size * sizeof(bucket *));
        drop_chunks();
    }

    void swap(map &m) {
        util::swap(p_size, m.p_size);
        util::swap(p_nelems, m.p_nelems);
        util::swap(p_buckets, m.p_buckets);
        util::swap(p_unused, m.p_unused);
        util::swap(p_chunks, m.p_chunks);
    }

    template<typename F>
    void for_each(F &&func) const {
        for (size_t i = 0; i < p_size; ++i) {
            for (bucket *b = p_buckets[i]; b; b = b->next) {
                func(b->value.key, b->value.data);
            }
        }
    }

private:
    bucket *add(size_t hash) {
        if (!p_unused) {
            chunk *nb = new chunk;
            nb->next = p_chunks;
            p_chunks = nb;
            for (size_t i = 0; i < CHUNK_SIZE - 1; ++i) {
                nb->buckets[i].next = &nb->buckets[i + 1];
            }
            nb->buckets[CHUNK_SIZE - 1].next = p_unused;
            p_unused = nb->buckets;
        }
        bucket *b = p_unused;
        p_unused = p_unused->next;
        b->next = p_buckets[hash];
        p_buckets[hash] = b;
        ++p_nelems;
        return b;
    }

    bucket *find_bucket(K const &key, size_t &h) const {
        h = HF{}(key) % p_size;
        for (bucket *b = p_buckets[h]; b; b = b->next) {
            if (CF{}(key, b->value.key)) {
                return b;
            }
        }
        return nullptr;
    }

    void drop_chunks() {
        for (chunk *nc; p_chunks; p_chunks = nc) {
            nc = p_chunks->next;
            delete p_chunks;
        }
    }

    struct chunk {
        bucket buckets[CHUNK_SIZE];
        chunk *next;
    };

    size_t p_size, p_nelems = 0;

    bucket **p_buckets;
    bucket *p_unused = nullptr;
    chunk *p_chunks = nullptr;
};

template<typename T, T offset_basis, T prime>
struct fnv1a {
    T operator()(char const *data) const {
        size_t slen = strlen(data);
        T hash = offset_basis;
        for (size_t i = 0; i < slen; ++i) {
            hash ^= T(data[i]);
            hash *= prime;
        }
        return hash;
    }
};

#if FFI_WORDSIZE == 64
struct str_hash: fnv1a<size_t,
    size_t(14695981039346656037ULL), size_t(1099511628211ULL)
> {};
#elif FFI_WORDSIZE == 32
struct str_hash: fnv1a<size_t, size_t(2166136261U), size_t(16777619U)> {};
#else
#  error Not implemented
#endif

struct str_equal {
    bool operator()(char const *k1, char const *k2) const {
        return !strcmp(k1, k2);
    }
};

template<typename V>
using str_map = map<char const *, V, str_hash, str_equal>;

} /* namespace util */

#endif /* UTIL_HH */
