#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>

#include "platform.hh"
#include "parser.hh"
#include "ast.hh"
#include "lib.hh"
#include "lua.hh"
#include "ffi.hh"
#include "util.hh"

/* sets up the metatable for library, i.e. the individual namespaces
 * of loaded shared libraries as well as the primary C namespace.
 */
struct lib_meta {
    static int gc(lua_State *L) {
        lib::close(lua::touserdata<lib::c_lib>(L, 1), L);
        return 0;
    }

    static int tostring(lua_State *L) {
        auto *cl = lua::touserdata<lib::c_lib>(L, 1);
        if (lib::is_c(cl)) {
            lua_pushfstring(L, "library: default");
        } else {
            lua_pushfstring(L, "library: %p", cl->h);
        }
        return 1;
    }

    static int index(lua_State *L) {
        auto dl = lua::touserdata<lib::c_lib>(L, 1);
        ffi::get_global(L, dl, luaL_checkstring(L, 2));
        return 1;
    }

    static int newindex(lua_State *L) {
        auto dl = lua::touserdata<lib::c_lib>(L, 1);
        ffi::set_global(L, dl, luaL_checkstring(L, 2), 3);
        return 0;
    }

    static void setup(lua_State *L) {
        if (!luaL_newmetatable(L, lua::CFFI_LIB_MT)) {
            luaL_error(L, "unexpected error: registry reinitialized");
        }

        lua_pushliteral(L, "ffi");
        lua_setfield(L, -2, "__metatable");

        lua_pushcfunction(L, gc);
        lua_setfield(L, -2, "__gc");

        lua_pushcfunction(L, index);
        lua_setfield(L, -2, "__index");

        lua_pushcfunction(L, newindex);
        lua_setfield(L, -2, "__newindex");

        lua_pushcfunction(L, tostring);
        lua_setfield(L, -2, "__tostring");

        lua_setmetatable(L, -2);
        lua_setfield(L, -2, "C");
    }
};

/* used by all kinds of cdata
 *
 * there are several kinds of cdata:
 * - callable cdata (functions)
 * - indexable cdata (pointers, arrays)
 * - value cdata (primitives)
 */
struct cdata_meta {
    static int gc(lua_State *L) {
        ffi::destroy_cdata(L, ffi::tocdata(L, 1));
        return 0;
    }

    static int metatype_getmt(lua_State *L, int idx, int &mflags) {
        auto &cd = ffi::tocdata(L, idx);
        auto *decl = &cd.decl;
        auto tp = decl->type();
        if (tp == ast::C_BUILTIN_RECORD) {
            return cd.decl.record().metatype(mflags);
        } else if (tp == ast::C_BUILTIN_PTR) {
            if (cd.decl.ptr_base().type() != ast::C_BUILTIN_RECORD) {
                return LUA_REFNIL;
            }
            return cd.decl.ptr_base().record().metatype(mflags);
        }
        return LUA_REFNIL;
    }

    template<ffi::metatype_flag flag>
    static inline bool metatype_check(lua_State *L, int idx) {
        int mflags = 0;
        int mtp = metatype_getmt(L, idx, mflags);
        if (!(mflags & flag)) {
            return false;
        }
        return ffi::metatype_getfield(L, mtp, ffi::metafield_name(flag));
    }

    static int tostring(lua_State *L) {
        if (metatype_check<ffi::METATYPE_FLAG_TOSTRING>(L, 1)) {
            lua_pushvalue(L, 1);
            lua_call(L, 1, 1);
            return 1;
        }
        auto &cd = ffi::tocdata(L, 1);
        if (ffi::isctype(cd)) {
#if LUA_VERSION_NUM > 502
            if (metatype_check<ffi::METATYPE_FLAG_NAME>(L, 1)) {
                /* __name is respected only when it's specifically
                 * a string, otherwise ignore like if it didn't exist
                 */
                if (lua_type(L, -1) == LUA_TSTRING) {
                    return 1;
                }
                lua_pop(L, 1);
            }
#endif
            lua_pushliteral(L, "ctype<");
            cd.decl.serialize(L);
            lua_pushliteral(L, ">");
            lua_concat(L, 3);
            return 1;
        }
#if LUA_VERSION_NUM > 502
        if (metatype_check<ffi::METATYPE_FLAG_NAME>(L, 1)) {
            if (lua_type(L, -1) == LUA_TSTRING) {
                lua_pushfstring(L, ": %p", cd.address_of());
                lua_concat(L, 2);
                return 1;
            }
            lua_pop(L, 1);
        }
#endif
        auto const *tp = &cd.decl;
        void *val = cd.as_deref_ptr();
        /* 64-bit integers */
        /* XXX: special printing for lua builds with non-double numbers? */
        if (tp->integer() && (tp->alloc_size() == 8)) {
            char buf[32];
            std::size_t written;
            if (tp->is_unsigned()) {
                written = util::write_u(
                    buf, sizeof(buf), *static_cast<unsigned long long *>(val)
                );
                std::memcpy(&buf[written], "ULL", 4);
                written += 4;
            } else {
                written = util::write_i(
                    buf, sizeof(buf), *static_cast<long long *>(val)
                );
                std::memcpy(&buf[written], "LL", 3);
                written += 3;
            }
            lua_pushlstring(L, buf, written);
            return 1;
        }
        lua_pushliteral(L, "cdata<");
        cd.decl.serialize(L);
        lua_pushfstring(L, ">: %p", cd.address_of());
        lua_concat(L, 3);
        return 1;
    }

    static int call(lua_State *L) {
        auto &fd = ffi::tocdata(L, 1);
        if (ffi::isctype(fd)) {
            if (metatype_check<ffi::METATYPE_FLAG_NEW>(L, 1)) {
                int nargs = lua_gettop(L) - 1;
                lua_insert(L, 1);
                lua_call(L, nargs, 1);
            } else {
                ffi::make_cdata(L, fd.decl, ffi::RULE_CONV, 2);
            }
            return 1;
        }
        if (!fd.decl.callable()) {
            int nargs = lua_gettop(L);
            if (metatype_check<ffi::METATYPE_FLAG_CALL>(L, 1)) {
                lua_insert(L, 1);
                lua_call(L, nargs, LUA_MULTRET);
                return lua_gettop(L);
            }
            fd.decl.serialize(L);
            luaL_error(L, "'%s' is not callable", lua_tostring(L, -1));
        }
        if (fd.decl.closure() && !fd.as<ffi::fdata>().cd) {
            luaL_error(L, "bad callback");
        }
        return ffi::call_cif(fd, L, lua_gettop(L) - 1);
    }

