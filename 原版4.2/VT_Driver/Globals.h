#pragma once

#ifndef _GLOBALS_H
#define _GLOBALS_H


//16位md5哈希摘要
#define EPTW_WRITE             0xE943BC6264401591
#define EPTW_READWRITE         0x03F03258ADC870FC
#define EPTW_EXECUTE           0xB68C0804641E71ED

#define EPTO_VIRTUAL_BREAKPOINT    1
#define EPTO_HOOK_FUNCTION         2

#define EPT_PD_COUNT 512
#define HOST_PHYSICAL_MEMORY_PD_COUNT 512


#define VMCALL_IDENTIFIER 0xBF5587567C4C830F  //VT_Driver经16位md5摘要
#define VMCALL_IDENTIFIER2 0x66666666
#define VMM_TAG 'vtmm'
#define VMM_STACK_SIZE 0x6000

//2mb pde
#define LARGE_PAGE_SIZE 0x200000
#define GET_PFN(_VAR_) (_VAR_ >> PAGE_SHIFT)

#define MASK_GET_HIGHER_32BITS(_ARG_)(_ARG_ & 0xffffffff00000000)
#define MASK_GET_LOWER_32BITS(_ARG_)(_ARG_ & 0xffffffff)
#define MASK_GET_LOWER_16BITS(_ARG_)(_ARG_ & 0xffff)
#define MASK_GET_LOWER_8BITS(_ARG_)(_ARG_ & 0xff)
#define MASK_32BITS 0xffffffff
#define HOST_GDT_DESCRIPTOR_COUNT  4
#define HOST_IDT_DESCRIPTOR_COUNT  256


#define MASK_EPT_PML1_OFFSET(_VAR_) ((unsigned __int64)_VAR_ & 0xFFFULL)            //Offset
#define MASK_EPT_PML1_INDEX(_VAR_) ((_VAR_ & 0x1FF000ULL) >> 12)                    //PTE index
#define MASK_EPT_PML2_INDEX(_VAR_) ((_VAR_ & 0x3FE00000ULL) >> 21)                  //PDE index
#define MASK_EPT_PML3_INDEX(_VAR_) ((_VAR_ & 0x7FC0000000ULL) >> 30)                //PDPTE index
#define MASK_EPT_PML4_INDEX(_VAR_) ((_VAR_ & 0xFF8000000000ULL) >> 39)              //PML4E index
#define CPU_BASED_MONITOR_TRAP_FLAG 0x08000000


#define EPTWATCHLISTSIZE  100

#pragma pack(push, 1)
struct __pseudo_descriptor64
{
    unsigned __int16 limit;
    unsigned __int64 base_address;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct __pseudo_descriptor32
{
    unsigned __int16 limit;
    unsigned __int32 base_address;
};
#pragma pack(pop)

typedef struct
{
    unsigned __int64 cr3; //目标进程的cr3    
    unsigned __int64 VirtualAddress;
    unsigned __int64 PhysicalAddress;    
    unsigned __int64 Type; //读、写、执行
    unsigned long Options;
    unsigned __int64 LoopUserMode;
    unsigned __int64 LoopKernelMode;
    int Size;
    int inuse;
    int bpType; //断点类型 3=int3  1=debugreg
    unsigned char OriginalByte;
} EPTWatchEntry, * PEPTWatchEntry;


typedef volatile struct _criticalSection
{
    volatile int locked;
    volatile int apicid;
    volatile int lockcount;
    char* name;
    int debuglevel;
#ifdef DEBUG
    int ignorelock;
#endif
} criticalSection, * PcriticalSection;


union __rflags
{
    unsigned __int64 all;
    struct
    {
        unsigned __int64 carry_flag : 1;  //bit0
        unsigned __int64 read_as_1 : 1;  //bit1
        unsigned __int64 parity_flag : 1; //bit2
        unsigned __int64 reserved_1 : 1;  //bit3
        unsigned __int64 auxiliary_carry_flag : 1; //bit4
        unsigned __int64 reserved_2 : 1;  //bit5
        unsigned __int64 zero_flag : 1;  //bit6
        unsigned __int64 sign_flag : 1;  //bit7
        unsigned __int64 trap_flag : 1;  //bit8
        unsigned __int64 interrupt_enable_flag : 1;  //bit9
        unsigned __int64 direction_flag : 1;
        unsigned __int64 overflow_flag : 1;
        unsigned __int64 io_privilege_level : 2;
        unsigned __int64 nested_task_flag : 1;
        unsigned __int64 reserved_3 : 1;
        unsigned __int64 resume_flag : 1;
        unsigned __int64 virtual_8086_mode_flag : 1;
        unsigned __int64 alignment_check_flag : 1;
        unsigned __int64 virtual_interrupt_flag : 1;
        unsigned __int64 virtual_interrupt_pending_flag : 1;
        unsigned __int64 identification_flag : 1;
    };
};



// contains state that isn't stored in guest vmcs fields
struct alignas(16) guest_context {
    union {
        uint64_t gpr[16];

