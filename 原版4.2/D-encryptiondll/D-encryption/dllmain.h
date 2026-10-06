#pragma once

#ifndef _DLL_MAIN_H
#define _DLL_MAIN_H

#include <iostream>
#include <Windows.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <process.h>
#include <fstream>
#include <string>
#include <ctime>
#include <TlHelp32.h>
#include <assert.h>
#include <sstream>
#include <mutex>
#include <map>
#include <algorithm>
#include <winternl.h>
#include "../Common/Common.h"

void EncryptDataToFile_internal(const std::string& data, const std::string& filename, const std::string& userKey);
void DecryptDataFromFile_internal(const std::string& filename, const std::string& userKey, std::string& decryptedData);


#endif // !_DLL_MAIN_H
