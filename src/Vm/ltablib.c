/*
** $Id: ltablib.c $
** Library for Table Manipulation
** See Copyright Notice in lua.h
*/

#define ltablib_c
#define LUA_LIB

#include "lprefix.h"


#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "llimits.h"


/*
** Operations that an object must define to mimic a table
** (some functions only need some of them)
*/
#define TAB_R	1			/* read */
#define TAB_W	2			/* write */
#define TAB_L	4			/* length */
#define TAB_RW	(TAB_R | TAB_W)		/* read/write */


#define aux_getn(L,n,w)	(checktab(L, n, (w) | TAB_L), luaL_len(L, n))


static int checkfield (lua_State *L, const char *key, int n) {
  lua_pushstring(L, key);
  return (lua_rawget(L, -n) != LUA_TNIL);
}


/*
** Check that 'arg' either is a table or can behave like one (that is,
** has a metatable with the required metamethods)
*/
static void checktab (lua_State *L, int arg, int what) {
  if (lua_type(L, arg) != LUA_TTABLE) {  /* is it not a table? */
    int n = 1;  /* number of elements to pop */
    if (lua_getmetatable(L, arg) &&  /* must have metatable */
        (!(what & TAB_R) || checkfield(L, "__index", ++n)) &&
        (!(what & TAB_W) || checkfield(L, "__newindex", ++n)) &&
        (!(what & TAB_L) || checkfield(L, "__len", ++n))) {
      lua_pop(L, n);  /* pop metatable and tested metamethods */
    }
    else
      luaL_checktype(L, arg, LUA_TTABLE);  /* force an error */
  }
}

/* tcreate - receiver is arg1, narray is arg2, nhash is arg3 */

static int tcreate (lua_State *L) {
  lua_Unsigned sizeseq = (lua_Unsigned)luaL_checkinteger(L, 2);
  lua_Unsigned sizerest = (lua_Unsigned)luaL_optinteger(L, 3, 0);
  luaL_argcheck(L, sizeseq <= cast_uint(INT_MAX), 2, "out of range");
  luaL_argcheck(L, sizerest <= cast_uint(INT_MAX), 3, "out of range");
  lua_createtable(L, cast_int(sizeseq), cast_int(sizerest));
  return 1;
}


/* tinsert - receiver is arg1, table is arg2, pos/value are arg3/arg4 */
static int tinsert (lua_State *L) {
  lua_Integer pos;  /* where to insert new element */
  lua_Integer e = aux_getn(L, 2, TAB_RW);
  /* 'e' is the length; in 0-based, first empty slot is index e */
  switch (lua_gettop(L)) {
  case 3: {  /* called with only 2 arguments */
    pos = e;  /* insert new element at the end */
    break;
  }
  case 4: {
    lua_Integer i;
    pos = luaL_checkinteger(L, 3);  /* 2nd argument is the position */
    /* check whether 'pos' is in [0, e] */
    luaL_argcheck(L, (lua_Unsigned)pos <= (lua_Unsigned)e, 3,
                  "position out of bounds");
    for (i = e; i > pos; i--) {  /* move up elements */
      lua_geti(L, 2, i - 1);
      lua_seti(L, 2, i);  /* t[i] = t[i - 1] */
    }
    break;
  }
  default: {
    return luaL_error(L, "wrong number of arguments to 'insert'");
  }
  }
  lua_seti(L, 2, pos);  /* t[pos] = v */
  return 0;
}

/* tremove - receiver is arg1, table is arg2, pos is arg3 */

static int tremove (lua_State *L) {
  lua_Integer size = aux_getn(L, 2, TAB_RW);
  lua_Integer pos = luaL_optinteger(L, 3, (size > 0) ? size - 1 : 0);
  if (size == 0) {  /* empty table? */
    lua_pushnil(L);  /* return nil, do nothing */
    return 1;
  }
  /* check whether 'pos' is in [0, size - 1] */
  luaL_argcheck(L, (lua_Unsigned)pos < (lua_Unsigned)size, 3,
                "position out of bounds");
  lua_geti(L, 2, pos);  /* result = t[pos] */
  for ( ; pos < size - 1; pos++) {
    lua_geti(L, 2, pos + 1);
    lua_seti(L, 2, pos);  /* t[pos] = t[pos + 1] */
  }
  lua_pushnil(L);
  lua_seti(L, 2, pos);  /* remove entry t[pos] */
  return 1;
}


