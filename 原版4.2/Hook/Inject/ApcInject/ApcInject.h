#pragma once

#ifndef _APC_INJECT_H
#define _APC_INJECT_H

void ApcCallRemoteFunc(HANDLE hProcess);

//通过驱动注入apc线程到目标进程
BOOL _ApcCallRemoteFunc(HANDLE hProcess);

#endif // !_APC_INJECT_H