        // aliases for general-purpose registers
        struct {
            union {
                uint64_t rax;
                uint32_t eax;
                uint16_t ax;
                uint8_t  al;
            };
            union {
                uint64_t rcx;
                uint32_t ecx;
                uint16_t cx;
                uint8_t  cl;
            };
            union {
                uint64_t rdx;
                uint32_t edx;
                uint16_t dx;
                uint8_t  dl;
            };
            union {
                uint64_t rbx;
                uint32_t ebx;
                uint16_t bx;
                uint8_t  bl;
            };

            // this is where RSP would be if it wasn't saved in the vmcs
            uint64_t rsp;

            union {
                uint64_t rbp;
                uint32_t ebp;
                uint16_t bp;
                uint8_t  bpl;
            };
            union {
                uint64_t rsi;
                uint32_t esi;
                uint16_t si;
                uint8_t  sil;
            };
            union {
                uint64_t rdi;
                uint32_t edi;
                uint16_t di;
                uint8_t  dil;
            };
            union {
                uint64_t r8;
                uint32_t r8d;
                uint16_t r8w;
                uint8_t  r8b;
            };
            union {
                uint64_t r9;
                uint32_t r9d;
                uint16_t r9w;
                uint8_t  r9b;
            };
            union {
                uint64_t r10;
                uint32_t r10d;
                uint16_t r10w;
                uint8_t  r10b;
            };
            union {
                uint64_t r11;
                uint32_t r11d;
                uint16_t r11w;
                uint8_t  r11b;
            };
            union {
                uint64_t r12;
                uint32_t r12d;
                uint16_t r12w;
                uint8_t  r12b;
            };
            union {
                uint64_t r13;
                uint32_t r13d;
                uint16_t r13w;
                uint8_t  r13b;
            };
            union {
                uint64_t r14;
                uint32_t r14d;
                uint16_t r14w;
                uint8_t  r14b;
            };
            union {
                uint64_t r15;
                uint32_t r15d;
                uint16_t r15w;
                uint8_t  r15b;
            };
        };
    };

    // control registers
    uint64_t cr2;
    uint64_t cr8;

    // debug registers
    uint64_t dr0;
    uint64_t dr1;
    uint64_t dr2;
    uint64_t dr3;
    uint64_t dr6;

