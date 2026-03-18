/*
** $Id: lutf8lib.c $
** Standard library for UTF-8 manipulation
** See Copyright Notice in lua.h
*/

#define lutf8lib_c
#define LUA_LIB

#include "lprefix.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "llimits.h"
#include "lualib.h"

#define MAXUNICODE 0x10FFFFu

#define MAXUTF 0x7FFFFFFFu

#define MSGInvalid "invalid UTF-8 code"

#define iscont(c) (((c) & 0xC0) == 0x80)
#define iscontp(p) iscont(*(p))

/* from strlib */
/* translate a relative string position (0-based): negative means back from end */
static lua_Integer u_posrelat(lua_Integer pos, size_t len) {
  if (pos >= 0)
    return pos;
  else if (0u - (size_t)pos > len)
    return -1; /* before start */
  else
    return (lua_Integer)len + pos; /* -1 -> len-1 */
}

/*
** Decode one UTF-8 sequence, returning NULL if byte sequence is
** invalid.  The array 'limits' stores the minimum value for each
** sequence length, to check for overlong representations. Its first
** entry forces an error for non-ASCII bytes with no continuation
** bytes (count == 0).
*/
static const char *utf8_decode(const char *s, l_uint32 *val, int strict) {
  static const l_uint32 limits[] = {~(l_uint32)0, 0x80, 0x800, 0x10000u, 0x200000u, 0x4000000u};
  unsigned int c = (unsigned char)s[0];
  l_uint32 res = 0; /* final result */
  if (c < 0x80)     /* ASCII? */
    res = c;
  else {
    int count = 0;                                 /* to count number of continuation bytes */
    for (; c & 0x40; c <<= 1) {                    /* while it needs continuation bytes... */
      unsigned int cc = (unsigned char)s[++count]; /* read next byte */
      if (!iscont(cc))                             /* not a continuation byte? */
        return NULL;                               /* invalid byte sequence */
      res = (res << 6) | (cc & 0x3F);              /* add lower 6 bits from cont. byte */
    }
    res |= ((l_uint32)(c & 0x7F) << (count * 5)); /* add first byte */
    if (count > 5 || res > MAXUTF || res < limits[count])
      return NULL; /* invalid byte sequence */
    s += count;    /* skip continuation bytes read */
  }
  if (strict) {
    /* check for invalid code points; too large or surrogates */
    if (res > MAXUNICODE || (0xD800u <= res && res <= 0xDFFFu))
      return NULL;
  }
  if (val)
    *val = res;
  return s + 1; /* +1 to include first byte */
}

/* utflen - receiver is arg1, s is arg2, len is arg3 (optional), */
static int utflen(lua_State *L) {
  lua_Integer n = 0; /* counter for the number of characters */
  size_t len;        /* string length in bytes */
  const char *s = luaL_checklstring(L, 2, &len);
  lua_Integer posi = u_posrelat(luaL_optinteger(L, 3, 0), len);
  lua_Integer posj = u_posrelat(luaL_optinteger(L, 4, (lua_Integer)len), len);
  int lax = lua_toboolean(L, 5);
  luaL_argcheck(L, 0 <= posi && posi <= (lua_Integer)len, 2, "initial position out of bounds");
  luaL_argcheck(L, posj <= (lua_Integer)len, 3, "final position out of bounds");
  while (posi < posj) {
    const char *s1 = utf8_decode(s + posi, NULL, !lax);
    if (s1 == NULL) {               /* conversion error? */
      luaL_pushfail(L);             /* return fail ... */
      lua_pushinteger(L, posi + 1); /* ... and current position */
      return 2;
    }
    posi = ct_diff2S(s1 - s);
    n++;
  }
  lua_pushinteger(L, n);
  return 1;
}

/*
** codepoint(s, [i, [j [, lax]]]) -> returns codepoints for all
** characters that start in the range [i,j]
*/
/* codepoint - receiver is arg1, s is arg2, i is arg3, j is arg4, lax is arg5 */
static int codepoint(lua_State *L) {
  size_t len;
  const char *s = luaL_checklstring(L, 2, &len);
  lua_Integer posi = u_posrelat(luaL_optinteger(L, 3, 0), len);
  lua_Integer pose = u_posrelat(luaL_optinteger(L, 4, posi + 1), len);
  int lax = lua_toboolean(L, 5);
  int n;
  const char *se;
  luaL_argcheck(L, posi >= 0, 3, "out of bounds");
  luaL_argcheck(L, pose <= (lua_Integer)len, 4, "out of bounds");
  if (posi > pose)
    return 0;                 /* empty interval; return no values */
  if (pose - posi >= INT_MAX) /* (lua_Integer -> int) overflow? */
    return luaL_error(L, "string slice too long");
  n = (int)(pose - posi); /* upper bound for number of returns */
  luaL_checkstack(L, n, "string slice too long");
  n = 0;         /* count the number of returns */
  se = s + pose; /* string end */
  for (s += posi; s < se;) {
    l_uint32 code;
    s = utf8_decode(s, &code, !lax);
    if (s == NULL)
      return luaL_error(L, MSGInvalid);
    lua_pushinteger(L, l_castU2S(code));
    n++;
  }
  return n;
}

