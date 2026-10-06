#include "Driver.h"
#include "invalid_ept.h"
#include "AsmCallset.h"

//刷新全部的逻辑处理器
void invept_all_contexts_func()
{
	__invept_descriptor descriptor = { 0 };
	__invept(invept_all_context, &descriptor);
}

//刷新指定的逻辑处理器的eptp
void invept_single_context_func(unsigned __int64 ept_pointer)
{
	__invept_descriptor descriptor = { 0 };
	descriptor.ept_pointer = ept_pointer;
	descriptor.reserved = 0;
	__invept(invept_single_context, &descriptor);
}