    template<bool New, typename F>
    static bool index_common(lua_State *L, F &&func) {
        auto &cd = ffi::tocdata(L, 1);
        if (ffi::isctype(cd)) {
            if (New) {
                luaL_error(L, "'ctype' is not indexable");
            } else {
                /* indexing ctypes is okay if they have __index */
                return false;
            }
        }
        void **valp = static_cast<void **>(cd.as_deref_ptr());
        auto const *decl = &cd.decl;
        if (
            (decl->type() == ast::C_BUILTIN_PTR) &&
            (lua_type(L, 2) == LUA_TSTRING)
        ) {
            /* pointers are indexable like arrays using numbers, but
             * not by names; however, there's a special case for record
             * types, where pointers to them can be indexed like the
             * underlying record, so assume that for the time being
             */
            decl = &decl->ptr_base();
            valp = static_cast<void **>(*valp);
        }
        std::size_t elsize = 0;
        unsigned char *ptr = nullptr;
        switch (decl->type()) {
            case ast::C_BUILTIN_PTR:
            case ast::C_BUILTIN_ARRAY:
                ptr = static_cast<unsigned char *>(*valp);
                elsize = decl->ptr_base().alloc_size();
                if (!elsize) {
                    decl->serialize(L);
                    luaL_error(
                        L, "attempt to index an incomplete type '%s'",
                        lua_tostring(L, -1)
                    );
                }
                break;
            case ast::C_BUILTIN_RECORD: {
                char const *fname = luaL_checkstring(L, 2);
                ast::c_type const *outf;
                auto foff = decl->record().field_offset(fname, outf);
                if (foff < 0) {
                    return false;
                }
                func(*outf, util::pun<unsigned char *>(valp) + foff);
                return true;
            }
            default: {
                decl->serialize(L);
                luaL_error(L, "'%s' is not indexable", lua_tostring(L, -1));
                break;
            }
        }
        auto sidx = ffi::check_arith<std::size_t>(L, 2);
        func(decl->ptr_base(), static_cast<void *>(&ptr[sidx * elsize]));
        return true;
    }

    static int cb_free(lua_State *L) {
        auto &cd = ffi::checkcdata(L, 1);
        luaL_argcheck(L, cd.decl.closure(), 1, "not a callback");
        if (!cd.as<ffi::fdata>().cd) {
            luaL_error(L, "bad callback");
        }
        ffi::destroy_closure(L, cd.as<ffi::fdata>().cd);
        return 0;
    }

    static int cb_set(lua_State *L) {
        auto &cd = ffi::checkcdata(L, 1);
        luaL_argcheck(L, cd.decl.closure(), 1, "not a callback");
        if (!cd.as<ffi::fdata>().cd) {
            luaL_error(L, "bad callback");
        }
        if (!lua_isfunction(L, 2)) {
            lua::type_error(L, 2, "function");
        }
        luaL_unref(L, LUA_REGISTRYINDEX, cd.as<ffi::fdata>().cd->fref);
        lua_pushvalue(L, 2);
        cd.as<ffi::fdata>().cd->fref = luaL_ref(L, LUA_REGISTRYINDEX);
        return 0;
    }

    static int index(lua_State *L) {
        auto &cd = ffi::tocdata(L, 1);
        if (cd.decl.closure()) {
            /* callbacks have some methods */
            char const *mname = lua_tostring(L, 2);
            /* if we had more methods, we'd do a table */
            if (!std::strcmp(mname, "free")) {
                lua_pushcfunction(L, cb_free);
                return 1;
            } else if (!std::strcmp(mname, "set")) {
                lua_pushcfunction(L, cb_set);
                return 1;
            } else if (!mname) {
                cd.decl.serialize(L);
                luaL_error(
                    L, "'%s' cannot be indexed with '%s'",
                    lua_tostring(L, -1),
                    lua_typename(L, lua_type(L, 2))
                );
            } else {
                cd.decl.serialize(L);
                luaL_error(
                    L, "'%s' has no member named '%s'",
                    lua_tostring(L, -1), mname
                );
            }
            return 0;
        }
        if (index_common<false>(L, [L](auto &decl, void *val) {
            if (!ffi::to_lua(L, decl, val, ffi::RULE_CONV, false)) {
                luaL_error(L, "invalid C type");
            }
        })) {
            return 1;
        };
        if (metatype_check<ffi::METATYPE_FLAG_INDEX>(L, 1)) {
            /* if __index is a function, call it */
            if (lua_isfunction(L, -1)) {
                /* __index takes 2 args, put it to the beginning and call */
                lua_insert(L, 1);
                lua_call(L, 2, 1);
                return 1;
            }
            /* otherwise, index it with key that's on top of the stack */
            lua_pushvalue(L, 2);
            lua_gettable(L, -2);
            if (!lua_isnil(L, -1)) {
                return 1;
            }
        }
        if (ffi::isctype(cd)) {
            luaL_error(L, "'ctype' is not indexable");
        }
        if (lua_type(L, 2) != LUA_TSTRING) {
            cd.decl.serialize(L);
            luaL_error(
                L, "'%s' is not indexable with '%s'",
                lua_tostring(L, -1), lua_typename(L, 2)
            );
        } else {
            cd.decl.serialize(L);
            luaL_error(
                L, "'%s' has no member named '%s'",
                lua_tostring(L, -1), lua_tostring(L, 2)
            );
        }
        return 1;
    }

    static int newindex(lua_State *L) {
        if (index_common<true>(L, [L](auto &decl, void *val) {
            ffi::from_lua(L, decl, val, 3);
        })) {
            return 0;
        };
        if (metatype_check<ffi::METATYPE_FLAG_NEWINDEX>(L, 1)) {
            lua_insert(L, 1);
            lua_call(L, 3, 0);
            return 0;
        }
        ffi::tocdata(L, 1).decl.serialize(L);
        luaL_error(
            L, "'%s' has no member named '%s'",
            lua_tostring(L, -1), lua_tostring(L, 2)
        );
        return 0;
    }

    template<ffi::metatype_flag mtype>
    static inline bool op_try_mt(
        lua_State *L, ffi::cdata const *cd1,
        ffi::cdata const *cd2, int rvals = 1
    ) {
        /* custom metatypes, either operand */
        if (
            (cd1 && metatype_check<mtype>(L, 1)) ||
            (cd2 && metatype_check<mtype>(L, 2))
        ) {
            lua_insert(L, 1);
            lua_call(L, lua_gettop(L) - 1, rvals);
            return true;
        }
        return false;
    }

    static int concat(lua_State *L) {
        auto *cd1 = ffi::testcdata(L, 1);
        auto *cd2 = ffi::testcdata(L, 2);
        if (op_try_mt<ffi::METATYPE_FLAG_CONCAT>(L, cd1, cd2)) {
            return 1;
        }
        luaL_error(
            L, "attempt to concatenate '%s' and '%s'",
            ffi::lua_serialize(L, 1), ffi::lua_serialize(L, 2)
        );
        return 0;
    }

    static int len(lua_State *L) {
        auto *cd = ffi::testcdata(L, 1);
        if (op_try_mt<ffi::METATYPE_FLAG_LEN>(L, cd, nullptr)) {
            return 1;
        }
        luaL_error(
            L, "attempt to get length of '%s'", ffi::lua_serialize(L, 1)
        );
        return 0;
    }

    /* this follows LuaJIT rules for cdata arithmetic: each operand is
     * converted to signed 64-bit integer unless one of them is an
     * unsigned 64-bit integer, in which case both become unsigned
     */
    template<typename T, ast::c_expr_type et>
    static void promote_to_64bit(ast::c_expr_type &t, void *v) {
        switch (t) {
            case ast::c_expr_type::INT:
                *static_cast<T *>(v) = T(*static_cast<int *>(v));
                break;
            case ast::c_expr_type::UINT:
                *static_cast<T *>(v) = T(*static_cast<unsigned int *>(v));
                break;
            case ast::c_expr_type::LONG:
                *static_cast<T *>(v) = T(*static_cast<long *>(v));
                break;
            case ast::c_expr_type::ULONG:
                *static_cast<T *>(v) = T(*static_cast<unsigned long *>(v));
                break;
            case ast::c_expr_type::LLONG:
                *static_cast<T *>(v) = T(*static_cast<long long *>(v));
                break;
            case ast::c_expr_type::FLOAT:
                *static_cast<T *>(v) = T(*static_cast<float *>(v));
                break;
            case ast::c_expr_type::DOUBLE:
                *static_cast<T *>(v) = T(*static_cast<double *>(v));
                break;
            case ast::c_expr_type::LDOUBLE:
                *static_cast<T *>(v) = T(*static_cast<long double *>(v));
                break;
            default: break;
        }
        t = et;
    }

