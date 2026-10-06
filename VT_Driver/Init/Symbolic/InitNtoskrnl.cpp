#include "../../Driver.h"
#include "../../poolmanager.h"
#include "../../Globals.h"
#include "InitNtoskrnl.h"


BOOLEAN InitNtoskrnlSymbolsTable()
{

    //DbgBreakPoint();
    symbolic_access::ModuleExtenderFactory extenderFactory{};
    const auto& moduleExtender = extenderFactory.Create(L"ntoskrnl.exe");
    if (!moduleExtender.has_value())
    {
        outDebug("ntoskrnl.exe 符号初始化失败..");
        return FALSE;
    }

    PsGetNextProcess = (PFN_PSGETNEXTPROCESS)moduleExtender->GetPointer<PFN_PSGETNEXTPROCESS>("PsGetNextProcess");
    if (PsGetNextProcess == nullptr)
    {
        outDebug("ntoskrnl.exe 符号初始化失败：PsGetNextProcess 未解析；原因：PDB 与当前内核版本不匹配；解决方案：安装匹配 Build 的 ntoskrnl PDB。\n");
        return FALSE;
    }

    return TRUE;
}
