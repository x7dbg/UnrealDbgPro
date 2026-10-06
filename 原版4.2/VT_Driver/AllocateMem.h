#pragma once

#ifndef _ALLOCATEMEM_H
#define _ALLOCATEMEM_H

template <typename T>
inline T allocate_pool(unsigned __int64 size)
{
    return (T)ExAllocatePoolWithTag(NonPagedPool, size, VMM_TAG);
}

template <typename T>
inline T* allocate_pool()
{
    return (T*)ExAllocatePoolWithTag(NonPagedPool, sizeof(T), VMM_TAG);
}

template <typename T>
inline T* allocate_contignous_memory()
{
    PHYSICAL_ADDRESS a;
    a.QuadPart = 0ULL - 1;
    return (T*)MmAllocateContiguousMemory(sizeof(T), a);
}

template <typename T>
inline T allocate_contignous_memory(unsigned __int64 size)
{
    PHYSICAL_ADDRESS a;
    a.QuadPart = 0ULL - 1;
    return (T)MmAllocateContiguousMemory(size, a);
}

inline void free_pool(void* pool_address)
{
    ExFreePoolWithTag(pool_address, VMM_TAG);
}

//释放连续内存
inline void free_contignous_memory(void* memory_address)
{
    MmFreeContiguousMemory(memory_address);
}

#endif // !_ALLOCATEMEM_H