    static void promote_long(ast::c_expr_type &t) {
        if (sizeof(long) == sizeof(long long)) {
            switch (t) {
                case ast::c_expr_type::LONG:
                    t = ast::c_expr_type::LLONG; break;
                case ast::c_expr_type::ULONG:
                    t = ast::c_expr_type::ULLONG; break;
                default:
                    break;
            }
        }
    }

    static void promote_sides(
        ast::c_expr_type &lt, ast::c_value &lv,
        ast::c_expr_type &rt, ast::c_value &rv
    ) {
        promote_long(lt);
        promote_long(rt);
        if (
            (lt == ast::c_expr_type::ULLONG) ||
            (rt == ast::c_expr_type::ULLONG)
        ) {
            promote_to_64bit<
                unsigned long long, ast::c_expr_type::ULLONG
            >(lt, &lv);
            promote_to_64bit<
                unsigned long long, ast::c_expr_type::ULLONG
            >(rt, &rv);
        } else {
            promote_to_64bit<long long, ast::c_expr_type::LLONG>(lt, &lv);
            promote_to_64bit<long long, ast::c_expr_type::LLONG>(rt, &rv);
        }
    }

    static ast::c_value arith_64bit_base(
        lua_State *L, ast::c_expr_binop op, ast::c_expr_type &retp
    ) {
        ast::c_expr bexp{ast::C_TYPE_WEAK}, lhs, rhs;
        ast::c_expr_type lt = ffi::check_arith_expr(L, 1, lhs.val);
        ast::c_expr_type rt = ffi::check_arith_expr(L, 2, rhs.val);
        promote_sides(lt, lhs.val, rt, rhs.val);
        lhs.type(lt);
        rhs.type(rt);
        bexp.type(ast::c_expr_type::BINARY);
        bexp.bin.op = op;
        bexp.bin.lhs = &lhs;
        bexp.bin.rhs = &rhs;
        ast::c_value ret;
        if (!bexp.eval(L, ret, retp, true)) {
            lua_error(L);
        }
        return ret;
    }

    static void arith_64bit_bin(lua_State *L, ast::c_expr_binop op) {
        /* regular arithmetic */
        ast::c_expr_type retp;
        auto rv = arith_64bit_base(L, op, retp);
        ffi::make_cdata_arith(L, retp, rv);
    }

    static void arith_64bit_cmp(lua_State *L, ast::c_expr_binop op) {
        /* comparison */
        ast::c_expr_type retp;
        auto rv = arith_64bit_base(L, op, retp);
        assert(retp == ast::c_expr_type::BOOL);
        lua_pushboolean(L, rv.b);
    }

    static int add(lua_State *L) {
        auto *cd1 = ffi::testcdata(L, 1);
        auto *cd2 = ffi::testcdata(L, 2);
        /* pointer arithmetic */
        if (cd1 && cd1->decl.ptr_like()) {
            auto asize = cd1->decl.ptr_base().alloc_size();
            if (!asize) {
                if (op_try_mt<ffi::METATYPE_FLAG_ADD>(L, cd1, cd2)) {
                    return 1;
                }
                luaL_error(L, "unknown C type size");
            }
            std::ptrdiff_t d;
            if (!ffi::test_arith<std::ptrdiff_t>(L, 2, d)) {
                if (op_try_mt<ffi::METATYPE_FLAG_ADD>(L, cd1, cd2)) {
                    return 1;
                }
                ffi::check_arith<std::ptrdiff_t>(L, 2);
            }
            /* do arithmetic on uintptr, doing it with a pointer would be UB
             * in case of a null pointer (and we want predicable behavior)
             */
            auto p = cd1->as_deref<std::uintptr_t>();
            auto tp = cd1->decl.as_type(ast::C_BUILTIN_PTR);
            auto &ret = ffi::newcdata(L,  tp.unref(), sizeof(void *));
            ret.as<std::uintptr_t>() = p + d * asize;
            return 1;
        } else if (cd2 && cd2->decl.ptr_like()) {
            auto asize = cd2->decl.ptr_base().alloc_size();
            if (!asize) {
                if (op_try_mt<ffi::METATYPE_FLAG_ADD>(L, cd1, cd2)) {
                    return 1;
                }
                luaL_error(L, "unknown C type size");
            }
            std::ptrdiff_t d;
            if (!ffi::test_arith<std::ptrdiff_t>(L, 1, d)) {
                if (op_try_mt<ffi::METATYPE_FLAG_ADD>(L, cd1, cd2)) {
                    return 1;
                }
                ffi::check_arith<std::ptrdiff_t>(L, 1);
            }
            auto p = cd2->as_deref<std::uintptr_t>();
            auto tp = cd2->decl.as_type(ast::C_BUILTIN_PTR);
            auto &ret = ffi::newcdata(L, tp.unref(), sizeof(void *));
            ret.as<std::uintptr_t>() = d * asize + p;
            return 1;
        }
        if (op_try_mt<ffi::METATYPE_FLAG_ADD>(L, cd1, cd2)) {
            return 1;
        }
        arith_64bit_bin(L, ast::c_expr_binop::ADD);
        return 1;
    }

    static int sub(lua_State *L) {
        auto *cd1 = ffi::testcdata(L, 1);
        auto *cd2 = ffi::testcdata(L, 2);
        /* pointer difference */
        if (cd1 && cd1->decl.ptr_like()) {
            auto asize = cd1->decl.ptr_base().alloc_size();
            if (!asize) {
                if (op_try_mt<ffi::METATYPE_FLAG_SUB>(L, cd1, cd2)) {
                    return 1;
                }
                luaL_error(L, "unknown C type size");
            }
            if (cd2 && cd2->decl.ptr_like()) {
                if (!cd1->decl.ptr_base().is_same(cd2->decl.ptr_base(), true)) {
                    if (op_try_mt<ffi::METATYPE_FLAG_SUB>(L, cd1, cd2)) {
                        return 1;
                    }
                    cd2->decl.serialize(L);
                    cd1->decl.serialize(L);
                    luaL_error(
                        L, "cannot convert '%s' to '%s'",
                        lua_tostring(L, -2), lua_tostring(L, -1)
                    );
                }
                /* use intptrs to prevent UB with potential nulls; signed so
                 * we can get a potential negative result in a safe way
                 */
                auto ret = cd1->as_deref<std::intptr_t>()
                         - cd2->as_deref<std::intptr_t>();
                lua_pushinteger(L, lua_Integer(ret / asize));
                return 1;
            }
            std::ptrdiff_t d;
            if (!ffi::test_arith<std::ptrdiff_t>(L, 2, d)) {
                if (op_try_mt<ffi::METATYPE_FLAG_ADD>(L, cd1, cd2)) {
                    return 1;
                }
                ffi::check_arith<std::ptrdiff_t>(L, 2);
            }
            auto p = cd1->as_deref<std::uintptr_t>();
            auto &ret = ffi::newcdata(L, cd1->decl, sizeof(void *));
            ret.as<std::uintptr_t>() = p + d;
            return 1;
        }
        if (op_try_mt<ffi::METATYPE_FLAG_SUB>(L, cd1, cd2)) {
            return 1;
        }
        arith_64bit_bin(L, ast::c_expr_binop::SUB);
        return 1;
    }