    // SSE registers
    M128A xmm0;
    M128A xmm1;
    M128A xmm2;
    M128A xmm3;
    M128A xmm4;
    M128A xmm5;
    M128A xmm6;
    M128A xmm7;
    M128A xmm8;
    M128A xmm9;
    M128A xmm10;
    M128A xmm11;
    M128A xmm12;
    M128A xmm13;
    M128A xmm14;
    M128A xmm15;
};

// remember to update this value in vm-exit.asm
static_assert(sizeof(guest_context) == 0x1C0);

//size: 512
typedef struct _FXSAVE64
{
    unsigned short FCW;
    unsigned short FSW;
    unsigned char FTW;
    unsigned char Reserved;
    unsigned short FOP;
    UINT64 FPU_IP;
    UINT64 FPU_DP;
    unsigned int MXCSR;
    unsigned int MXCSR_MASK;
    unsigned __int64 FP_MM0;
    unsigned __int64 FP_MM0_H;
    unsigned __int64 FP_MM1;
    unsigned __int64 FP_MM1_H;
    unsigned __int64 FP_MM2;
    unsigned __int64 FP_MM2_H;
    unsigned __int64 FP_MM3;
    unsigned __int64 FP_MM3_H;
    unsigned __int64 FP_MM4;
    unsigned __int64 FP_MM4_H;
    unsigned __int64 FP_MM5;
    unsigned __int64 FP_MM5_H;
    unsigned __int64 FP_MM6;
    unsigned __int64 FP_MM6_H;
    unsigned __int64 FP_MM7;
    unsigned __int64 FP_MM7_H;
    unsigned __int64 XMM0;
    unsigned __int64 XMM0_H;
    unsigned __int64 XMM1;
    unsigned __int64 XMM1_H;
    unsigned __int64 XMM2;
    unsigned __int64 XMM2_H;
    unsigned __int64 XMM3;
    unsigned __int64 XMM3_H;
    unsigned __int64 XMM4;
    unsigned __int64 XMM4_H;
    unsigned __int64 XMM5;
    unsigned __int64 XMM5_H;
    unsigned __int64 XMM6;
    unsigned __int64 XMM6_H;
    unsigned __int64 XMM7;
    unsigned __int64 XMM7_H;
    unsigned __int64 XMM8;
    unsigned __int64 XMM8_H;
    unsigned __int64 XMM9;
    unsigned __int64 XMM9_H;
    unsigned __int64 XMM10;
    unsigned __int64 XMM10_H;
    unsigned __int64 XMM11;
    unsigned __int64 XMM11_H;
    unsigned __int64 XMM12;
    unsigned __int64 XMM12_H;
    unsigned __int64 XMM13;
    unsigned __int64 XMM13_H;
    unsigned __int64 XMM14;
    unsigned __int64 XMM14_H;
    unsigned __int64 XMM15;
    unsigned __int64 XMM15_H;
    unsigned __int64 res1;
    unsigned __int64 res1_H;
    unsigned __int64 res2;
    unsigned __int64 res2_H;
    unsigned __int64 res3;
    unsigned __int64 res3_H;
    unsigned __int64 res4;
    unsigned __int64 res4_H;
    unsigned __int64 res5;
    unsigned __int64 res5_H;
    unsigned __int64 res6;
    unsigned __int64 res6_H;
} FXSAVE64, * PFXSAVE64;

static_assert(sizeof(FXSAVE64) == 512);


typedef struct _PageEventBasic
{
    unsigned __int64 VirtualAddress;
    unsigned __int64 PhysicalAddress;
    unsigned __int64 CR3; //in case of kernel or other process
    unsigned __int64 FSBASE;
    unsigned __int64 GSBASE;
    unsigned __int64 GSBASE_KERNEL;
    unsigned __int64 FLAGS;
    unsigned __int64 RAX;
    unsigned __int64 RBX;
    unsigned __int64 RCX;
    unsigned __int64 RDX;
    unsigned __int64 RSI;
    unsigned __int64 RDI;
    unsigned __int64 R8;
    unsigned __int64 R9;
    unsigned __int64 R10;
    unsigned __int64 R11;
    unsigned __int64 R12;
    unsigned __int64 R13;
    unsigned __int64 R14;
    unsigned __int64 R15;
    unsigned __int64 RBP;
    unsigned __int64 RSP;
    unsigned __int64 RIP;
    unsigned __int64 DR0;
    unsigned __int64 DR1;
    unsigned __int64 DR2;
    unsigned __int64 DR3;
    unsigned __int64 DR6;
    unsigned __int64 DR7;
    unsigned short CS;
    unsigned short DS;
    unsigned short ES;
    unsigned short SS;
    unsigned short FS;
    unsigned short GS;
    unsigned int Count; //number of times this block has been seen, or heartbeat when used for internal dbvm bp
} PageEventBasic, * PPageEventBasic;

typedef struct _PageEventExtended
{
    PageEventBasic basic;
    FXSAVE64 fpudata;
} PageEventExtended, * PPageEventExtended;

typedef struct _BrokenThreadEntry
{
    int inuse;  //是否已被使用
    int continueMethod; //0=no, 1=single step, 2=run  (resets to 0 after taking a step.  if 2 then inuse turns false
    int watchid; //导致中断的 watchid。如果是单步，则为 -1

    unsigned __int64 UserModeLoop; //完成某步后要去哪里
    unsigned __int64 KernelModeLoop;

    PageEventExtended state; //包含 CR3、FSBASE 和 GSBASE
} BrokenThreadEntry, * PBrokenThreadEntry;

struct __vmcs
{
    union
    {
        unsigned int all;
        struct
        {
            unsigned int revision_identifier : 31;
            unsigned int shadow_vmcs_indicator : 1;
        };
    } header;
    unsigned int abort_indicator;
    char data[0x1000 - 2 * sizeof(unsigned)];
};

struct vmx_msr_entry {
    uint32_t msr_idx;
    uint32_t _reserved;
    uint64_t msr_data;
};


struct __mtrr_range_descriptor
{
    unsigned __int64 physcial_base_address;
    unsigned __int64 physcial_end_address;
    unsigned __int8 memory_type;
    bool fixed_range;
};

union __mtrr_physmask_reg
{
    unsigned __int64 all;
    struct
    {
        unsigned __int64 reserved : 11;
        unsigned __int64 valid : 1;
        unsigned __int64 physmask : 36;
        unsigned __int64 reserved2 : 16;
    };
};

union __mtrr_physbase_reg
{
    unsigned __int64 all;
    struct
    {
        unsigned __int64 type : 8;
        unsigned __int64 reserved : 4;
        unsigned __int64 physbase : 36;
        unsigned __int64 reserved2 : 16;
    };
};

union __mtrr_cap_reg
{
    unsigned __int64 all;
    struct
    {
        unsigned __int64 range_register_number : 8; //bit 7:0  处理器中可变MTRRs寄存器的数量。
        unsigned __int64 fixed_range_support : 1; //bit 8
        unsigned __int64 reserved : 1; //bit 9
        unsigned __int64 write_combining_support : 1; //bit 10
        unsigned __int64 smrr_support : 1; //bit 11
        unsigned __int64 reserved2 : 52; //bit 63:12
    };
};

union __mtrr_def_type
{
    unsigned __int64 all;
    struct
    {
        unsigned __int64 memory_type : 3;  //bit 2:0
        unsigned __int64 reserved1 : 7;  //bit 9:3
        unsigned __int64 fixed_range_mtrr_enabled : 1;  //bit 10
        unsigned __int64 mtrr_enabled : 1;  //bit 11
        unsigned __int64 reserved2 : 52;  //bit 63:12
    };
};

union __mtrr_fixed_range_type
{
    unsigned __int64 all;
    struct
    {
        unsigned __int8 types[8];
    };
};

struct mtrr_data {
    ia32_mtrr_capabilities_register cap;
    ia32_mtrr_def_type_register def_type;

    // fixed-range MTRRs
    // 固定范围MTRRs
    struct {
        // TODO: implement
    } fixed;

