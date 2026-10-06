#include <ntifs.h>
#include <ntstrsafe.h>
#include "Utils/Log.h"
#include "LogFile.h"

#define MAX_BUFFER_SIZE 256

namespace LogFile
{
    bool boLogInit;
    //FAST_MUTEX Mutex;          //互斥锁
    // Kernel-mode file logging used to write a fixed, globally writable path.
    // It is intentionally retired: diagnostics are emitted through the
    // versioned UTF-8 debugger protocol instead of doing file I/O from a
    // privileged driver.

    NTSTATUS CreateLogsDirectory(PWCHAR path)
    {
        UNREFERENCED_PARAMETER(path);
        return STATUS_NOT_SUPPORTED;
    }

    NTSTATUS WriteLogToXmlFileW(PWCHAR logMessage)
    {
        UNREFERENCED_PARAMETER(logMessage);
        return STATUS_NOT_SUPPORTED;
    }

    NTSTATUS WriteLogToXmlFileA(PCHAR logMessage)
    {
        UNREFERENCED_PARAMETER(logMessage);
        return STATUS_NOT_SUPPORTED;
    }

    NTSTATUS LogDriverMessageW(PWCHAR message)
    {
        UNREFERENCED_PARAMETER(message);
        return STATUS_NOT_SUPPORTED;
    }

    NTSTATUS LogDriverMessageA(PCHAR message)
    {
        UNREFERENCED_PARAMETER(message);
        return STATUS_NOT_SUPPORTED;
    }

    NTSTATUS SetLogFilePath(PWCHAR path)
    {
        UNREFERENCED_PARAMETER(path);
        return STATUS_NOT_SUPPORTED;
    }

    NTSTATUS ConvertUnicodeToAnsi(PCWSTR unicodeString, PCHAR* ansiString)
    {
        UNICODE_STRING unicodeStr;
        ANSI_STRING ansiStr;

        RtlInitUnicodeString(&unicodeStr, unicodeString);
        NTSTATUS status = RtlUnicodeStringToAnsiString(&ansiStr, &unicodeStr, TRUE);

        if (NT_SUCCESS(status))
        {
            *ansiString = ansiStr.Buffer;
        }

        return status;
    }

    NTSTATUS ReadIniValue(_In_ PCWSTR filePath, _In_ PCWSTR sectionName, _In_ PCWSTR keyName, _Out_ PWSTR value, _In_ ULONG valueSize)
    {
        NTSTATUS status = STATUS_UNSUCCESSFUL;

        //// 打开INI文件
        //HANDLE fileHandle;
        //IO_STATUS_BLOCK ioStatusBlock;
        //UNICODE_STRING unicodeFilePath;
        //RtlInitUnicodeString(&unicodeFilePath, filePath);
        //OBJECT_ATTRIBUTES objectAttributes;
        //InitializeObjectAttributes(&objectAttributes, &unicodeFilePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
        //status = ZwCreateFile(&fileHandle, GENERIC_READ, &objectAttributes, &ioStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
        //if (!NT_SUCCESS(status))
        //{
        //    KdPrint(("Failed to open file. Status: 0x%X\n", status));
        //    return status;
        //}

        //// 读取INI文件内容
        //CHAR buffer[512];
        //ULONG bytesRead;
        //status = ZwReadFile(fileHandle, NULL, NULL, NULL, &ioStatusBlock, buffer, sizeof(buffer) - sizeof(CHAR), NULL, NULL);
        //if (!NT_SUCCESS(status))
        //{
        //    KdPrint(("Failed to read file. Status: 0x%X\n", status));
        //    ZwClose(fileHandle);
        //    return status;
        //}

        //// 关闭INI文件
        //ZwClose(fileHandle);

        //// 解析INI文件内容
        //PCHAR section = NULL;
        //PCHAR key = NULL;
        //PCHAR valueStart = NULL;
        //PCHAR valueEnd = NULL;
        //BOOLEAN inTargetSection = FALSE;
        //PCHAR token = strtok(buffer, "\r\n");
        //while (token != NULL)
        //{
        //    if (token[0] == '[' && token[strlen(token) - 1] == ']')
        //    {
        //        // 判断是否进入目标节
        //        token[strlen(token) - 1] = '\0';

        //        PCHAR ansiString = NULL;
        //        status = ConvertUnicodeToAnsi(sectionName, &ansiString);
        //        if (!NT_SUCCESS(status))
        //        {
        //            return status;
        //        }

        //        if (strcmp(token + 1, ansiString) == 0)
        //        {
        //            inTargetSection = TRUE;
        //        }
        //        else
        //        {
        //            inTargetSection = FALSE;
        //        }

        //        // 释放资源
        //        if (ansiString != NULL)
        //        {
        //            RtlFreeAnsiString((PANSI_STRING)&ansiString);
        //        }
        //    }
        //    else if (inTargetSection)
        //    {
        //        // 判断是否是目标键
        //        PCHAR equalSign = strchr(token, '=');
        //        if (equalSign != NULL)
        //        {
        //            *equalSign = '\0';

        //            PCHAR ansiString = NULL;
        //            status = ConvertUnicodeToAnsi(keyName, &ansiString);
        //            if (!NT_SUCCESS(status))
        //            {
        //                return status;
        //            }


        //            if (strcmp(token, ansiString) == 0)
        //            {
        //                valueStart = equalSign + 1;
        //                valueEnd = token + strlen(token);
        //            }

        //            // 释放资源
        //            if (ansiString != NULL)
        //            {
        //                RtlFreeAnsiString((PANSI_STRING)&ansiString);
        //            }
        //        }
        //    }

        //    token = strtok(NULL, "\r\n");
        //}

        //// 复制键值到输出缓冲区
        //if (valueStart != NULL && valueEnd != NULL && valueSize >= valueEnd - valueStart + 1)
        //{
        //    RtlCopyMemory(value, valueStart, valueEnd - valueStart);
        //    value[valueEnd - valueStart] = '\0';
        //    status = STATUS_SUCCESS;
        //}
        //else
        //{
        //    status = STATUS_BUFFER_TOO_SMALL;
        //}

        return status;
    }

