#pragma once

#ifndef _ALLOCATEMEM_H
#define _ALLOCATEMEM_H

#define NPPM_TAG 'nppm'

template <typename T>
inline T allocate_pool(unsigned __int64 size)
{
    PVOID pAddr = ExAllocatePoolWithTag(NonPagedPool, size, NPPM_TAG);
    RtlSecureZeroMemory(pAddr, size);
    return (T)pAddr;
}

template <typename T>
inline T* allocate_pool()
{
    PVOID pAddr = ExAllocatePoolWithTag(NonPagedPool, sizeof(T), NPPM_TAG);
    RtlSecureZeroMemory(pAddr, sizeof(T));
    return (T*)pAddr;
}

template <typename T>
inline T* allocate_contignous_memory()
{
    PHYSICAL_ADDRESS a;
    a.QuadPart = 0ULL - 1;
    PVOID pAddr = MmAllocateContiguousMemory(sizeof(T), a);
    RtlSecureZeroMemory(pAddr, sizeof(T));
    return (T*)pAddr;
}

template <typename T>
inline T allocate_contignous_memory(unsigned __int64 size)
{
    PHYSICAL_ADDRESS a;
    a.QuadPart = 0ULL - 1;
    PVOID pAddr = (T)MmAllocateContiguousMemory(size, a);
    RtlSecureZeroMemory(pAddr, size);
    return (T)pAddr;
}

inline void free_pool(void* pool_address)
{
    if (pool_address)
    {
        ExFreePoolWithTag(pool_address, NPPM_TAG);
    }    
}

//释放连续内存
inline void free_contignous_memory(void* memory_address)
{
    MmFreeContiguousMemory(memory_address);
}

#endif // !_ALLOCATEMEM_H
