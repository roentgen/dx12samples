/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#include <windows.h>
#include <stdio.h>
#include "dbgutils.hpp"
#include <string>
#include <stdarg.h>
#include <Strsafe.h>

void dbg_print1_(int flag, const wchar_t* str)
{
	OutputDebugString(str);
}

void dbg_print_(int flag, const wchar_t* fmt, ...)
{
	va_list va;
	wchar_t buf[1024];
	va_start(va, fmt);
	StringCbVPrintfW(buf, 1023, fmt, va);
	va_end(va);
	OutputDebugString(buf);
}

void dbg_set_name_(ID3D12Object* o, const wchar_t* name)
{
	o->SetName(name);
}

void dbg_set_name_(ID3D12Object* o, int idx, const wchar_t* name)
{
	wchar_t tmp[64];
	if (swprintf_s(tmp, L"%s[%u]", name, idx) > 0)
		o->SetName(tmp);
}
