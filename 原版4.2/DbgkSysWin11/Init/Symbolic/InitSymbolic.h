#pragma once

#ifndef _INIT_SYMBOLIC_H
#define _INIT_SYMBOLIC_H

VOID InitSymbolsTable(IN PUSER_DATA userData, IN PIRP pIrp);

BOOLEAN InitNtoskrnlSymbolsTable();
BOOLEAN InitWin32kbaseSymbolsTable();
BOOLEAN InitWin32kfullSymbolsTable();

//将内核结构的偏移发送给vt host
bool DispatchOffsetToHost();

//Dump对象偏移和函数指针
void DumpOffsetAndFuncPtr();

void CheckFunctionPointers();

#endif // !_INIT_SYMBOLIC_H
