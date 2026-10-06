#pragma once


#ifndef _SERIAL_PORT_H
#define _SERIAL_PORT_H

#define PRINTF_LONG_SUPPORT
#define PRINTF_LONG_LONG_SUPPORT

//#define _SERIAL_DEBUG_LOG

void Log(const char* format, ...);


#ifdef _SERIAL_DEBUG_LOG
#define sLog(format, ...) \
	Log("[%s:%d]" format , __func__, __LINE__, __VA_ARGS__)
#else
#define sLog(format, ...)
#endif // _SERIAL_DEBUG_LOG

#endif // !_SERIAL_PORT_H