    // variable-range MTRRs
    // 可变范围MTRRs寄存器组  处理器一般实现8个
    struct {
        ia32_mtrr_physbase_register base;
        ia32_mtrr_physmask_register mask;
    } variable[64];

    // number of valid variable-range MTRRs
    // 有效的可变范围 MTRRs 的数量
    size_t var_count;
};

typedef struct _MDL_MAP
{
    PMDL mdl;
    PVOID MappedAddress;
    PVOID VirtualAddress;
}MDL_MAP, * PMDL_MAP;

union __eptp
{
    unsigned __int64 all;
    struct
    {
        unsigned __int64 memory_type : 3;  //2:0
        unsigned __int64 page_walk_length : 3;  //5:3
        unsigned __int64 dirty_and_aceess_enabled : 1;  //6
        unsigned __int64 reserved_0 : 5;  //11:7
        unsigned __int64 pml4_address : 36;  //47:12
        unsigned __int64 reserved_1 : 16;  //63:48
    };
};


// See Table 28-1. 
union __ept_pml4e
{
    unsigned __int64 all;
    struct
    {
        unsigned __int64 read : 1; // bit 0
        unsigned __int64 write : 1; // bit 1
        unsigned __int64 execute : 1; // bit 2
        unsigned __int64 reserved1 : 5; // bit 7:3 (Must be Zero)
        unsigned __int64 accessed : 1; // bit 8
        unsigned __int64 ignored1 : 1; // bit 9
        unsigned __int64 execute_for_usermode : 1; // bit 10
        unsigned __int64 ignored2 : 1; // bit 11
        unsigned __int64 page_frame_number : 36; // bit (N-1):12 or Page-Frame-Number
        unsigned __int64 reserved2 : 4; // bit 51:N
        unsigned __int64 ignored3 : 12; // bit 63:52
    };
};

// See Table 28-3
union __ept_pdpte
{
    unsigned __int64 all;
    struct
    {
        unsigned __int64 read : 1; // bit 0
        unsigned __int64 write : 1; // bit 1
        unsigned __int64 execute : 1; // bit 2
        unsigned __int64 reserved1 : 5; // bit 7:3 (Must be Zero)
        unsigned __int64 accessed : 1; // bit 8
        unsigned __int64 ignored1 : 1; // bit 9
        unsigned __int64 execute_for_usermode : 1; // bit 10
        unsigned __int64 ignored2 : 1; // bit 11
        unsigned __int64 page_frame_number : 36; // bit (N-1):12 or Page-Frame-Number
        unsigned __int64 reserved2 : 4; // bit 51:N
        unsigned __int64 ignored3 : 12; // bit 63:52
    };
};

// See Table 28-5
union __ept_pde {
    unsigned __int64 all;
    struct
    {
        unsigned __int64 read : 1; // bit 0
        unsigned __int64 write : 1; // bit 1
        unsigned __int64 execute : 1; // bit 2
        unsigned __int64 reserved1 : 5; // bit 7:3 (Must be Zero)
        unsigned __int64 accessed : 1; // bit 8
        unsigned __int64 ignored1 : 1; // bit 9
        unsigned __int64 execute_for_usermode : 1; // bit 10
        unsigned __int64 ignored2 : 1; // bit 11
        unsigned __int64 page_frame_number : 36; // bit (N-1):12 or Page-Frame-Number
        unsigned __int64 reserved2 : 4; // bit 51:N
        unsigned __int64 ignored3 : 12; // bit 63:52
    }large_page;
    struct
    {
        unsigned __int64 read : 1;
        unsigned __int64 write : 1;
        unsigned __int64 execute : 1;
        unsigned __int64 memory_type : 3;
        unsigned __int64 ignore_pat : 1;
        unsigned __int64 large_page : 1;  //ps位 大页面
        unsigned __int64 accessed : 1;
        unsigned __int64 dirty : 1;
        unsigned __int64 execute_for_usermode : 1;
        unsigned __int64 reserved1 : 10;
        unsigned __int64 page_frame_number : 27;
        unsigned __int64 reserved2 : 15;
        unsigned __int64 suppressve : 1;
    }page_directory_entry;  //2mb的结构
};

// See Table 28-6																	 
union __ept_pte {
    unsigned __int64 all;
    struct
    {
        unsigned __int64 read : 1; // bit 0											 
        unsigned __int64 write : 1; // bit 1										 
        unsigned __int64 execute : 1; // bit 2
        unsigned __int64 ept_memory_type : 3; // bit 5:3 (EPT Memory type)
        unsigned __int64 ignore_pat : 1; // bit 6
        unsigned __int64 ignored1 : 1; // bit 7
        unsigned __int64 accessed_flag : 1; // bit 8	
        unsigned __int64 dirty_flag : 1; // bit 9
        unsigned __int64 execute_for_usermode : 1; // bit 10
        unsigned __int64 ignored2 : 1; // bit 11
        unsigned __int64 page_frame_number : 36; // bit (N-1):12 or Page-Frame-Number
        unsigned __int64 reserved : 4; // bit 51:N
        unsigned __int64 ignored3 : 11; // bit 62:52
        unsigned __int64 suppress_ve : 1; // bit 63
    };
};

//动态分割
struct __ept_dynamic_split
{
    DECLSPEC_ALIGN(PAGE_SIZE) __ept_pte pml1[512];  //页表pt