static void pushutfchar(lua_State *L, int arg) {
  lua_Unsigned code = (lua_Unsigned)luaL_checkinteger(L, arg);
  luaL_argcheck(L, code <= MAXUTF, arg, "value out of range");
  lua_pushfstring(L, "%U", (long)code);
}

/*
** utfchar(n1, n2, ...)  -> char(n1)..char(n2)...
*/
/* utfchar - receiver is arg1, values start from arg2 */
static int utfchar(lua_State *L) {
  int n = lua_gettop(L) - 1; /* number of arguments (excluding receiver) */
  if (n == 1)                /* optimize common case of single char */
    pushutfchar(L, 2);
  else {
    int i;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (i = 2; i <= n + 1; i++) {
      pushutfchar(L, i);
      luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
  }
  return 1;
}

/*
** offset(s, n, [i])  -> indices where n-th character counting from
**   position 'i' starts and ends; 0 means character at 'i'.
*/
/* byteoffset - receiver is arg1, s is arg2, n is arg3, i is arg4 */
static int byteoffset(lua_State *L) {
  size_t len;
  const char *s = luaL_checklstring(L, 2, &len);
  lua_Integer n = luaL_checkinteger(L, 3);
  lua_Integer posi = (n >= 0) ? 0 : cast_st2S(len);
  posi = u_posrelat(luaL_optinteger(L, 4, posi), len);
  luaL_argcheck(L, 0 <= posi && posi <= (lua_Integer)len, 4, "position out of bounds");
  if (n == 0) {
    /* find beginning of current byte sequence */
    while (posi > 0 && iscontp(s + posi))
      posi--;
  } else {
    if (iscontp(s + posi))
      return luaL_error(L, "initial position is a continuation byte");
    if (n < 0) {
      while (n < 0 && posi > 0) { /* move back */
        do {                      /* find beginning of previous character */
          posi--;
        } while (posi > 0 && iscontp(s + posi));
        n++;
      }
    } else {
      while (n > 0 && posi < (lua_Integer)len) {
        do { /* find beginning of next character */
          posi++;
        } while (iscontp(s + posi)); /* (cannot pass final '\0') */
        n--;
      }
    }
  }
  if (n != 0) { /* did not find given character? */
    luaL_pushfail(L);
    return 1;
  }
  lua_pushinteger(L, posi);    /* initial position (0-based) */
  if ((s[posi] & 0x80) != 0) { /* multi-byte character? */
    if (iscont(s[posi]))
      return luaL_error(L, "initial position is a continuation byte");
    while (iscontp(s + posi + 1))
      posi++; /* skip to last continuation byte */
  }
  /* else one-byte character: final position is the initial one */
  lua_pushinteger(L, posi + 1); /* exclusive end position */
  return 2;
}

static int iter_aux(lua_State *L, int strict) {
  size_t len;
  const char *s = luaL_checklstring(L, 2, &len);
  lua_Integer sn = lua_tointeger(L, 3);
  lua_Unsigned n;
  if (sn < 0) {
    n = 0; /* first iteration: start from byte 0 */
  } else {
    n = (lua_Unsigned)sn;
    if (n < len)
      do {
        n++;
      } while (n < len && iscontp(s + n)); /* advance past current char */
  }
  if (n >= len)
    return 0; /* no more codepoints */
  else {
    l_uint32 code;
    const char *next = utf8_decode(s + n, &code, strict);
    if (next == NULL || iscontp(next))
      return luaL_error(L, MSGInvalid);
    lua_pushinteger(L, l_castU2S(n)); /* 0-based byte position */
    lua_pushinteger(L, l_castU2S(code));
    return 2;
  }
}

static int iter_auxstrict(lua_State *L) { return iter_aux(L, 1); }

static int iter_auxlax(lua_State *L) { return iter_aux(L, 0); }

/* iter_codes - receiver is arg1, s is arg2, lax is arg3 */
static int iter_codes(lua_State *L) {
  int lax = lua_toboolean(L, 3);
  const char *s = luaL_checkstring(L, 2);
  luaL_argcheck(L, !iscontp(s), 2, MSGInvalid);
  lua_pushcfunction(L, lax ? iter_auxlax : iter_auxstrict);
  lua_pushvalue(L, 2);
  lua_pushinteger(L, -1); /* initial control: before first byte */
  return 3;
}

/* pattern to match a single UTF-8 character */
#define UTF8PATT "[\0-\x7F\xC2-\xFD][\x80-\xBF]*"

static const luaL_Reg funcs[] = {{"offset", byteoffset},
                                 {"codepoint", codepoint},
                                 {"char", utfchar},
                                 {"len", utflen},
                                 {"codes", iter_codes},
                                 /* placeholders */
                                 {"charpattern", NULL},
                                 {NULL, NULL}};

LUAMOD_API int luaopen_utf8(lua_State *L) {
  luaL_newlib(L, funcs);
  lua_pushlstring(L, UTF8PATT, sizeof(UTF8PATT) / sizeof(char) - 1);
  lua_setfield(L, -2, "charpattern");
  return 1;
}