/*
** Copy elements (1[f], ..., 1[e]) into (tt[t], tt[t+1], ...). Whenever
** possible, copy in increasing order, which is better for rehashing.
** tmove - receiver is arg1, src is arg2, f is arg3, e is arg4, t is arg5, dst is arg6
** "possible" means destination after original range, or smaller than origin, or copying to another table.
*/
static int tmove (lua_State *L) {
  lua_Integer f = luaL_checkinteger(L, 3);
  lua_Integer e = luaL_checkinteger(L, 4);
  lua_Integer t = luaL_checkinteger(L, 5);
  int tt = !lua_isnoneornil(L, 6) ? 6 : 2;  /* destination table */
  checktab(L, 2, TAB_R);
  checktab(L, tt, TAB_W);
  if (e >= f) {  /* otherwise, nothing to move */
    lua_Integer n, i;
    luaL_argcheck(L, f >= 0 || e < LUA_MAXINTEGER + f, 3,
                  "too many elements to move");
    n = e - f + 1;  /* number of elements to move */
    luaL_argcheck(L, t <= LUA_MAXINTEGER - n + 1, 5,
                  "destination wrap around");
    if (t > e || t <= f || (tt != 2 && !lua_compare(L, 2, tt, LUA_OPEQ))) {
      for (i = 0; i < n; i++) {
        lua_geti(L, 2, f + i);
        lua_seti(L, tt, t + i);
      }
    }
    else {
      for (i = n - 1; i >= 0; i--) {
        lua_geti(L, 2, f + i);
        lua_seti(L, tt, t + i);
      }
    }
  }
  lua_pushvalue(L, tt);  /* return destination table */
  return 1;
}


static void addfield (lua_State *L, luaL_Buffer *b, lua_Integer i) {
  /* tconcat - receiver is arg1, table is arg2, sep is arg3, i is arg4, j is arg5 */
  lua_geti(L, 2, i);
  if (l_unlikely(!lua_isstring(L, -1)))
    luaL_error(L, "invalid value (%s) at index %I in table for 'concat'",
               luaL_typename(L, -1), (LUAI_UACINT)i);
  luaL_addvalue(b);
}


static int tconcat (lua_State *L) {
  luaL_Buffer b;
  lua_Integer last = aux_getn(L, 2, TAB_R);  /* length */
  size_t lsep;
  const char *sep = luaL_optlstring(L, 3, "", &lsep);
  lua_Integer i = luaL_optinteger(L, 4, 0);           /* default start: 0 */
  last = luaL_optinteger(L, 5, last - 1);             /* default end: last valid index */
  luaL_buffinit(L, &b);
  for (; i < last; i++) {
    addfield(L, &b, i);
    luaL_addlstring(&b, sep, lsep);
  }
  if (i == last)  /* add last value (if interval was not empty) */
    addfield(L, &b, i);
  luaL_pushresult(&b);
  return 1;
}


/*
** {======================================================
** Pack/unpack
** =======================================================
*/

/* tpack - receiver is arg1, values start from arg2 */
static int tpack (lua_State *L) {
  int i;
  int n = lua_gettop(L) - 1;  /* number of elements to pack */
  lua_createtable(L, n, 1);  /* create result table */
  lua_insert(L, 1);  /* put it at index 1 */
  for (i = n; i >= 1; i--)  /* assign elements */
    lua_seti(L, 1, i - 1);  /* 0-based: args stored at indices 0..n-1 */
  lua_pushinteger(L, n);
  lua_setfield(L, 1, "n");  /* t.n = number of elements */
  return 1;  /* return table */
}


/* tunpack - receiver is arg1, table is arg2, i is arg3, e is arg4 */
static int tunpack (lua_State *L) {
  lua_Unsigned n;
  lua_Integer i = luaL_optinteger(L, 3, 0);           /* default start: 0 */
  lua_Integer e = luaL_opt(L, luaL_checkinteger, 4, luaL_len(L, 2));  /* exclusive end */
  if (i >= e) return 0;  /* empty range */
  n = l_castS2U(e) - l_castS2U(i);  /* number of elements */
  if (l_unlikely(n >= (unsigned int)INT_MAX  ||
                 !lua_checkstack(L, (int)(n))))
    return luaL_error(L, "too many results to unpack");
  for (; i < e; i++) {  /* push t[i..e-1] */
    lua_geti(L, 2, i);
  }
  return (int)n;
}

