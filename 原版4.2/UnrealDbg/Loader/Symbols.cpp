#include "WinMain.h"
#include "Symbols.h"

std::wstring g_szPdbPath;

CStatusCallback callback;


// 封装目录创建的函数
bool CreateDirectoryIfNotExists(const TCHAR* dir) {
	if (CreateDirectory(dir, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
		return true;
	}
	else {
		return false;
	}
}


bool VerifyExistingPdb(const GUID& guid)
{
	std::ifstream f(g_szPdbPath.c_str(), std::ios::binary | std::ios::ate);
	if (f.bad())
	{
		return false;
	}

	size_t size_on_disk = static_cast<size_t>(f.tellg());
	if (!size_on_disk)
	{
		f.close();

		return false;
	}

	char* pdb_raw = new char[size_on_disk];
	if (!pdb_raw)
	{
		f.close();

		return false;
	}

	f.seekg(std::ios::beg);
	f.read(pdb_raw, size_on_disk);
	f.close();

	if (size_on_disk < sizeof(PDBHeader7))
	{
		delete[] pdb_raw;

		return false;
	}

	auto* pPDBHeader = reinterpret_cast<PDBHeader7*>(pdb_raw);

	if (memcmp(pPDBHeader->signature, "Microsoft C/C++ MSF 7.00\r\n\x1A""DS\0\0\0", sizeof(PDBHeader7::signature)))
	{
		delete[] pdb_raw;

		return false;
	}

	if (size_on_disk < (size_t)pPDBHeader->page_size * pPDBHeader->file_page_count)
	{
		delete[] pdb_raw;

		return false;
	}

	int* pRootPageNumber = reinterpret_cast<int*>(pdb_raw + (size_t)pPDBHeader->root_stream_page_number_list_number * pPDBHeader->page_size);
	auto* pRootStream = reinterpret_cast<RootStream7*>(pdb_raw + (size_t)(*pRootPageNumber) * pPDBHeader->page_size);

	std::map<int, std::vector<int>> streams;
	int current_page_number = 0;

	for (int i = 0; i != pRootStream->num_streams; ++i)
	{
		int current_size = pRootStream->stream_sizes[i] == 0xFFFFFFFF ? 0 : pRootStream->stream_sizes[i];

		int current_page_count = current_size / pPDBHeader->page_size;
		if (current_size % pPDBHeader->page_size)
		{
			++current_page_count;
		}

		std::vector<int> numbers;

		for (int j = 0; j != current_page_count; ++j, ++current_page_number)
		{
			numbers.push_back(pRootStream->stream_sizes[pRootStream->num_streams + current_page_number]);
		}

		streams.insert({ i, numbers });
	}

	auto pdb_info_page_index = streams.at(1).at(0);

	auto* stram_data = reinterpret_cast<GUID_StreamData*>(pdb_raw + (size_t)(pdb_info_page_index)*pPDBHeader->page_size);

	int guid_eq = memcmp(&stram_data->guid, &guid, sizeof(GUID));

	delete[] pdb_raw;

	return (guid_eq == 0);
}

bool DownloadSymbol_internal(const std::wstring& szModulePath, const std::wstring& path, std::wstring* pdb_path_out, bool Redownload)
{
	std::ifstream File(szModulePath, std::ios::binary | std::ios::ate);
	if (!File.good())
	{
		Common::ReportSeriousError("%s[%d] 打开文件失败! (error: %d)", __func__, __LINE__, GetLastError());
		return false;
	}

	auto FileSize = File.tellg();
	if (!FileSize)
	{
		Common::ReportSeriousError("%s[%d] 获取文件大小失败! (error: %d)", __func__, __LINE__, GetLastError());
		return false;
	}

	BYTE* pRawData = new(std::nothrow) BYTE[static_cast<size_t>(FileSize)];
	if (!pRawData)
	{
		delete[] pRawData;

		File.close();

		Common::ReportSeriousError("%s[%d] 分配内存失败! (error: %d)", __func__, __LINE__, GetLastError());
		return false;
	}

	File.seekg(0, std::ios::beg);
	File.read(reinterpret_cast<char*>(pRawData), FileSize);
	File.close();

	IMAGE_DOS_HEADER* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(pRawData);
	IMAGE_NT_HEADERS* pNT = reinterpret_cast<IMAGE_NT_HEADERS*>(pRawData + pDos->e_lfanew);
	IMAGE_FILE_HEADER* pFile = &pNT->FileHeader;

	IMAGE_OPTIONAL_HEADER64* pOpt64 = nullptr;
	IMAGE_OPTIONAL_HEADER32* pOpt32 = nullptr;

	bool x86 = false;

	if (pFile->Machine == IMAGE_FILE_MACHINE_AMD64)
	{
		pOpt64 = reinterpret_cast<IMAGE_OPTIONAL_HEADER64*>(&pNT->OptionalHeader);
	}
	else if (pFile->Machine == IMAGE_FILE_MACHINE_I386)
	{
		pOpt32 = reinterpret_cast<IMAGE_OPTIONAL_HEADER32*>(&pNT->OptionalHeader);
		x86 = true;
	}
	else
	{
		delete[] pRawData;

		Common::ReportSeriousError("%s[%d] 获取Machine失败! (error: %d)", __func__, __LINE__, GetLastError());
		return false;
	}

	DWORD ImageSize = x86 ? pOpt32->SizeOfImage : pOpt64->SizeOfImage;
	BYTE* pLocalImageBase = reinterpret_cast<BYTE*>(VirtualAlloc(nullptr, ImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
	if (!pLocalImageBase)
	{

		delete[] pRawData;

		Common::ReportSeriousError("%s[%d] 分配内存失败! (error: %d)", __func__, __LINE__, GetLastError());
		return false;
	}

	memcpy(pLocalImageBase, pRawData, x86 ? pOpt32->SizeOfHeaders : pOpt64->SizeOfHeaders);

	auto* pCurrentSectionHeader = IMAGE_FIRST_SECTION(pNT);
	for (UINT i = 0; i != pFile->NumberOfSections; ++i, ++pCurrentSectionHeader)
	{
		if (pCurrentSectionHeader->SizeOfRawData)
		{
			memcpy(pLocalImageBase + pCurrentSectionHeader->VirtualAddress, pRawData + pCurrentSectionHeader->PointerToRawData, pCurrentSectionHeader->SizeOfRawData);
		}
	}

	IMAGE_DATA_DIRECTORY* pDataDir = nullptr;
	if (x86)
	{
		pDataDir = &pOpt32->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
	}
	else
	{
		pDataDir = &pOpt64->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
	}

	IMAGE_DEBUG_DIRECTORY* pDebugDir = reinterpret_cast<IMAGE_DEBUG_DIRECTORY*>(pLocalImageBase + pDataDir->VirtualAddress);

	if (!pDataDir->Size || IMAGE_DEBUG_TYPE_CODEVIEW != pDebugDir->Type)
	{
		VirtualFree(pLocalImageBase, 0, MEM_RELEASE);

		delete[] pRawData;

		Common::ReportSeriousError("%s[%d] 获取DebugDir失败! (error: %d)", __func__, __LINE__, GetLastError());
		return false;
	}

	PdbInfo* pdb_info = reinterpret_cast<PdbInfo*>(pLocalImageBase + pDebugDir->AddressOfRawData);
	if (pdb_info->Signature != 0x53445352)
	{
		VirtualFree(pLocalImageBase, 0, MEM_RELEASE);

		delete[] pRawData;

		Common::ReportSeriousError("%s[%d] pdb_info->Signature不匹配! (error: %d)", __func__, __LINE__, GetLastError());
		return false;
	}
	
	g_szPdbPath = path;

	if (g_szPdbPath[g_szPdbPath.length() - 1] != L'\\')
	{
		g_szPdbPath += L'\\';
	}

	// 创建目录
	CreateDirectoryIfNotExists(g_szPdbPath.c_str());  //创建baseDir	

	size_t len = lstrlenA(pdb_info->PdbFileName);
	wchar_t* PdbFileNameW = new(std::nothrow) wchar_t[len + 1]();
	if (!PdbFileNameW)
	{
		Common::ReportSeriousError("%s[%d] 分配内存失败! (error: %d)", __func__, __LINE__, GetLastError());
		return false;
	}

	size_t SizeOut = 0;
	auto conversion_ret = mbstowcs_s(&SizeOut, PdbFileNameW, len + 1, pdb_info->PdbFileName, len);
	if (conversion_ret != 0 || !SizeOut)
	{

		delete[] PdbFileNameW;

		Common::ReportSeriousError("%s[%d] 转换Unicode字符串失败! (error: %d)", __func__, __LINE__, GetLastError());
		return false;
	}

	g_szPdbPath += PdbFileNameW;
	CreateDirectoryIfNotExists(g_szPdbPath.c_str()); //二级目录

	delete[] PdbFileNameW;

	if (Redownload)
	{
		wchar_t w_GUID[100]{ 0 };
		if (!StringFromGUID2(pdb_info->Guid, w_GUID, 100))
		{
			VirtualFree(pLocalImageBase, 0, MEM_RELEASE);

			delete[] pRawData;

			Common::ReportSeriousError("%s[%d] 获取Guid失败! (error: %d)", __func__, __LINE__, GetLastError());
			return false;
		}

		char a_GUID[100]{ 0 };
		size_t l_GUID = 0;
		conversion_ret = wcstombs_s(&l_GUID, a_GUID, w_GUID, sizeof(a_GUID));
		if (conversion_ret != 0 || !l_GUID)
		{

			VirtualFree(pLocalImageBase, 0, MEM_RELEASE);

			delete[] pRawData;

			Common::ReportSeriousError("%s[%d] 转换ANSI字符串失败! (error: %d)", __func__, __LINE__, GetLastError());
			return false;
		}

		std::string guid_filtered;
		for (UINT i = 0; i != l_GUID; ++i)
		{
			if ((a_GUID[i] >= '0' && a_GUID[i] <= '9') || (a_GUID[i] >= 'A' && a_GUID[i] <= 'F') || (a_GUID[i] >= 'a' && a_GUID[i] <= 'f'))
			{
				guid_filtered += a_GUID[i];
			}
		}

		char age[3]{ 0 };
		_itoa_s(pdb_info->Age, age, 10);

		std::map<std::wstring, std::wstring> config;
		std::wstring filename = FileSystem::GetModuleDirectory(NULL) + _T("SymbolTool\\Config.ini");
		//char szBuf[MAX_PATH] = { 0 };
		//sprintf(szBuf, "符号服务器: %s\n", Common::wideStringToString(filename).c_str());
		//OutputDebugStringA(szBuf);
		config[L"服务器"] = FileSystem::ReadIniValue(filename, L"符号", L"服务器");

		int selectSvr = 100; //默认选择镜像服务器
		if (config[L"服务器"] != L"")
		{
			selectSvr = std::stoi(config[L"服务器"]);
		}

		std::string url;
		if (selectSvr == 100)
		{
			url = "http://msdl.blackint3.com:88/download/symbols/";
		}
		else if(selectSvr == 200)
		{
			url = "https://msdl.microsoft.com/download/symbols/";
		}		
		url += pdb_info->PdbFileName;
		url += '/';
		url += guid_filtered;
		url += age;
		url += '/';
		url += pdb_info->PdbFileName;

		len = url.length();
		wchar_t* UrlW = new(std::nothrow) wchar_t[len + 1]();
		if (!UrlW)
		{
			Common::ReportSeriousError("%s[%d] 分配内存失败! (error: %d)", __func__, __LINE__, GetLastError());
			return false;
		}

		SizeOut = 0;
		conversion_ret = mbstowcs_s(&SizeOut, UrlW, len + 1, url.c_str(), len);
		if (conversion_ret != 0 || !SizeOut)
		{

			delete[] UrlW;

			Common::ReportSeriousError("%s[%d] 转换Unicode字符串失败! (error: %d)", __func__, __LINE__, GetLastError());
			return false;
		}

		if (g_szPdbPath[g_szPdbPath.length() - 1] != L'\\')
		{
			g_szPdbPath += L'\\';
		}
		g_szPdbPath += Common::stringToWideString(guid_filtered);
		g_szPdbPath += Common::stringToWideString(age);
		CreateDirectoryIfNotExists(g_szPdbPath.c_str()); //三级目录

		if (g_szPdbPath[g_szPdbPath.length() - 1] != L'\\')
		{
			g_szPdbPath += L'\\';
		}
		g_szPdbPath += Common::stringToWideString(pdb_info->PdbFileName);

		DWORD Filesize = 0;
		WIN32_FILE_ATTRIBUTE_DATA file_attr_data{ 0 };

		//Redownload = false;
		//if (GetFileAttributesExW(g_szPdbPath.c_str(), GetFileExInfoStandard, &file_attr_data))
		//{
		//	Filesize = file_attr_data.nFileSizeLow;

		//	if (!Redownload && !VerifyExistingPdb(pdb_info->Guid))
		//	{
		//		Redownload = true;
		//	}

		//	if (Redownload)
		//	{
		//		DeleteFileW(g_szPdbPath.c_str());
		//	}
		//}
		//else
		//{
		//	Redownload = true;
		//}

		if (Redownload)
		{
			//不能用于下载大小超过 4 GB 的文件
			auto hr = URLDownloadToFileW(nullptr, UrlW, g_szPdbPath.c_str(), NULL, &callback);

			if (hr != S_OK)
			{
				Common::ReportSeriousError("下载符号表失败，系统无法找到指定的资源! "
					"请按照说明书操作使用第三方工具进行下载。(error: %d)", GetLastError());
				exit(0);
			}
			//char szBuf[MAX_PATH] = { 0 };
			//sprintf(szBuf, "下载符号表结束: %d\n", hr);
			//OutputDebugStringA(szBuf);
		}		

		delete[] UrlW;
	}

	VirtualFree(pLocalImageBase, 0, MEM_RELEASE);

	delete[] pRawData;	

	return true;
}