    template<ffi::metatype_flag mflag, ast::c_expr_binop bop>
    static int arith_bin(lua_State *L) {
        auto *cd1 = ffi::testcdata(L, 1);
        auto *cd2 = ffi::testcdata(L, 2);
        if (!op_try_mt<mflag>(L, cd1, cd2)) {
            arith_64bit_bin(L, bop);
        }
        return 1;
    }

    template<typename T>
    static T powimp(T base, T exp) {
        if (util::is_signed<T>::value && (exp < 0)) {
            return 0;
        }
        T ret = 1;
        for (;;) {
            if (exp & 1) {
                ret *= base;
            }
            exp = exp >> 1;
            if (!exp) {
                break;
            }
            base *= base;
        }
        return ret;
    }

    static int pow(lua_State *L) {
        auto *cd1 = ffi::testcdata(L, 1);
        auto *cd2 = ffi::testcdata(L, 2);
        if (op_try_mt<ffi::METATYPE_FLAG_POW>(L, cd1, cd2)) {
            return 1;
        }
        ast::c_value lhs, rhs;
        ast::c_expr_type lt = ffi::check_arith_expr(L, 1, lhs);
        ast::c_expr_type rt = ffi::check_arith_expr(L, 2, rhs);
        promote_sides(lt, lhs, rt, rhs);
        assert(lt == rt);
        switch (lt) {
            case ast::c_expr_type::LLONG:
                lhs.ll = powimp<long long>(lhs.ll, rhs.ll);
                break;
            case ast::c_expr_type::ULLONG:
                lhs.ull = powimp<unsigned long long>(lhs.ull, rhs.ull);
                break;
            default:
                assert(false);
                break;
        }
        ffi::make_cdata_arith(L, lt, lhs);
        return 1;
    }

    template<ffi::metatype_flag mflag, ast::c_expr_unop uop>
    static int arith_un(lua_State *L) {
        auto *cd = ffi::testcdata(L, 1);
        if (op_try_mt<mflag>(L, cd, nullptr)) {
            return 1;
        }
        ast::c_expr uexp{ast::C_TYPE_WEAK}, exp;
        ast::c_expr_type et = ffi::check_arith_expr(L, 1, exp.val);
        promote_long(et);
        if (et != ast::c_expr_type::ULLONG) {
            promote_to_64bit<long long, ast::c_expr_type::LLONG>(et, &exp.val);
        }
        exp.type(et);
        uexp.type(ast::c_expr_type::UNARY);
        uexp.un.op = uop;
        uexp.un.expr = &exp;
        ast::c_value rv;
        if (!uexp.eval(L, rv, et, true)) {
            lua_error(L);
        }
        ffi::make_cdata_arith(L, et, rv);
        return 1;
    }

    static void *cmp_addr(ffi::cdata *cd) {
        if (cd->decl.ptr_like()) {
            return cd->as_deref<void *>();
        }
        return cd->as_deref_ptr();
    }

    static int eq(lua_State *L) {
        auto *cd1 = ffi::testcval(L, 1);
        auto *cd2 = ffi::testcval(L, 2);
        if (!cd1 || !cd2) {
            /* equality against non-cdata object is always false */
            lua_pushboolean(L, false);
            return 1;
        }
        if (
            (cd1->gc_ref == lua::CFFI_CTYPE_TAG) ||
            (cd2->gc_ref == lua::CFFI_CTYPE_TAG)
        ) {
            if (cd1->gc_ref != cd2->gc_ref) {
                /* ctype against cdata */
                lua_pushboolean(L, false);
            } else {
                lua_pushboolean(L, cd1->decl.is_same(cd2->decl));
            }
            return 1;
        }
        if (!cd1->decl.arith() || !cd2->decl.arith()) {
            if (cd1->decl.ptr_like() && cd2->decl.ptr_like()) {
                lua_pushboolean(
                    L, cd1->as_deref<void *>() == cd2->as_deref<void *>()
                );
                return 1;
            }
            if (op_try_mt<ffi::METATYPE_FLAG_EQ>(L, cd1, cd2)) {
                return 1;
            }
            /* if any operand is non-arithmetic, compare by address */
            lua_pushboolean(L, cmp_addr(cd1) == cmp_addr(cd2));
            return 1;
        }
        if (op_try_mt<ffi::METATYPE_FLAG_EQ>(L, cd1, cd2)) {
            return 1;
        }
        /* otherwise compare values */
        arith_64bit_cmp(L, ast::c_expr_binop::EQ);
        return 1;
    }

    template<ffi::metatype_flag mf1, ffi::metatype_flag mf2>
    static bool cmp_base(
        lua_State *L, ast::c_expr_binop op,
        ffi::cdata const *cd1, ffi::cdata const *cd2
    ) {
        if (!cd1 || !cd2) {
            auto *ccd = (cd1 ? cd1 : cd2);
            if (!ccd->decl.arith() || !lua_isnumber(L, 2 - !cd1)) {
                if (op_try_mt<mf1>(L, cd1, cd2)) {
                    return true;
                } else if ((mf2 != mf1) && op_try_mt<mf2>(L, cd2, cd1)) {
                    lua_pushboolean(L, !lua_toboolean(L, -1));
                    return true;
                }
                luaL_error(
                    L, "attempt to compare '%s' with '%s'",
                    ffi::lua_serialize(L, 1), ffi::lua_serialize(L, 2)
                );
            }
            arith_64bit_cmp(L, op);
            return true;
        }
        if (cd1->decl.arith() && cd2->decl.arith()) {
            /* compare values if both are arithmetic types */
            arith_64bit_cmp(L, op);
            return true;
        }
        /* compare only compatible pointers */
        if ((
            (cd1->decl.type() != ast::C_BUILTIN_PTR) ||
            (cd2->decl.type() != ast::C_BUILTIN_PTR)
        ) || (!cd1->decl.ptr_base().is_same(cd2->decl.ptr_base(), true))) {
            if (op_try_mt<mf1>(L, cd1, cd2)) {
                return true;
            } else if ((mf2 != mf1) && op_try_mt<mf2>(L, cd2, cd1)) {
                lua_pushboolean(L, !lua_toboolean(L, -1));
                return true;
            }
            luaL_error(
                L, "attempt to compare '%s' with '%s'",
                ffi::lua_serialize(L, 1), ffi::lua_serialize(L, 2)
            );
        }
        if (op_try_mt<mf1>(L, cd1, cd2)) {
            return true;
        } else if ((mf2 != mf1) && op_try_mt<mf2>(L, cd2, cd1)) {
            lua_pushboolean(L, !lua_toboolean(L, -1));
            return true;
        }
        return false;
    }

