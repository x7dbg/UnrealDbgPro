/*
www.sourceforge.net/projects/tinyxml
Original code (2.0 and earlier )copyright (c) 2000-2006 Lee Thomason (www.grinninglizard.com)

This software is provided 'as-is', without any express or implied 
warranty. In no event will the authors be held liable for any 
damages arising from the use of this software.

Permission is granted to anyone to use this software for any 
purpose, including commercial applications, and to alter it and 
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must
not claim that you wrote the original software. If you use this
software in a product, an acknowledgment in the product documentation
would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/

#include "tinyxml.h"

// The goal of the seperate error file is to make the first
// step towards localization. tinyxml (currently) only supports
// english error messages, but the could now be translated.
//
// It also cleans up the code a bit.
//

const char* TiXmlBase::errorString[ TiXmlBase::TIXML_ERROR_STRING_COUNT ] =
{
	"无错误",
	"错误",
	"打开文件失败",
	"解析元素失败",
	"读取元素名称失败",
	"读取元素值失败",
	"读取属性失败",
	"错误：标签为空",
	"读取结束标签失败",
	"解析未知节点失败",
	"解析注释失败",
	"解析声明失败",
	"文档为空",
	"输入流中出现空值（0）或意外的文件结束",
	"解析 CDATA 失败",
	"TiXmlDocument 加入文档失败：TiXmlDocument 只能位于根节点",
};