    __ept_pde* entry;

    LIST_ENTRY dynamic_split_list;
};

//数组最大为512即2^9次方
struct __vmm_ept_page_table
{
    DECLSPEC_ALIGN(PAGE_SIZE) __ept_pml4e pml4[512];

    DECLSPEC_ALIGN(PAGE_SIZE) __ept_pdpte pml3[512];

    DECLSPEC_ALIGN(PAGE_SIZE) __ept_pde pml2[EPT_PD_COUNT][512];
};

struct __ept_hooked_function_info
{
    //
    // Linked list entires for each function hook.
    //
    LIST_ENTRY hooked_function_list;

    //
    // Pointer to page with our hooked functions
    // 伪造页内容
    // 指向4KB物理页
    //
    unsigned __int8* fake_page_contents;

    //
    // Size of hook
    //
    unsigned __int64 hook_size;

    //欲设置断点的物理地址
    unsigned __int64 breakpoint_address;

    //用途
    int Options;

    unsigned __int64 LoopUserMode;

    unsigned __int64 LoopKernelMode;

    BrokenThreadEntry CurrentBrokenThread;

    //
    // Virtual address of function
    // 函数的虚拟地址
    //
    void* virtual_address;

    //事件的处理函数
    void* handler_function;

    //
    // Address to first trampoline used to call original function
    // 用于调用原始函数的
    //
    unsigned __int8* first_trampoline_address;

    //
    // Address of code cave which is used to jmp to our hooked function
    //
    void* second_trampoline_address;
};

//hook页 描述符
struct __ept_hooked_page_info
{
    //
    // Page with our hooked functions
    // 伪造页的内容存放hook代码
    // 4KB物理页
    //
    DECLSPEC_ALIGN(PAGE_SIZE) unsigned __int8 fake_page_contents[PAGE_SIZE];

    //
    // Linked list entires for each page hook.
    // 记录被hook的页面
    //
    LIST_ENTRY hooked_page_list;

    //
    // Linked list entries for each function hook
    // 记录被hook的函数
    //
    LIST_ENTRY hooked_functions_list;

    //
    // The base address of the page. Used to find this structure in the list of page hooks
    // 源函数的页帧号
    //
    unsigned __int64 pfn_of_hooked_page;

    //
    // The base address of the page with fake contents. Used to swap page with fake contents
    // 伪造页的页帧号
    //
    unsigned __int64 pfn_of_fake_page_contents;

    //
    // The page entry in the page tables that this page is targetting.
    //
    __ept_pte* entry_address;

    //
    // The original page entry
    // 原页
    // 
    __ept_pte original_entry;

    //
    // The changed page entry
    // 伪造页
    //
    __ept_pte changed_entry;

    int Options;

    int ID;

    bool isBp;

    bool isInt3;
};

union __ept_violation
{
    unsigned __int64 all;
    struct
    {
        /**
         * [Bit 0] Set if the access causing the EPT violation was a data read.
         */
        unsigned __int64 read_access : 1;

        /**
         * [Bit 1] Set if the access causing the EPT violation was a data write.
         */
        unsigned __int64 write_access : 1;

        /**
         * [Bit 2] Set if the access causing the EPT violation was an instruction fetch.
         */
        unsigned __int64 execute_access : 1;

        /**
         * [Bit 3] The logical-AND of bit 0 in the EPT paging-structure entries used to translate the guest-physical address of the
         * access causing the EPT violation (indicates whether the guest-physical address was readable).
         */
        unsigned __int64 ept_readable : 1;

        /**
         * [Bit 4] The logical-AND of bit 1 in the EPT paging-structure entries used to translate the guest-physical address of the
         * access causing the EPT violation (indicates whether the guest-physical address was writeable).
         */
        unsigned __int64 ept_writeable : 1;

        /**
         * [Bit 5] The logical-AND of bit 2 in the EPT paging-structure entries used to translate the guest-physical address of the
         * access causing the EPT violation.
         * If the "mode-based execute control for EPT" VM-execution control is 0, this indicates whether the guest-physical address
         * was executable. If that control is 1, this indicates whether the guest-physical address was executable for
         * supervisor-mode linear addresses.
         */
        unsigned __int64 ept_executable : 1;

        /**
         * [Bit 6] If the "mode-based execute control" VM-execution control is 0, the value of this bit is undefined. If that
         * control is 1, this bit is the logical-AND of bit 10 in the EPT paging-structures entries used to translate the
         * guest-physical address of the access causing the EPT violation. In this case, it indicates whether the guest-physical
         * address was executable for user-mode linear addresses.
         */
        unsigned __int64 ept_executable_for_usermode : 1;

        /**
         * [Bit 7] Set if the guest linear-address field is valid. The guest linear-address field is valid for all EPT violations
         * except those resulting from an attempt to load the guest PDPTEs as part of the execution of the MOV CR instruction.
         */
        unsigned __int64 valid_guest_linear_address : 1;

