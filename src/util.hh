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
#include <cstdint>
#include <climits>
#include <cfloat>

/* allocation */

inline void *operator new(std::size_t, void *p) noexcept { return p; }
inline void *operator new[](std::size_t, void *p) noexcept { return p; }
inline void operator delete(void *, void *) noexcept {}
inline void operator delete[](void *, void *) noexcept {}

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

/* these need to be adjusted if a larger type support
 * is added, e.g. 128-bit integers/floats and so on
 */
using max_aligned_t = conditional_t<
    (alignof(long double) > alignof(long long)),
    long double,
    long long
>;

using biggest_t = conditional_t<
    (sizeof(long double) > sizeof(long long)),
    long double,
    long long
>;

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

/* safe punning */

template<typename T, typename U>
inline T pun(U val) {
    T ret;
    std::memcpy(&ret, &val, sizeof(ret));
    return ret;
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

/* simple writers for base 10 to avoid printf family */

std::size_t write_i(char *buf, std::size_t bufsize, long long v);
std::size_t write_u(char *buf, std::size_t bufsize, unsigned long long v);

/* a simple helper to align pointers to what we can represent */

inline void *ptr_align(void *p) {
    auto *up = static_cast<unsigned char *>(p);
    auto mod = pun<std::uintptr_t>(up) % alignof(max_aligned_t);
    if (mod) {
        up += alignof(max_aligned_t) - mod;
    }
    return up;
}

/* a refernce counted object; manages its own memory,
 * so it can avoid separately allocating the refcount
 */

template<typename T>
struct rc_obj {
    struct construct {};

    template<typename ...A>
    rc_obj(construct, A &&...cargs) {
        auto *np = new unsigned char[sizeof(T) + get_rc_size()];
        /* initial acquire */
        *pun<std::size_t *>(np) = 1;
        /* store */
        np += get_rc_size();
        p_ptr = pun<T *>(np);
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

    std::size_t count() const {
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
    static constexpr std::size_t get_rc_size() {
        return (alignof(T) > sizeof(std::size_t))
            ? alignof(T) : sizeof(std::size_t);
    }

    std::size_t *counter() const {
        return pun<std::size_t *>(pun<unsigned char *>(p_ptr) - get_rc_size());
    }

    void incr() {
        ++*counter();
    }

    void decr() {
        auto *ptr = counter();
        if (!--*ptr) {
            p_ptr->~T();
            delete[] pun<unsigned char *>(ptr);
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
    static constexpr std::size_t MIN_SIZE = 4;

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
        for (std::size_t i = 0; i < v.size(); ++i) {
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

    void reserve(std::size_t n) {
        if (n <= p_cap) {
            return;
        }
        if (n < MIN_SIZE) {
            n = MIN_SIZE;
        }
        T *np = pun<T *>(new unsigned char[n * sizeof(T)]);
        if (p_cap) {
            for (std::size_t i = 0; i < p_size; ++i) {
                new (&np[i]) T(util::move(p_buf[i]));
                p_buf[i].~T();
            }
            delete[] pun<unsigned char *>(p_buf);
        }
        p_buf = np;
        p_cap = n;
    }

    void shrink(std::size_t n) {
        for (std::size_t i = n; i < p_size; ++i) {
            p_buf[i].~T();
        }
        p_size = n;
    }

    void clear() {
        shrink(0);
    }

    T &operator[](std::size_t i) {
        return p_buf[i];
    }

    T const &operator[](std::size_t i) const {
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

    std::size_t size() const {
        return p_size;
    }

    std::size_t capacity() const {
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

    void setlen(std::size_t len) {
        p_size = len;
    }

    void setbuf(T const *data, std::size_t len) {
        std::memcpy(p_buf, data, len);
        p_size = len;
    }

private:
    void drop() {
        shrink(0);
        delete[] pun<unsigned char *>(p_buf);
    }

    T *p_buf = nullptr;
    std::size_t p_size = 0, p_cap = 0;
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

    strbuf(char const *str, std::size_t n) {
        set(str, n);
    }

    strbuf(char const *str): strbuf(str, std::strlen(str)) {}

    ~strbuf() {}

    strbuf &operator=(char const *str) {
        set(str, std::strlen(str));
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

    void append(char const *str, std::size_t n) {
        auto sz = p_buf.size();
        p_buf.reserve(sz + n);
        std::memcpy(&p_buf[sz - 1], str, n);
        p_buf[n + sz - 1] = '\0';
        p_buf.setlen(sz + n);
    }

    void append(char const *str) {
        append(str, std::strlen(str));
    }

    void append(char c) {
        push_back(c);
    }

    void append(strbuf const &b) {
        append(b.data(), b.size());
    }

    void prepend(char const *str, std::size_t n) {
        auto sz = p_buf.size();
        p_buf.reserve(sz + n);
        std::memmove(&p_buf[n], &p_buf[0], sz);
        std::memcpy(&p_buf[0], str, n);
        p_buf.setlen(sz + n);
    }

    void prepend(char const *str) {
        prepend(str, std::strlen(str));
    }

    void prepend(char c) {
        auto sz = p_buf.size();
        p_buf.reserve(sz + 1);
        std::memmove(&p_buf[1], &p_buf[0], sz);
        p_buf[0] = c;
        p_buf.setlen(sz + 1);
    }

    void prepend(strbuf const &b) {
        prepend(b.data(), b.size());
    }

    void insert(char const *str, std::size_t n, std::size_t idx) {
        auto sz = p_buf.size();
        p_buf.reserve(sz + n);
        std::memmove(&p_buf[idx + n], &p_buf[idx], sz - idx);
        std::memcpy(&p_buf[idx], str, n);
        p_buf.setlen(sz + n);
    }

    void insert(char const *str, std::size_t idx) {
        insert(str, std::strlen(str), idx);
    }

    void insert(strbuf const &b, std::size_t idx) {
        insert(b.data(), b.size(), idx);
    }

    void remove(size_t idx, size_t n = 1) {
        std::memmove(&p_buf[idx], &p_buf[idx + n], p_buf.size() + 1 - idx - n);
    }

    void set(char const *str, std::size_t n) {
        p_buf.reserve(n + 1);
        std::memcpy(&p_buf[0], str, n);
        p_buf[n] = '\0';
        p_buf.setlen(n + 1);
    }

    void set(char const *str) {
        set(str, std::strlen(str));
    }

    void set(strbuf const &b) {
        set(b.data(), b.size());
    }

    void reserve(std::size_t n) {
        p_buf.reserve(n + 1);
    }

    void clear() {
        p_buf.clear();
        p_buf[0] = '\0';
        p_buf.setlen(1);
    }

    char  operator[](std::size_t i) const { return p_buf[i]; }
    char &operator[](std::size_t i) { return p_buf[i]; }

    char  front() const { return p_buf.front(); }
    char &front() { return p_buf.front(); }

    char  back() const { return p_buf[size() - 1]; }
    char &back() { return p_buf[size() - 1]; }

    char const *data() const { return p_buf.data(); }
    char *data() { return p_buf.data(); }

    std::size_t size() const { return p_buf.size() - 1; }
    std::size_t capacity() const { return p_buf.capacity() - 1; }

    bool empty() const { return (size() == 0); }

    void swap(strbuf &b) {
        p_buf.swap(b.p_buf);
    }

    void setlen(std::size_t n) {
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
    static constexpr std::size_t CHUNK_SIZE = 64;
    static constexpr std::size_t DEFAULT_SIZE = 1024;

    struct entry {
        K key;
        V data;
    };

    struct bucket {
        entry value;
        bucket *next;
    };

public:
    map(std::size_t sz = DEFAULT_SIZE): p_size{sz} {
        p_buckets = new bucket *[sz];
        std::memset(p_buckets, 0, sz * sizeof(bucket *));
    }

    ~map() {
        delete[] p_buckets;
        drop_chunks();
    }

    bool empty() const {
        return p_nelems == 0;
    }

    V &operator[](K const &key) {
        std::size_t h;
        bucket *b = find_bucket(key, h);
        if (!b) {
            b = add(h);
            b->value.key = key;
        }
        return b->value.data;
    }

    V *find(K const &key) const {
        std::size_t h;
        bucket *b = find_bucket(key, h);
        if (!b) {
            return nullptr;
        }
        return &b->value.data;
    }

    V &insert(K const &key, V const &value) {
        std::size_t h;
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
        std::memset(p_buckets, 0, p_size * sizeof(bucket *));
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
        for (std::size_t i = 0; i < p_size; ++i) {
            for (bucket *b = p_buckets[i]; b; b = b->next) {
                func(b->value.key, b->value.data);
            }
        }
    }

private:
    bucket *add(std::size_t hash) {
        if (!p_unused) {
            chunk *nb = new chunk;
            nb->next = p_chunks;
            p_chunks = nb;
            for (std::size_t i = 0; i < CHUNK_SIZE - 1; ++i) {
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

    bucket *find_bucket(K const &key, std::size_t &h) const {
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

    std::size_t p_size, p_nelems = 0;

    bucket **p_buckets;
    bucket *p_unused = nullptr;
    chunk *p_chunks = nullptr;
};

#if SIZE_MAX > 0xFFFF
/* fnv1a for 32/64bit values */
template<std::size_t offset_basis, std::size_t prime>
struct fnv1a {
    std::size_t operator()(char const *data) const {
        std::size_t slen = std::strlen(data);
        std::size_t hash = offset_basis;
        for (std::size_t i = 0; i < slen; ++i) {
            hash ^= std::size_t(data[i]);
            hash *= prime;
        }
        return hash;
    }
};
#else
/* pearson hash for smaller values */
static unsigned char const ph_lt[256] = {
    167,  49, 207, 184,  90, 134,  74, 211, 215,  76, 109, 126, 222,  97, 231,
      1, 132, 204, 149, 249, 166,  33, 237, 100, 141, 186, 191, 112, 151, 203,
     69,  87,  65,  80, 157,  95,  58,  59,  82, 115, 171, 192,  24, 244, 225,
    223, 102, 189, 164, 119, 216, 174,  68, 133,   7,  10, 159,  31, 255, 150,
     41, 169, 161,  43, 245, 235,  16,  94,  81, 162, 103,  53, 110, 135, 228,
     86, 114, 144, 156, 241,   2, 253, 195, 128,  22, 105, 199, 250,  64,  13,
    178,  63,  99,  39, 190, 130, 163,  30, 122,  18, 168,  83, 220,  71, 129,
     84,   3, 208, 155,   9, 242, 170,  51, 143,  56, 158, 176, 172, 148,  55,
    227, 254, 247, 224,  50,  93,  54, 210, 206, 234, 218, 229,  61,  26, 107,
     32, 196, 217, 248, 138, 154, 212,  96,  40, 209,  38, 101,  73,  88, 125,
    175, 187,  34,  62, 118,  66, 113,  46, 238,  42, 202,   0, 179,  67,  47,
     20, 152, 165,  17,  89,  48, 123, 219,  70,  91, 120, 177, 188, 145, 104,
     92,  98,  44, 108,   4,  37, 139,  11, 214,  52, 221,  29,  75,  19,  35,
    124, 185,  28, 201, 230, 198, 131, 116, 153,  77,  72,  45, 226, 146,  12,
    137,  21, 147,  25,  27, 180, 240, 200, 243, 194,  15, 183, 181, 233, 213,
    232, 136,  14, 252, 121,  85, 111, 106, 127, 197, 251, 205,   8,  60, 246,
    140, 160, 239,  36,   6,   5, 142,  79,  57, 173, 182, 193, 117, 236,  23,
     78
};
struct pearson {
    std::size_t operator()(char const *data) const {
        std::size_t slen = std::strlen(data);
        std::size_t hash = 0;
        auto *udata = pun<unsigned char const *>(data);
        for (std::size_t j = 0; j < sizeof(std::size_t); ++j) {
            auto h = ph_lt[(udata[0] + j) % 256];
            for (std::size_t i = 1; i < slen; ++i) {
                h = ph_lt[h ^ udata[i]];
            }
            hash = ((hash << 8) | h);
        }
        return hash;
    }
};
#endif

#if SIZE_MAX > 0xFFFFFFFF
struct str_hash: fnv1a<14695981039346656037ULL, 1099511628211ULL> {};
#elif SIZE_MAX > 0xFFFF
struct str_hash: fnv1a<2166136261U, 16777619U> {};
#else
struct str_hash: pearson {};
#endif

struct str_equal {
    bool operator()(char const *k1, char const *k2) const {
        return !std::strcmp(k1, k2);
    }
};

template<typename V>
using str_map = map<char const *, V, str_hash, str_equal>;

} /* namespace util */

#endif /* UTIL_HH */