    PWSTR ReadIni(_In_ PCWSTR filePath, _In_ PCWSTR sectionName, _In_ PCWSTR keyName)
    {
        static WCHAR valueBuffer[256];  // 用于存储键值的缓冲区

        NTSTATUS status = ReadIniValue(filePath, sectionName, keyName, valueBuffer, sizeof(valueBuffer));

        if (!NT_SUCCESS(status))
        {
            // 处理错误，例如打印错误日志、抛出异常等
            // ...
            return nullptr;
        }

        return valueBuffer;
    }


    NTSTATUS InitDriverLog()
    {
        // Retired compatibility entry point.  Returning success prevents a
        // nonessential diagnostic sink from blocking driver initialization;
        // boLogInit stays false so no kernel file I/O can occur.
        boLogInit = false;
        return STATUS_SUCCESS;
    }

    NTSTATUS CreateInternalThread(PKSTART_ROUTINE StartRoutine, PVOID StartContext, PETHREAD* Thread)
    {
        NTSTATUS status;
        HANDLE hThread;
        OBJECT_ATTRIBUTES objectAttributes;
        InitializeObjectAttributes(&objectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

        // 创建线程
        status = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, &objectAttributes, NULL, NULL, StartRoutine, StartContext);
        if (!NT_SUCCESS(status))
        {
            // 处理错误
            return status;
        }

        // 获取线程对象指针
        //status = ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, (PVOID*)Thread, NULL);
        //if (!NT_SUCCESS(status))
        //{
        //    // 处理错误
        //    ZwClose(hThread);
        //    return status;
        //}

        // 关闭线程句柄
        ZwClose(hThread);

        return STATUS_SUCCESS;
    }

    //NTSTATUS CreateKernelThread(PKSTART_ROUTINE StartRoutine, PTHREAD_DATA threadData, PETHREAD* Thread)
    //{
    //    // 创建线程
    //    return CreateInternalThread(StartRoutine, threadData, Thread);
    //}

    VOID KernelSleep(UINT32 milliseconds)
    {
        LARGE_INTEGER delay;
        delay.QuadPart = -((LONGLONG)milliseconds * 10 * 1000);  // 转换为100纳秒单位

        KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }

    VOID RemovePath(WCHAR* fullPath)
    {
        WCHAR* lastSlash = wcsrchr(fullPath, L'\\');  // 在字符串中查找最后一个目录分隔符 '\'

        if (lastSlash != NULL)
        {
            WCHAR* fileName = lastSlash + 1;  // 跳过目录分隔符
            wcscpy_s(fullPath, wcslen(fileName) + 1, fileName);
        }
    }
}