    static int lt(lua_State *L) {
        auto *cd1 = ffi::testcdata(L, 1);
        auto *cd2 = ffi::testcdata(L, 2);
        if (cmp_base<ffi::METATYPE_FLAG_LT, ffi::METATYPE_FLAG_LT>(
            L, ast::c_expr_binop::LT, cd1, cd2
        )) {
            return 1;
        }
        lua_pushboolean(L, cmp_addr(cd1) < cmp_addr(cd2));
        return 1;
    }

    static int le(lua_State *L) {
        auto *cd1 = ffi::testcdata(L, 1);
        auto *cd2 = ffi::testcdata(L, 2);
        /* tries both (a <= b) and not (b < a), like lua */
        if (cmp_base<ffi::METATYPE_FLAG_LE, ffi::METATYPE_FLAG_LT>(
            L, ast::c_expr_binop::LE, cd1, cd2
        )) {
            return 1;
        }
        lua_pushboolean(L, cmp_addr(cd1) <= cmp_addr(cd2));
        return 1;
    }

#if LUA_VERSION_NUM > 501
    static int pairs(lua_State *L) {
        auto *cd = ffi::testcdata(L, 1);
        if (op_try_mt<ffi::METATYPE_FLAG_PAIRS>(L, cd, nullptr, 3)) {
            return 3;
        }
        luaL_error(
            L, "attempt to iterate '%s'", ffi::lua_serialize(L, 1)
        );
        return 0;
    }

#if LUA_VERSION_NUM == 502
    static int ipairs(lua_State *L) {
        auto *cd = ffi::testcdata(L, 1);
        if (op_try_mt<ffi::METATYPE_FLAG_IPAIRS>(L, cd, nullptr, 3)) {
            return 3;
        }
        luaL_error(
            L, "attempt to iterate '%s'", ffi::lua_serialize(L, 1)
        );
        return 0;
    }
#endif

#if LUA_VERSION_NUM > 502
    template<ffi::metatype_flag mflag, ast::c_expr_binop bop>
    static int shift_bin(lua_State *L) {
        auto *cd1 = ffi::testcdata(L, 1);
        auto *cd2 = ffi::testcdata(L, 2);
        if (op_try_mt<mflag>(L, cd1, cd2)) {
            return 1;
        }
        ast::c_expr_type retp;
        ast::c_expr bexp{ast::C_TYPE_WEAK}, lhs, rhs;
        ast::c_expr_type lt = ffi::check_arith_expr(L, 1, lhs.val);
        ast::c_expr_type rt = ffi::check_arith_expr(L, 2, rhs.val);
        /* we're only promoting the left side in shifts */
        promote_long(lt);
        if (lt != ast::c_expr_type::ULLONG) {
            promote_to_64bit<long long, ast::c_expr_type::LLONG>(lt, &lhs.val);
        }
        lhs.type(lt);
        rhs.type(rt);
        bexp.type(ast::c_expr_type::BINARY);
        bexp.bin.op = bop;
        bexp.bin.lhs = &lhs;
        bexp.bin.rhs = &rhs;
        ast::c_value rv;
        if (!bexp.eval(L, rv, retp, true)) {
            lua_error(L);
        }
        ffi::make_cdata_arith(L, retp, rv);
        return 1;
    }

#if LUA_VERSION_NUM > 503
    static int close(lua_State *L) {
        auto *cd = ffi::testcdata(L, 1);
        if (cd && metatype_check<ffi::METATYPE_FLAG_CLOSE>(L, 1)) {
            lua_insert(L, 1);
            lua_call(L, 2, 0);
        }
        return 0;
    }
#endif /* LUA_VERSION_NUM > 503 */
#endif /* LUA_VERSION_NUM > 502 */
#endif /* LUA_VERSION_NUM > 501 */

    static void setup(lua_State *L) {
        if (!luaL_newmetatable(L, lua::CFFI_CDATA_MT)) {
            luaL_error(L, "unexpected error: registry reinitialized");
        }

        lua_pushliteral(L, "ffi");
        lua_setfield(L, -2, "__metatable");

        /* this will store registered permanent struct/union metatypes
         *
         * it's used instead of regular lua registry because there is no
         * way to reasonably garbage collect these references, and they die
         * with the rest of the ffi anyway, so...
         */
        lua_newtable(L);
        lua_setfield(L, -2, "__ffi_metatypes");

        lua_pushcfunction(L, tostring);
        lua_setfield(L, -2, "__tostring");

        lua_pushcfunction(L, gc);
        lua_setfield(L, -2, "__gc");

        lua_pushcfunction(L, call);
        lua_setfield(L, -2, "__call");

        lua_pushcfunction(L, index);
        lua_setfield(L, -2, "__index");

        lua_pushcfunction(L, newindex);
        lua_setfield(L, -2, "__newindex");

        lua_pushcfunction(L, concat);
        lua_setfield(L, -2, "__concat");

        lua_pushcfunction(L, len);
        lua_setfield(L, -2, "__len");

        lua_pushcfunction(L, add);
        lua_setfield(L, -2, "__add");

        lua_pushcfunction(L, sub);
        lua_setfield(L, -2, "__sub");

        lua_pushcfunction(L, (arith_bin<
            ffi::METATYPE_FLAG_MUL, ast::c_expr_binop::MUL
        >));
        lua_setfield(L, -2, "__mul");

        lua_pushcfunction(L, (arith_bin<
            ffi::METATYPE_FLAG_DIV, ast::c_expr_binop::DIV
        >));
        lua_setfield(L, -2, "__div");

        lua_pushcfunction(L, (arith_bin<
            ffi::METATYPE_FLAG_MOD, ast::c_expr_binop::MOD
        >));
        lua_setfield(L, -2, "__mod");

        lua_pushcfunction(L, pow);
        lua_setfield(L, -2, "__pow");

        lua_pushcfunction(L, (arith_un<
            ffi::METATYPE_FLAG_UNM, ast::c_expr_unop::UNM
        >));
        lua_setfield(L, -2, "__unm");

        lua_pushcfunction(L, eq);
        lua_setfield(L, -2, "__eq");

        lua_pushcfunction(L, lt);
        lua_setfield(L, -2, "__lt");

        lua_pushcfunction(L, le);
        lua_setfield(L, -2, "__le");

#if LUA_VERSION_NUM > 501
        lua_pushcfunction(L, pairs);
        lua_setfield(L, -2, "__pairs");

#if LUA_VERSION_NUM == 502
        lua_pushcfunction(L, ipairs);
        lua_setfield(L, -2, "__ipairs");
#endif

#if LUA_VERSION_NUM > 502
        lua_pushcfunction(L, (arith_bin<
            ffi::METATYPE_FLAG_IDIV, ast::c_expr_binop::DIV
        >));
        lua_setfield(L, -2, "__idiv");

        lua_pushcfunction(L, (arith_bin<
            ffi::METATYPE_FLAG_BAND, ast::c_expr_binop::BAND
        >));
        lua_setfield(L, -2, "__band");

        lua_pushcfunction(L, (arith_bin<
            ffi::METATYPE_FLAG_BOR, ast::c_expr_binop::BOR
        >));
        lua_setfield(L, -2, "__bor");

        lua_pushcfunction(L, (arith_bin<
            ffi::METATYPE_FLAG_BXOR, ast::c_expr_binop::BXOR
        >));
        lua_setfield(L, -2, "__bxor");

        lua_pushcfunction(L, (arith_un<
            ffi::METATYPE_FLAG_BNOT, ast::c_expr_unop::BNOT
        >));
        lua_setfield(L, -2, "__bnot");

        lua_pushcfunction(L, (shift_bin<
            ffi::METATYPE_FLAG_SHL, ast::c_expr_binop::LSH
        >));
        lua_setfield(L, -2, "__shl");

        lua_pushcfunction(L, (shift_bin<
            ffi::METATYPE_FLAG_SHR, ast::c_expr_binop::RSH
        >));
        lua_setfield(L, -2, "__shr");

#if LUA_VERSION_NUM > 503
        lua_pushcfunction(L, close);
        lua_setfield(L, -2, "__close");
#endif /* LUA_VERSION_NUM > 503 */
#endif /* LUA_VERSION_NUM > 502 */
#endif /* LUA_VERSION_NUM > 501 */

        lua_pop(L, 1);
    }
};

