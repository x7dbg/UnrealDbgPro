#include <ntifs.h>
#include "StringHandler.h"

//获取字符串长度   返回字符个数
SIZE_T GetStrLen(WCHAR* _Str)
{
    SIZE_T str_len = 0;
    if (_Str == NULL)
    {
        return 0;
    }

    for (;;)
    {
        if (*_Str == _T('\0'))
        {
            break;
        }
        _Str++;
        str_len++;
    }
    return str_len;
}

//字符串拷贝
VOID StrCopy(WCHAR* _Dest, WCHAR* _Source)
{
    RtlCopyMemory(_Dest, _Source, GetStrLen(_Source) * sizeof(WCHAR));
}

VOID StrCopy2(WCHAR* _Dest, PUNICODE_STRING _Source)
{
    RtlCopyMemory(_Dest, _Source->Buffer, _Source->Length);
}

//分配池内存
PVOID MemAllocate(IN SIZE_T Size, IN BOOLEAN Paged, IN ULONG Tag)
{
    PVOID pAddr = ExAllocatePoolWithTag(Paged ? PagedPool : NonPagedPool,
        Size,
        Tag);
    RtlZeroMemory(pAddr, Size);
    return pAddr;
}

//释放池内存
VOID FreeMemAllocate(IN PVOID Ptr, IN ULONG Tag)
{
    ExFreePoolWithTag(Ptr, Tag);
}

//判断字符串是否为空
BOOLEAN StrIsValid(WCHAR* _Str)
{
    if (*_Str == _T('\0'))
        return FALSE;
    else
        return TRUE;
}

//判断UNICODE字符串是否为空
BOOLEAN StrIsValid2(UNICODE_STRING filePath)
{
    if (filePath.Length == 0)
        return FALSE;
    else
        return TRUE;
}

//获取文件名称
PVOID GetModuleFileName(PUNICODE_STRING filePath)
{
    if (StrIsValid2(*filePath))
    {
        int i = filePath->Length / sizeof(WCHAR);

        while (filePath->Buffer[i] != L'\\')
        {
            i--;
        }

        return &filePath->Buffer[i + 1];
    }
    else
    {
        return NULL;
    }
}

//获取进程名
NTSTATUS GetProcessName(PEPROCESS Process, PUNICODE_STRING fileName)
{
    NTSTATUS Status;
    PUNICODE_STRING ImageFileName;
    Status = SeLocateProcessImageName(Process, &ImageFileName);
    if (NT_SUCCESS(Status))
    {
        WCHAR* SubStr = (WCHAR*)GetModuleFileName(ImageFileName);
        if (StrIsValid(SubStr))
        {
            if (fileName)
            {
                RtlInitUnicodeString(fileName, SubStr);
            }            
        }
        if (ImageFileName)
        {
            ExFreePool(ImageFileName);
        }
    }
    return Status;
}

//搜索子字符串
BOOLEAN RtlUnicodeStringContains(PUNICODE_STRING Str, PUNICODE_STRING SubStr, BOOLEAN CaseInsensitive)
{
    if (Str == NULL || SubStr == NULL || Str->Length < SubStr->Length)
        return FALSE;

    CONST USHORT NumCharsDiff = (Str->Length - SubStr->Length) / sizeof(WCHAR);
    UNICODE_STRING Slice = *Str;
    Slice.Length = SubStr->Length;

    for (USHORT i = 0; i <= NumCharsDiff; ++i, ++Slice.Buffer, Slice.MaximumLength -= sizeof(WCHAR))
    {
        if (RtlEqualUnicodeString(&Slice, SubStr, CaseInsensitive))
        {
            return TRUE;
        }
    }
    return FALSE;
}

//字符串分割
WCHAR* SplitString(WCHAR* SourceString, WCHAR* DestString, WCHAR Delimiter)
{
    //WCHAR Buffer[1024] = { 0 };
    WCHAR* CurrentChar = SourceString;

    //StrCopy(Buffer, SourceString);

    if (SourceString == NULL || DestString == NULL)
        return NULL;

    for (ULONG i = 0; i < GetStrLen(SourceString); i++)
    {
        // 找到分隔符
        if (*CurrentChar == Delimiter)
        {
            *CurrentChar = _T('\0');
            StrCopy(DestString, SourceString);
            break;
        }

        // 移动到下一个字符
        CurrentChar++;
    }
    ++CurrentChar;


    if (*CurrentChar == _T('\0'))
        return NULL;

    return CurrentChar;
}