/* }====================================================== */



/*
** {======================================================
** Quicksort
** (based on 'Algorithms in MODULA-3', Robert Sedgewick;
**  Addison-Wesley, 1993.)
** =======================================================
*/


/*
** Type for array indices. These indices are always limited by INT_MAX,
** so it is safe to cast them to lua_Integer even for Lua 32 bits.
*/
typedef unsigned int IdxT;


/* Versions of lua_seti/lua_geti specialized for IdxT */
#define geti(L,idt,idx)	lua_geti(L, idt, l_castU2S(idx))
#define seti(L,idt,idx)	lua_seti(L, idt, l_castU2S(idx))


/*
** Produce a "random" 'unsigned int' to randomize pivot choice. This
** macro is used only when 'sort' detects a big imbalance in the result
** of a partition. (If you don't want/need this "randomness", ~0 is a
** good choice.)
*/
#if !defined(l_randomizePivot)
#define l_randomizePivot(L)	luaL_makeseed(L)
#endif					/* } */


/* arrays larger than 'RANLIMIT' may use randomized pivots */
#define RANLIMIT	100u


static void set2 (lua_State *L, IdxT i, IdxT j) {
  seti(L, 2, i);
  seti(L, 2, j);
}


/*
** Return true iff value at stack index 'a' is less than the value at
** index 'b' (according to the order of the sort).
*/
static int sort_comp (lua_State *L, int a, int b) {
  if (lua_isnil(L, 3))  /* no function? */
    return lua_compare(L, a, b, LUA_OPLT);  /* a < b */
  else {  /* function */
    int res;
    lua_pushvalue(L, 3);    /* push function */
    lua_pushvalue(L, a-1);  /* -1 to compensate function */
    lua_pushvalue(L, b-2);  /* -2 to compensate function and 'a' */
    lua_call(L, 2, 1);      /* call function */
    res = lua_toboolean(L, -1);  /* get result */
    lua_pop(L, 1);          /* pop result */
    return res;
  }
}


/*
** Does the partition: Pivot P is at the top of the stack.
** precondition: a[lo] <= P == a[up-1] <= a[up],
** so it only needs to do the partition from lo + 1 to up - 2.
** Pos-condition: a[lo .. i - 1] <= a[i] == P <= a[i + 1 .. up]
** returns 'i'.
*/
static IdxT partition (lua_State *L, IdxT lo, IdxT up) {
  IdxT i = lo;  /* will be incremented before first use */
  IdxT j = up - 1;  /* will be decremented before first use */
  /* loop invariant: a[lo .. i] <= P <= a[j .. up] */
  for (;;) {
    /* next loop: repeat ++i while a[i] < P */
    while ((void)geti(L, 2, ++i), sort_comp(L, -1, -2)) {
      if (l_unlikely(i == up - 1))  /* a[up - 1] < P == a[up - 1] */
        luaL_error(L, "invalid order function for sorting");
      lua_pop(L, 1);  /* remove a[i] */
    }
    /* after the loop, a[i] >= P and a[lo .. i - 1] < P  (a) */
    /* next loop: repeat --j while P < a[j] */
    while ((void)geti(L, 2, --j), sort_comp(L, -3, -1)) {
      if (l_unlikely(j < i))  /* j <= i - 1 and a[j] > P, contradicts (a) */
        luaL_error(L, "invalid order function for sorting");
      lua_pop(L, 1);  /* remove a[j] */
    }
    /* after the loop, a[j] <= P and a[j + 1 .. up] >= P */
    if (j < i) {  /* no elements out of place? */
      /* a[lo .. i - 1] <= P <= a[j + 1 .. i .. up] */
      lua_pop(L, 1);  /* pop a[j] */
      /* swap pivot (a[up - 1]) with a[i] to satisfy pos-condition */
      set2(L, up - 1, i);
      return i;
    }
    /* otherwise, swap a[i] - a[j] to restore invariant and repeat */
    set2(L, i, j);
  }
}