/* the ffi module itself */
struct ffi_module {
    static int cdef_f(lua_State *L) {
        std::size_t slen;
        char const *inp = luaL_checklstring(L, 1, &slen);
        parser::parse(L, inp, inp + slen, (lua_gettop(L) > 1) ? 2 : -1);
        return 0;
    }

    /* either gets a ctype or makes a ctype from a string */
    static ast::c_type const &check_ct(
        lua_State *L, int idx, int paridx = -1
    ) {
        if (ffi::iscval(L, idx)) {
            auto &cd = ffi::tocdata(L, idx);
            if (ffi::isctype(cd)) {
                return cd.decl;
            }
            auto &ct = ffi::newctype(L, cd.decl.copy());
            lua_replace(L, idx);
            return ct.decl;
        }
        std::size_t slen;
        char const *inp = luaL_checklstring(L, idx, &slen);
        auto &ct = ffi::newctype(
            L, parser::parse_type(L, inp, inp + slen, paridx)
        );
        lua_replace(L, idx);
        return ct.decl;
    }

    static int new_f(lua_State *L) {
        ffi::make_cdata(L, check_ct(L, 1), ffi::RULE_CONV, 2);
        return 1;
    }

    static int cast_f(lua_State *L) {
        luaL_checkany(L, 2);
        ffi::make_cdata(L, check_ct(L, 1), ffi::RULE_CAST, 2);
        return 1;
    }

    static int metatype_f(lua_State *L) {
        auto &ct = check_ct(L, 1);
        luaL_argcheck(
            L, ct.type() == ast::C_BUILTIN_RECORD, 1,
            "invalid C type"
        );
        int mflags;
        if (ct.record().metatype(mflags) != LUA_REFNIL) {
            luaL_error(L, "cannot change a protected metatable");
        }
        luaL_checktype(L, 2, LUA_TTABLE);

#define FIELD_CHECK(fname, flagn) { \
            lua_getfield(L, 2, "__" fname); \
            if (!lua_isnil(L, -1)) { \
                mflags |= ffi::METATYPE_FLAG_##flagn; \
            } \
            lua_pop(L, 1); \
        }

        FIELD_CHECK("add", ADD)
        FIELD_CHECK("sub", SUB)
        FIELD_CHECK("mul", MUL)
        FIELD_CHECK("div", DIV)
        FIELD_CHECK("mod", MOD)
        FIELD_CHECK("pow", POW)
        FIELD_CHECK("unm", UNM)
        FIELD_CHECK("concat", CONCAT)
        FIELD_CHECK("len", LEN)
        FIELD_CHECK("eq", EQ)
        FIELD_CHECK("lt", LT)
        FIELD_CHECK("le", LE)
        FIELD_CHECK("index", INDEX)
        FIELD_CHECK("newindex", NEWINDEX)
        FIELD_CHECK("call", CALL)
        FIELD_CHECK("gc", GC)
        FIELD_CHECK("tostring", TOSTRING)

#if LUA_VERSION_NUM > 501
        FIELD_CHECK("pairs", PAIRS)

#if LUA_VERSION_NUM == 502
        FIELD_CHECK("ipairs", IPAIRS)
#endif

#if LUA_VERSION_NUM > 502
        FIELD_CHECK("idiv", IDIV)
        FIELD_CHECK("band", BAND)
        FIELD_CHECK("bor", BOR)
        FIELD_CHECK("bxor", BXOR)
        FIELD_CHECK("bnot", BNOT)
        FIELD_CHECK("shl", SHL)
        FIELD_CHECK("shr", SHR)

        FIELD_CHECK("name", NAME)
#if LUA_VERSION_NUM > 503
        FIELD_CHECK("close", CLOSE)
#endif /* LUA_VERSION_NUM > 503 */
#endif /* LUA_VERSION_NUM > 502 */
#endif /* LUA_VERSION_NUM > 501 */

#undef FIELD_CHECK

        /* get the metatypes table on the stack */
        luaL_getmetatable(L, lua::CFFI_CDATA_MT);
        lua_getfield(L, -1, "__ffi_metatypes");
        /* the metatype */
        lua_pushvalue(L, 2);
        const_cast<ast::c_record &>(ct.record()).metatype(
            luaL_ref(L, -2), mflags
        );

