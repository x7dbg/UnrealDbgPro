@title Debug Release 临时文件清理！
@echo.
@echo         删除当前目录树下所有位于 Debug 或 Release 目录中的文件
@echo                                                         ---wlc 2009/10/12
@echo.
@echo off
@setlocal enabledelayedexpansion
set /a tm=0
set /a size=0 
set /a fa=0
@color 8b
for /r %%i in (*.aps,*.ilk,*.sdf,*.suo,*res,*.unsuccessfulbuild,*.lastbuildstate,*.manifest,*.obj,*.pch,*.pdb,*.tlog,*.log,*.idb) do if exist %%i    @echo Deleteing %%i & del "%%i" && if not exist %%i  (set /a tm+=1 && set /a size+=%%~zi && @title 已经清理 !tm!个文件...) else (set /a fa+=1)
@echo.
@echo.
@set /a m=%size%/1000000
@set /a la=%size%%%1000000
@title Debug Release 临时文件清理！
@echo         操作完毕! 
@set /a num = %tm% + %fa%
if %num% EQU 0 (@echo         没有需要清除的文件! ) else (@echo         总共清除文件 %tm% 个,节省磁盘空间 %m%.%la% 兆。)
if %fa% GTR 0 @echo         %fa% 个文件清除失败，也许他们正在被使用...
@echo.
@echo         %date% %time%
@echo.