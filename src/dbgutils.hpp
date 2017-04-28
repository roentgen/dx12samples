/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#if !defined(DBG_UTILS_HPP__)
#define DBG_UTILS_HPP__

#include <d3d12.h>

#define LEVEL_FATAL (0)
#define LEVEL_WARN  (2)
#define LEVEL_INFO  (4)
#define LEVEL_TRACE (6)

void dbg_print1_(int flag, const wchar_t* str);
void dbg_print_(int flag, const wchar_t* fmt, ...);
void dbg_set_name_(ID3D12Object*, const wchar_t*);
void dbg_set_name_(ID3D12Object*, int, const wchar_t*);


#define DBG(fmt, ...) dbg_print_(LEVEL_TRACE, TEXT(fmt), __VA_ARGS__);
#define INF(fmt, ...) dbg_print_(LEVEL_INFO,  TEXT(fmt), __VA_ARGS__);
#define WRN(fmt, ...) dbg_print_(LEVEL_WARN,  TEXT(fmt), __VA_ARGS__);
#define ABT(fmt, ...) dbg_print_(LEVEL_FATAL, TEXT(fmt), __VA_ARGS__);

#define ABTMSG(msg) dbg_print1_(LEVEL_FATAL, TEXT(msg));
#define DBGMSG(msg) dbg_print1_(LEVEL_TRACE, TEXT(msg));

#define NAME_OBJ(o) dbg_set_name_(o.Get(), L#o)
#define NAME_OBJ_WITH_INDEXED(o, n) dbg_set_name_(o[(n)].Get(), (n), L#o)
#define NAME_OBJ2(o, name) dbg_set_name_(o.Get(), name)

#endif