/*
** Choose an element in the middle (2nd-3th quarters) of [lo,up]
** "randomized" by 'rnd'
*/
static IdxT choosePivot (IdxT lo, IdxT up, unsigned int rnd) {
  IdxT r4 = (up - lo) / 4;  /* range/4 */
  IdxT p = (rnd ^ lo ^ up) % (r4 * 2) + (lo + r4);
  lua_assert(lo + r4 <= p && p <= up - r4);
  return p;
}


/*
** Quicksort algorithm (recursive function)
*/
static void auxsort (lua_State *L, IdxT lo, IdxT up, unsigned rnd) {
  while (lo < up) {  /* loop for tail recursion */
    IdxT p;  /* Pivot index */
    IdxT n;  /* to be used later */
    /* sort elements 'lo', 'p', and 'up' */
    geti(L, 2, lo);
    geti(L, 2, up);
    if (sort_comp(L, -1, -2))  /* a[up] < a[lo]? */
      set2(L, lo, up);  /* swap a[lo] - a[up] */
    else
      lua_pop(L, 2);  /* remove both values */
    if (up - lo == 1)  /* only 2 elements? */
      return;  /* already sorted */
    if (up - lo < RANLIMIT || rnd == 0)  /* small interval or no randomize? */
      p = (lo + up)/2;  /* middle element is a good pivot */
    else  /* for larger intervals, it is worth a random pivot */
      p = choosePivot(lo, up, rnd);
    geti(L, 2, p);
    geti(L, 2, lo);
    if (sort_comp(L, -2, -1))  /* a[p] < a[lo]? */
      set2(L, p, lo);  /* swap a[p] - a[lo] */
    else {
      lua_pop(L, 1);  /* remove a[lo] */
      geti(L, 2, up);
      if (sort_comp(L, -1, -2))  /* a[up] < a[p]? */
        set2(L, p, up);  /* swap a[up] - a[p] */
      else
        lua_pop(L, 2);
    }
    if (up - lo == 2)  /* only 3 elements? */
      return;  /* already sorted */
    geti(L, 2, p);  /* get middle element (Pivot) */
    lua_pushvalue(L, -1);  /* push Pivot */
    geti(L, 2, up - 1);  /* push a[up - 1] */
    set2(L, p, up - 1);  /* swap Pivot (a[p]) with a[up - 1] */
    p = partition(L, lo, up);
    /* a[lo .. p - 1] <= a[p] == P <= a[p + 1 .. up] */
    if (p - lo < up - p) {  /* lower interval is smaller? */
      auxsort(L, lo, p - 1, rnd);  /* call recursively for lower interval */
      n = p - lo;  /* size of smaller interval */
      lo = p + 1;  /* tail call for [p + 1 .. up] (upper interval) */
    }
    else {
      auxsort(L, p + 1, up, rnd);  /* call recursively for upper interval */
      n = up - p;  /* size of smaller interval */
      up = p - 1;  /* tail call for [lo .. p - 1]  (lower interval) */
    }
    if ((up - lo) / 128 > n) /* partition too imbalanced? */
      rnd = l_randomizePivot(L);  /* try a new randomization */
  }  /* tail call auxsort(L, lo, up, rnd) */
}


/* sort - receiver is arg1, table is arg2, comp is arg3 */
static int sort (lua_State *L) {
  lua_Integer n = aux_getn(L, 2, TAB_RW);
  if (n > 1) {  /* non-trivial interval? */
    luaL_argcheck(L, n < INT_MAX, 2, "array too big");
    if (!lua_isnoneornil(L, 3))  /* is there a 2nd argument? */
      luaL_checktype(L, 3, LUA_TFUNCTION);  /* must be a function */
    lua_settop(L, 3);  /* make sure there are two arguments */
    auxsort(L, 0, (IdxT)(n - 1), 0);  /* sort 0-based: indices 0..n-1 */
  }
  return 0;
}

/* }====================================================== */


static const luaL_Reg tab_funcs[] = {
    {"concat", tconcat},
    {"create", tcreate},
    {"insert", tinsert},
    {"pack", tpack},
    {"unpack", tunpack},
    {"remove", tremove},
    {"move", tmove},
    {"sort", sort},
    {NULL, NULL}
};


LUAMOD_API int luaopen_table (lua_State *L) {
  luaL_newlib(L, tab_funcs);
  return 1;
}