        /**
         * [Bit 8] If bit 7 is 1:
         * - Set if the access causing the EPT violation is to a guest-physical address that is the translation of a linear
         * address.
         * - Clear if the access causing the EPT violation is to a paging-structure entry as part of a page walk or the update of
         * an accessed or dirty bit.
         * Reserved if bit 7 is 0 (cleared to 0).
         */
        unsigned __int64 caused_by_translation : 1;

        /**
         * [Bit 9] This bit is 0 if the linear address is a supervisor-mode linear address and 1 if it is a user-mode linear
         * address. Otherwise, this bit is undefined.
         *
         * @remarks If bit 7 is 1, bit 8 is 1, and the processor supports advanced VM-exit information for EPT violations. (If
         *          CR0.PG = 0, the translation of every linear address is a user-mode linear address and thus this bit will be 1.)
         */
        unsigned __int64 usermode_linear_address : 1;

        /**
         * [Bit 10] This bit is 0 if paging translates the linear address to a read-only page and 1 if it translates to a
         * read/write page. Otherwise, this bit is undefined
         *
         * @remarks If bit 7 is 1, bit 8 is 1, and the processor supports advanced VM-exit information for EPT violations. (If
         *          CR0.PG = 0, every linear address is read/write and thus this bit will be 1.)
         */
        unsigned __int64 readable_writable_page : 1;

        /**
         * [Bit 11] This bit is 0 if paging translates the linear address to an executable page and 1 if it translates to an
         * execute-disable page. Otherwise, this bit is undefined.
         *
         * @remarks If bit 7 is 1, bit 8 is 1, and the processor supports advanced VM-exit information for EPT violations. (If
         *          CR0.PG = 0, CR4.PAE = 0, or MSR_IA32_EFER.NXE = 0, every linear address is executable and thus this bit will be 0.)
         */
        unsigned __int64 execute_disable_page : 1;

        /**
         * [Bit 12] NMI unblocking due to IRET.
         */
        unsigned __int64 nmi_unblocking : 1;
        unsigned __int64 reserved1 : 51;
    };
};

struct __ept_state
{
    LIST_ENTRY hooked_page_list;  //hook页列表   被hook的页面会被加入到该列表
    __eptp* ept_pointer;
    __vmm_ept_page_table* ept_page_table;  //ept页表
    __ept_hooked_page_info* page_to_change;
};

struct __vcpu
{
    // host task state segment
    alignas(0x1000) task_state_segment_64 host_tss;

    // host global descriptor table
    alignas(0x1000) segment_descriptor_32 host_gdt[HOST_GDT_DESCRIPTOR_COUNT];

    // host interrupt descriptor table
    alignas(0x1000) segment_descriptor_interrupt_gate_64 host_idt[HOST_IDT_DESCRIPTOR_COUNT];

    alignas(0x1000) vmcs vmcs;

    alignas(0x1000) vmxon vmxon;

    alignas(0x1000) uint8_t host_stack[VMM_STACK_SIZE];

    alignas(0x1000) vmx_msr_bitmap msr_bitmap;

    // the number of NMIs that need to be delivered
    uint32_t volatile queued_nmis;

    // current preemption timer
    uint64_t preemption_timer;

    bool hide_vm_exit_overhead;
    uint64_t tsc_offset;

    // the overhead caused by world-transitions
    uint64_t vm_exit_tsc_overhead;
    uint64_t vm_exit_mperf_overhead;
    uint64_t vm_exit_ref_tsc_overhead;

    cr3 target_cr3;

    //void* vmm_stack;

    // vm-exit MSR store area
    struct alignas(0x10) {
        vmx_msr_entry tsc;
        vmx_msr_entry perf_global_ctrl;
        vmx_msr_entry aperf;
        vmx_msr_entry mperf;
    } msr_exit_store;

    // vm-entry MSR load area
    struct alignas(0x10) {
        vmx_msr_entry aperf;
        vmx_msr_entry mperf;
    } msr_entry_load;

    struct __vmexit_info
    {
        guest_context* guest_registers;
        PFXSAVE64 fxsave;

        unsigned __int64 guest_rip;

        __rflags guest_rflags;

        unsigned __int64 instruction_length;

        unsigned __int64 reason;  //vmexit原因

        unsigned __int64 qualification;

        unsigned __int64 instruction_information;

        unsigned __int64 instruction_error;  //记录VMfailValid失败原因

    }vmexit_info;

    struct __vcpu_status
    {
        unsigned __int64 vmx_on;
        unsigned __int64 vmm_launched;  //vm虚拟机的运行状态
    }vcpu_status;

    struct __vmx_off_state
    {
        unsigned __int64  vmx_off_executed;  //关闭虚拟化
        unsigned __int64  guest_rip;
        unsigned __int64  guest_rsp;
    }vmx_off_state;

    struct __vcpu_bitmaps
    {
        unsigned __int8* io_bitmap_a;
        unsigned __int64 io_bitmap_a_physical;

        unsigned __int8* io_bitmap_b;
        unsigned __int64 io_bitmap_b_physical;
    }vcpu_bitmaps;

    __ept_state* ept_state;