        lua_pushvalue(L, 1);
        return 1; /* return the ctype */
    }

    static int load_f(lua_State *L) {
        char const *path = luaL_checkstring(L, 1);
        bool glob = (lua_gettop(L) >= 2) && lua_toboolean(L, 2);
        auto *c_ud = static_cast<lib::c_lib *>(
            lua_newuserdata(L, sizeof(lib::c_lib))
        );
        new (c_ud) lib::c_lib{};
        lib::load(c_ud, path, L, glob);
        return 1;
    }

    static int typeof_f(lua_State *L) {
        check_ct(L, 1, (lua_gettop(L) > 1) ? 2 : -1);
        /* make sure the type we've checked out is the result,
         * and not the last argument it's parameterized with
         */
        lua_pushvalue(L, 1);
        return 1;
    }

    static int addressof_f(lua_State *L) {
        auto &cd = ffi::checkcdata(L, 1);
        ffi::newcdata(L, ast::c_type{
            util::make_rc<ast::c_type>(util::move(cd.decl.unref())),
            0, ast::C_BUILTIN_PTR
        }, sizeof(void *)).as<void *>() = cd.address_of();
        return 1;
    }

    static int gc_f(lua_State *L) {
        auto &cd = ffi::checkcdata(L, 1);
        if (lua_isnil(L, 2)) {
            /* if nil and there is an existing finalizer, unset */
            if (cd.gc_ref != LUA_REFNIL) {
                luaL_unref(L, LUA_REGISTRYINDEX, cd.gc_ref);
                cd.gc_ref = LUA_REFNIL;
            }
        } else {
            /* new finalizer can be any type, it's pcall'd */
            lua_pushvalue(L, 2);
            cd.gc_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
        lua_pushvalue(L, 1); /* return the cdata */
        return 1;
    }

    static int sizeof_f(lua_State *L) {
        if (ffi::iscdata(L, 1)) {
            lua_pushinteger(L, ffi::cdata_value_size(L, 1));
            return 1;
        }
        auto get_vlasz = [L](std::size_t &sz, bool vla) -> bool {
            if (lua_isinteger(L, 2)) {
                auto isz = lua_tointeger(L, 2);
                if (isz < 0) {
                    return false;
                }
                sz = std::size_t(isz);
            } else if (lua_isnumber(L, 2)) {
                auto isz = lua_tonumber(L, 2);
                if (isz < 0) {
                    return false;
                }
                sz = std::size_t(isz);
            } else if (ffi::iscdata(L, 2)) {
                auto &cd = ffi::tocdata(L, 2);
                if (!cd.decl.integer()) {
                    luaL_checkinteger(L, 2);
                }
                if (cd.decl.is_unsigned()) {
                    sz = ffi::check_arith<std::size_t>(L, 2);
                } else {
                    auto isz = ffi::check_arith<long long>(L, 2);
                    if (isz < 0) {
                        return false;
                    }
                    sz = std::size_t(isz);
                }
            } else if (vla) {
                luaL_checkinteger(L, 2); /* this will longjmp */
                return false;
            } else {
                sz = 0;
            }
            return true;
        };
        auto &ct = check_ct(L, 1);
        if (ct.vla()) {
            std::size_t sz = 0;
            if (!get_vlasz(sz, true)) {
                return 0;
            }
            lua_pushinteger(L, ct.ptr_base().alloc_size() * sz);
            return 1;
        } else if (ct.flex()) {
            return 0;
        } else if (ct.type() == ast::C_BUILTIN_RECORD) {
            ast::c_type const *lf = nullptr;
            if (ct.record().flexible(&lf)) {
                std::size_t sz = 0;
                if (!get_vlasz(sz, lf->vla())) {
                    return 0;
                }
                lua_pushinteger(L, ct.alloc_size() + lf->ptr_base().alloc_size() * sz);
                return 1;
            }
        }
        lua_pushinteger(L, ct.alloc_size());
        return 1;
    }

    static int alignof_f(lua_State *L) {
        auto &ct = check_ct(L, 1);
        lua_pushinteger(L, ct.libffi_type()->alignment);
        return 1;
    }

    static int offsetof_f(lua_State *L) {
        auto &ct = check_ct(L, 1);
        char const *fname = luaL_checkstring(L, 2);
        if (ct.type() != ast::C_BUILTIN_RECORD) {
            return 0;
        }
        auto &cs = ct.record();
        if (cs.opaque()) {
            return 0;
        }
        ast::c_type const *tp;
        auto off = cs.field_offset(fname, tp);
        if (off >= 0) {
            lua_pushinteger(L, lua_Integer(off));
            return 1;
        }
        return 0;
    }

    static int istype_f(lua_State *L) {
        auto &ct = check_ct(L, 1);
        auto cd = ffi::testcval(L, 2);
        if (!cd) {
            lua_pushboolean(L, false);
            return 1;
        }
        if (ct.type() == ast::C_BUILTIN_RECORD) {
            /* if ct is a record, accept pointers to the struct */
            if (cd->decl.type() == ast::C_BUILTIN_PTR) {
                lua_pushboolean(L, ct.is_same(cd->decl.ptr_base(), true, true));
                return 1;
            }
        }
        lua_pushboolean(L, ct.is_same(cd->decl, true, true));
        return 1;
    }

    static int errno_f(lua_State *L) {
        int cur = errno;
        if (lua_gettop(L) >= 1) {
            errno = ffi::check_arith<int>(L, 1);
        }
        lua_pushinteger(L, cur);
        return 1;
    }

    static int string_f(lua_State *L) {
        if (!ffi::iscval(L, 1)) {
            if (lua_type(L, 1) == LUA_TSTRING) {
                /* allow passing through the string, but do not use
                 * lua_isstring as that allows numbers as well
                 */
                if (lua_gettop(L) <= 1) {
                    lua_pushvalue(L, 1);
                } else {
                    lua_pushlstring(
                        L, lua_tostring(L, 1),
                        ffi::check_arith<std::size_t>(L, 2)
                    );
                }
                return 1;
            }
            lua_pushfstring(
                L, "cannot convert '%s' to 'char const *'",
                luaL_typename(L, 1)
            );
            luaL_argcheck(L, false, 1, lua_tostring(L, -1));
        }
        auto &ud = ffi::tocdata(L, 1);
        /* make sure we deal with cdata */
        if (ffi::isctype(ud)) {
            luaL_argcheck(
                L, false, 1, "cannot convert 'ctype' to 'char const *'"
            );
        }
        /* handle potential ref case */
        void **valp = static_cast<void **>(ud.as_deref_ptr());
        if (lua_gettop(L) > 1) {
            /* if the length is given, special logic is used; any value can
             * be serialized here (addresses will be taken automatically)
             */
            auto slen = ffi::check_arith<std::size_t>(L, 2);
            switch (ud.decl.type()) {
                case ast::C_BUILTIN_PTR:
                case ast::C_BUILTIN_ARRAY:
                    lua_pushlstring(L, static_cast<char const *>(*valp), slen);
                    return 1;
                case ast::C_BUILTIN_RECORD:
                    lua_pushlstring(L, util::pun<char const *>(valp), slen);
                    return 1;
                default:
                    break;
            }
            goto converr;
        }
        /* if the length is not given, treat it like a string
         * the rules are still more loose here; arrays and pointers
         * are allowed, and their base type can be any kind of byte
         * signedness is not checked
         */
        if (!ud.decl.ptr_like()) {
            goto converr;
        }
        switch (ud.decl.ptr_base().type()) {
            case ast::C_BUILTIN_VOID:
            case ast::C_BUILTIN_CHAR:
            case ast::C_BUILTIN_SCHAR:
            case ast::C_BUILTIN_UCHAR:
                break;
            default:
                goto converr;
        }
        if (ud.decl.static_array()) {
            char const *strp = static_cast<char const *>(*valp);
            /* static arrays are special (no need for null termination) */
            auto slen = ud.decl.alloc_size();
            /* but if an embedded zero is found, terminate at that */
            auto *p = static_cast<char const *>(std::memchr(strp, '\0', slen));
            if (p) {
                slen = std::size_t(p - strp);
            }
            lua_pushlstring(L, strp, slen);
        } else {
            /* strings need to be null terminated */
            lua_pushstring(L, static_cast<char const *>(*valp));
        }
        return 1;
converr:
        ud.decl.serialize(L);
        lua_pushfstring(
            L, "cannot convert '%s' to 'string'", lua_tostring(L, -1)
        );
        luaL_argcheck(L, false, 1, lua_tostring(L, -1));
        return 1;
    }

    /* FIXME: type conversions (constness etc.) */
    static void *check_voidptr(lua_State *L, int idx) {
        if (ffi::iscval(L, idx)) {
            auto &cd = ffi::tocdata(L, idx);
            if (ffi::isctype(cd)) {
                luaL_argcheck(
                    L, false, idx, "cannot convert 'ctype' to 'void *'"
                );
            }
            if (cd.decl.ptr_like()) {
                return cd.as_deref<void *>();
            }
            if (cd.decl.is_ref()) {
                return cd.as_ptr();
            }
            cd.decl.serialize(L);
            lua_pushfstring(
                L, "cannot convert '%s' to 'void *'",
                lua_tostring(L, -1)
            );
            goto argcheck;
        } else if (lua_isuserdata(L, idx)) {
            return lua_touserdata(L, idx);
        }
        lua_pushfstring(
            L, "cannot convert '%s' to 'void *'",
            luaL_typename(L, 1)
        );
argcheck:
        luaL_argcheck(L, false, idx, lua_tostring(L, -1));
        return nullptr;
    }

    /* FIXME: lengths (and character) in these APIs may be given by cdata... */

    static int copy_f(lua_State *L) {
        void *dst = check_voidptr(L, 1);
        void const *src;
        std::size_t len;
        if (lua_isstring(L, 2)) {
            src = lua_tostring(L, 2);
            if (lua_gettop(L) <= 2) {
                len = lua_rawlen(L, 2);
            } else {
                len = ffi::check_arith<std::size_t>(L, 3);
            }
        } else {
            src = check_voidptr(L, 2);
            len = ffi::check_arith<std::size_t>(L, 3);
        }
        std::memcpy(dst, src, len);
        return 0;
    }

    static int fill_f(lua_State *L) {
        void *dst = check_voidptr(L, 1);
        auto len = ffi::check_arith<std::size_t>(L, 2);
        int byte = int(luaL_optinteger(L, 3, 0));
        std::memset(dst, byte, len);
        return 0;
    }

    static int tonumber_f(lua_State *L) {
        auto *cd = ffi::testcdata(L, 1);
        if (cd) {
            if (cd->decl.arith()) {
                ffi::to_lua(
                    L, cd->decl, cd->as_deref_ptr(), ffi::RULE_CONV, false, true
                );
                return 1;
            }
            switch (cd->decl.type()) {
                case ast::C_BUILTIN_PTR:
                case ast::C_BUILTIN_RECORD:
                case ast::C_BUILTIN_ARRAY:
                case ast::C_BUILTIN_FUNC:
                    /* these may appear */
                    lua_pushnil(L);
                    return 1;
                default:
                    /* these should not */
                    assert(false);
                    lua_pushnil(L);
                    return 1;
            }
        }
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
        return lua_gettop(L);
    }

    static int toretval_f(lua_State *L) {
        auto &cd = ffi::checkcdata(L, 1);
        ffi::to_lua(L, cd.decl, &cd.as<void *>(), ffi::RULE_RET, false);
        return 1;
    }

    static int eval_f(lua_State *L) {
        /* TODO: accept expressions */
        char const *str = luaL_checkstring(L, 1);
        ast::c_value outv;
        auto v = parser::parse_number(L, outv, str, str + lua_rawlen(L, 1));
        ffi::make_cdata_arith(L, v, outv);
        return 1;
    }

    static int type_f(lua_State *L) {
        if (ffi::iscval(L, 1)) {
            lua_pushliteral(L, "cdata");
            return 1;
        }
        luaL_checkany(L, 1);
        lua_pushstring(L, luaL_typename(L, 1));
        return 1;
    }

    static int abi_f(lua_State *L) {
        luaL_checkstring(L, 1);
        lua_pushvalue(L, 1);
        lua_rawget(L, lua_upvalueindex(1));
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushboolean(L, false);
        }
        return 1;
    }

    static void setup_abi(lua_State *L) {
        lua_newtable(L);
        lua_pushboolean(L, true);
        if (sizeof(void *) == 8) {
            lua_setfield(L, -2, "64bit");
        } else if (sizeof(void *) == 4) {
            lua_setfield(L, -2, "32bit");
        } else {
            lua_pop(L, 1);
        }
        lua_pushboolean(L, true);
#if defined(FFI_BIG_ENDIAN)
        lua_setfield(L, -2, "be");
#else
        lua_setfield(L, -2, "le");
#endif
#ifdef FFI_WINDOWS_ABI
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "win");
#endif
#ifdef FFI_WINDOWS_UWP
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "uwp");
#endif
#ifdef FFI_ARM_EABI
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "eabi");
#endif
#if FFI_ARCH == FFI_ARCH_PPC64 && defined(_CALL_ELF) && _CALL_ELF == 2
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "elfv2");
#endif
#if FFI_ARCH_HAS_FPU == 1
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "fpu");
#endif
        lua_pushboolean(L, true);
