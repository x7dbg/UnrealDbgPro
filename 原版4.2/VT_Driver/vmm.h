#pragma once

#ifndef _VMM_H
#define _VMM_H

/// <summary>
/// Initialize and launch vmm
/// </summary>
/// <returns> status </returns>
bool vmm_init();

/// <summary>
/// Deallocate all structures
/// </summary>
void free_vmm_context();

//创建host页表
void create_host_page_tables();

//分配vcpu结构内存
bool init_vcpu(__vcpu* vcpu);

#endif // !_VMM_H