    CLIENT_ID Cid; //记录触发监视断点的guest的线程id
    unsigned __int64 breakpoint_detected; //记录触发的地址
};

struct __mtrr_info
{
    __mtrr_range_descriptor memory_range[100];
    unsigned __int32 enabled_memory_ranges;
    unsigned __int8 default_memory_type;
    mtrr_data mtrrs;
};

struct __vmm_context
{
    __vcpu* vcpu; //虚拟处理器
    pool_manager::__pool_manager* pool_manager;
    __mtrr_info mtrr_info;

    unsigned __int32 processor_count;  //处理器数量
    unsigned __int32 highest_basic_leaf;
    bool hv_presence;  //标识hy对象已经创建    
};

// 4级页映射的虚拟地址
union pml4_virtual_address {
    void const* address;
    struct {
        uint64_t offset : 12;
        uint64_t pt_idx : 9;
        uint64_t pd_idx : 9;
        uint64_t pdpt_idx : 9;
        uint64_t pml4_idx : 9;
    };
};

namespace hv
{

    struct trap_frame {
        // TODO: SSE registers...

        // general-purpose registers
        union {
            uint64_t rax;
            uint32_t eax;
            uint16_t ax;
            uint8_t  al;
        };
        union {
            uint64_t rcx;
            uint32_t ecx;
            uint16_t cx;
            uint8_t  cl;
        };
        union {
            uint64_t rdx;
            uint32_t edx;
            uint16_t dx;
            uint8_t  dl;
        };
        union {
            uint64_t rbx;
            uint32_t ebx;
            uint16_t bx;
            uint8_t  bl;
        };
        union {
            uint64_t rbp;
            uint32_t ebp;
            uint16_t bp;
            uint8_t  bpl;
        };
        union {
            uint64_t rsi;
            uint32_t esi;
            uint16_t si;
            uint8_t  sil;
        };
        union {
            uint64_t rdi;
            uint32_t edi;
            uint16_t di;
            uint8_t  dil;
        };
        union {
            uint64_t r8;
            uint32_t r8d;
            uint16_t r8w;
            uint8_t  r8b;
        };
        union {
            uint64_t r9;
            uint32_t r9d;
            uint16_t r9w;
            uint8_t  r9b;
        };
        union {
            uint64_t r10;
            uint32_t r10d;
            uint16_t r10w;
            uint8_t  r10b;
        };
        union {
            uint64_t r11;
            uint32_t r11d;
            uint16_t r11w;
            uint8_t  r11b;
        };
        union {
            uint64_t r12;
            uint32_t r12d;
            uint16_t r12w;
            uint8_t  r12b;
        };
        union {
            uint64_t r13;
            uint32_t r13d;
            uint16_t r13w;
            uint8_t  r13b;
        };
        union {
            uint64_t r14;
            uint32_t r14d;
            uint16_t r14w;
            uint8_t  r14b;
        };
        union {
            uint64_t r15;
            uint32_t r15d;
            uint16_t r15w;
            uint8_t  r15b;
        };

        // interrupt vector
        uint8_t vector;

        // _MACHINE_FRAME
        uint64_t error;
        uint64_t rip;
        uint64_t cs;
        uint64_t rflags;
        uint64_t rsp;
        uint64_t ss;
    };

    // remember to update this value in interrupt-handlers.asm
    static_assert(sizeof(trap_frame) == (0x78 + 0x38));


    // structure that gets filled out when a host exception occurs
    struct host_exception_info {
        // whether an exception occurred or not
        // 是否发生异常
        bool exception_occurred;

        // interrupt vector
        uint64_t vector;

        // error code
        uint64_t error;
    };

    struct host_page_tables {
        // array of PML4 entries that point to a PDPT
        alignas(0x1000) pml4e_64 pml4[512];

        // PDPT for mapping physical memory
        alignas(0x1000) pdpte_64 phys_pdpt[512];

        // PDs for mapping physical memory
        alignas(0x1000) pde_2mb_64 phys_pds[HOST_PHYSICAL_MEMORY_PD_COUNT][512];
    };

    struct hypervisor {
        // host page tables that are shared between vcpus
        alignas(0x1000) host_page_tables host_page_tables;

        // pointer to the System process
        uint8_t* system_eprocess;

        // kernel CR3 value of the System process
        // System进程的内核 CR3 值
        cr3 system_cr3;

        // windows specific offsets D:
        uint64_t kprocess_directory_table_base_offset;
        uint64_t eprocess_unique_process_id_offset;
        uint64_t eprocess_image_file_name;
        uint64_t kpcr_pcrb_offset;
        uint64_t kprcb_current_thread_offset;
        uint64_t kthread_apc_state_offset;
        uint64_t kapc_state_process_offset;
    };

    // global instance of the hypervisor
    extern hypervisor ghv;

	// selectors for the host GDT
	extern segment_selector host_cs_selector;
	extern segment_selector host_tr_selector;