#if FFI_ARCH_SOFTFP == 1
        lua_setfield(L, -2, "softfp");
#else
        lua_setfield(L, -2, "hardfp");
#endif
#ifdef FFI_ABI_UNIONVAL
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "unionval");
#endif
    }

    static void setup(lua_State *L) {
        static luaL_Reg const lib_def[] = {
            /* core */
            {"cdef", cdef_f},
            {"load", load_f},

            /* data handling */
            {"new", new_f},
            {"cast", cast_f},
            {"metatype", metatype_f},
            {"typeof", typeof_f},
            {"addressof", addressof_f},
            {"gc", gc_f},

            /* type info */
            {"sizeof", sizeof_f},
            {"alignof", alignof_f},
            {"offsetof", offsetof_f},
            {"istype", istype_f},

            /* utilities */
            {"errno", errno_f},
            {"string", string_f},
            {"copy", copy_f},
            {"fill", fill_f},
            {"toretval", toretval_f},
            {"eval", eval_f},
            {"type", type_f},

            {nullptr, nullptr}
        };
        luaL_newlib(L, lib_def);

        lua_pushliteral(L, FFI_OS_NAME);
        lua_setfield(L, -2, "os");

        lua_pushliteral(L, FFI_ARCH_NAME);
        lua_setfield(L, -2, "arch");

        setup_abi(L);
        lua_pushcclosure(L, abi_f, 1);
        lua_setfield(L, -2, "abi");

        /* FIXME: relying on the global table being intact */
        lua_getglobal(L, "tonumber");
        lua_pushcclosure(L, tonumber_f, 1);
        lua_setfield(L, -2, "tonumber");

        /* NULL = (void *)0 */
        ffi::newcdata(L, ast::c_type{
            util::make_rc<ast::c_type>(ast::C_BUILTIN_VOID, 0),
            0, ast::C_BUILTIN_PTR
        }, sizeof(void *)).as<void *>() = nullptr;
        lua_setfield(L, -2, "nullptr");
    }

    static void setup_dstor(lua_State *L) {
        /* our declaration storage is a userdata in the registry */
        auto *ds = static_cast<ast::decl_store *>(
            lua_newuserdata(L, sizeof(ast::decl_store))
        );
        new (ds) ast::decl_store{};
        /* stack: dstor */
        lua_newtable(L);
        /* stack: dstor, mt */
        lua_pushcfunction(L, [](lua_State *LL) -> int {
            using T = ast::decl_store;
            auto *dsp = lua::touserdata<T>(LL, 1);
            dsp->~T();
            return 0;
        });
        /* stack: dstor, mt, __gc */
        lua_setfield(L, -2, "__gc");
        /* stack: dstor, __mt */
        lua_setmetatable(L, -2);
        /* stack: dstor */
        lua_setfield(L, LUA_REGISTRYINDEX, lua::CFFI_DECL_STOR);
        /* stack: empty */
    }

    static void open(lua_State *L) {
        setup_dstor(L); /* declaration store */
        parser::init(L);

        /* cdata handles */
        cdata_meta::setup(L);

        setup(L); /* push table to stack */

        /* lib handles, needs the module table on the stack */
        auto *c_ud = static_cast<lib::c_lib *>(
            lua_newuserdata(L, sizeof(lib::c_lib))
        );
        new (c_ud) lib::c_lib{};
        lib::load(c_ud, nullptr, L, false);
        lib_meta::setup(L);
    }
};

void ffi_module_open(lua_State *L) {
    ffi_module::open(L);
}