	EXTERN_C
	{
		void interrupt_handler_0();
		void interrupt_handler_1();
		void interrupt_handler_2();
		void interrupt_handler_3();
		void interrupt_handler_4();
		void interrupt_handler_5();
		void interrupt_handler_6();
		void interrupt_handler_7();
		void interrupt_handler_8();
		void interrupt_handler_10();
		void interrupt_handler_11();
		void interrupt_handler_12();
		void interrupt_handler_13();
		void interrupt_handler_14();
		void interrupt_handler_16();
		void interrupt_handler_17();
		void interrupt_handler_18();
		void interrupt_handler_19();
		void interrupt_handler_20();
		void interrupt_handler_30();
	}

    EXTERN_C
    {
        // defined in vm-launch.asm
        bool vm_launch();

        // defined in vm-exit.asm
        void vm_exit();

        void handle_host_interrupt(trap_frame* const frame);

        // 具有异常处理的xsetbv
        void xsetbv_safe(host_exception_info& e, uint32_t idx, uint64_t value);

        // 具有异常处理的 rdmsr
        uint64_t rdmsr_safe(host_exception_info& e, uint32_t msr);

        // 具有异常处理的wrmsr
        void wrmsr_safe(host_exception_info& e, uint32_t msr, uint64_t value);        

        // 具有异常处理的 memcpy
        void memcpy_safe(host_exception_info& e, void* dst, void const* src, size_t size);
    }

    ia32_vmx_procbased_ctls_register read_ctrl_proc_based();
    void write_ctrl_proc_based(ia32_vmx_procbased_ctls_register const value);    

    void prepare_host_idt(segment_descriptor_interrupt_gate_64* const idt);

    void prepare_host_gdt(
        segment_descriptor_32* const gdt,
        task_state_segment_64 const* const tss);

    void prepare_external_structures(__vcpu* const vcpu);
    void prepare_host_page_tables();

    void inject_nmi();

    cr0 read_effective_guest_cr0();
    cr4 read_effective_guest_cr4();
    bool load_vmcs_pointer(vmcs& vmcs_region);

    //读取guest通用寄存器
    uint64_t read_guest_gpr(guest_context const* const ctx, uint64_t const gpr_idx);

    //写guest通用寄存器
    void write_guest_gpr(guest_context* const ctx, uint64_t const gpr_idx, uint64_t const value);

    vmx_interruptibility_state read_interruptibility_state();
    void write_interruptibility_state(vmx_interruptibility_state const value);
    bool enter_vmx_operation(vmxon& vmxon_region);

    // read MTRR data into a single structure
    mtrr_data read_mtrr_data();

    // calculate the MTRR memory type for the given physical memory range
    uint8_t calc_mtrr_mem_type(mtrr_data const& mtrrs, uint64_t address, uint64_t size);

    // set the memory type in every EPT paging structure to the specified value
    // 将每个 EPT 分页结构中的内存类型设置为指定值将每个 EPT 分页结构中的内存类型设置为指定值
    void set_ept_memory_type(__ept_state& ept_state, uint8_t const memory_type);

    void update_ept_memory_type(__ept_state& ept_state);

    //将guest虚拟地址转为guest物理地址
    uint64_t gva2gpa(cr3 guest_cr3, void* gva, size_t* offset_to_next_page = nullptr);

    //将GVA转换为GPA
    uint64_t get_physical_address(unsigned __int64 guest_cr3, _In_ PVOID BaseAddress);

    size_t read_guest_virtual_memory(cr3 const guest_cr3,
        void* const gva, void* const hva, size_t const size);

    // attempt to read the memory at the specified guest virtual address from root-mode
    // 读取guest中当前进程的虚拟内存
    size_t read_guest_virtual_memory(void* const gva, void* const hva, size_t const size);

    size_t write_guest_virtual_memory(cr3 const guest_cr3,
        void* const gva, void* const hva, size_t const size);

    // 写入guest中当前进程的虚拟内存
    size_t write_guest_virtual_memory(void* const gva, void* const hva, size_t const size);

    // get the ETHREAD of the current guest
    // 获取guest里的当前线程对象
    size_t current_guest_ethread();

    //返回触发的断点给guest调试器
    bool get_breakpoint_detected(__vcpu* vcpu, PBREAKPOINT_DETECTED vmcallinfo);

    //向guest注入#DB事件
    void inject_single_step(__vcpu* vcpu);

    //获取空闲的id
    int getIdleWatchID();
    void InitGlobalVariables();
}


typedef PEPROCESS(__fastcall* PFN_PSGETNEXTPROCESS)(_In_ PEPROCESS Process);


extern PFN_PSGETNEXTPROCESS PsGetNextProcess;


extern __pseudo_descriptor64 g_gdtr;
extern __pseudo_descriptor64 g_idtr;
extern unsigned __int64 g_guest_cr0;
extern unsigned __int64 g_guest_cr3;
extern unsigned __int64 g_guest_cr4;
extern __vmm_context g_vmm_context;
extern uint16_t guest_vpid;
extern volatile long eptWatchList_lock;
extern EPTWatchEntry eptWatchList[EPTWATCHLISTSIZE];

namespace ethread_offset
{
    extern size_t Tcb;
    extern size_t CrossThreadFlags;
    extern size_t Cid;
    extern size_t RundownProtect;
}

#endif // !_GLOBALS_H
