#pragma once

// 统一设计令牌（Design Tokens）。
//
// 界面层所有颜色、字号、间距、圆角、控件尺寸都必须来自本文件，页面代码里
// 不允许再出现写死的颜色值或魔法数字。这样后续换肤或调整密度时只需要改一处。

#include <cstdint>
#include <string>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace unrealdbg_ui
{
    namespace token
    {
        using winrt::Microsoft::UI::Xaml::Controls::Border;
        using winrt::Microsoft::UI::Xaml::Controls::FontIcon;
        using winrt::Microsoft::UI::Xaml::Controls::TextBlock;
        using winrt::Microsoft::UI::Xaml::Media::FontFamily;
        using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
        using winrt::Windows::UI::Color;
        using winrt::Windows::UI::Text::FontWeight;

        inline Color Rgb(const uint8_t red, const uint8_t green, const uint8_t blue, const uint8_t alpha = 255) noexcept
        {
            return Color{ alpha, red, green, blue };
        }

        // ---------------------------------------------------------------- 颜色
        // 中性色（由深到浅）
        inline const Color kWindowTop{ Rgb(0x10, 0x14, 0x1C) };
        inline const Color kWindowBottom{ Rgb(0x0A, 0x0D, 0x13) };
        inline const Color kTitleBar{ Rgb(0x0C, 0x0F, 0x16) };
        inline const Color kSidebar{ Rgb(0x11, 0x15, 0x1D) };
        inline const Color kCard{ Rgb(0x16, 0x1B, 0x25) };
        inline const Color kCardInset{ Rgb(0x10, 0x15, 0x1E) };
        inline const Color kCardHover{ Rgb(0x1C, 0x23, 0x30) };
        inline const Color kSunken{ Rgb(0x0B, 0x0F, 0x15) };
        inline const Color kBorder{ Rgb(0x22, 0x2A, 0x37) };
        inline const Color kBorderStrong{ Rgb(0x2E, 0x38, 0x49) };
        inline const Color kTextPrimary{ Rgb(0xE9, 0xEE, 0xF8) };
        inline const Color kTextSecondary{ Rgb(0x9B, 0xA7, 0xBD) };
        inline const Color kTextMuted{ Rgb(0x6B, 0x76, 0x89) };
        inline const Color kTextOnAccent{ Rgb(0xFF, 0xFF, 0xFF) };

        // 强调色与语义色
        inline const Color kAccent{ Rgb(0x4C, 0x8D, 0xFF) };
        inline const Color kAccentHover{ Rgb(0x62, 0x9C, 0xFF) };
        inline const Color kAccentPressed{ Rgb(0x3A, 0x76, 0xE0) };
        inline const Color kAccentSoft{ Rgb(0x18, 0x26, 0x3D) };
        inline const Color kAccentText{ Rgb(0xB8, 0xD3, 0xFF) };

        inline const Color kSuccess{ Rgb(0x46, 0xD0, 0x7F) };
        inline const Color kSuccessSoft{ Rgb(0x11, 0x29, 0x1C) };
        inline const Color kWarning{ Rgb(0xF2, 0xB4, 0x5C) };
        inline const Color kWarningSoft{ Rgb(0x2E, 0x24, 0x11) };
        inline const Color kDanger{ Rgb(0xFF, 0x6B, 0x7D) };
        inline const Color kDangerSoft{ Rgb(0x2E, 0x14, 0x1A) };
        inline const Color kInfo{ Rgb(0x63, 0xB8, 0xFF) };
        inline const Color kInfoSoft{ Rgb(0x12, 0x25, 0x36) };
        inline const Color kDebug{ Rgb(0x8C, 0xB7, 0xFF) };
        inline const Color kDebugSoft{ Rgb(0x14, 0x1C, 0x2B) };

        // -------------------------------------------------------------- 字号
        // 紧凑窗口（940x690）下的字号层级：10.5 / 12 / 12.5 / 14.5 / 16 / 20，
        // 相邻层级间隔 >=1px，整体较宽松版（14 主字号）下调约 12%，保证小窗口里
        // 标题、正文、说明的层级差异仍然一眼可辨。
        inline constexpr double kFontMicro{ 10.5 };      // 徽标、极小提示
        inline constexpr double kFontCaption{ 12.0 };    // 说明文字、副标题
        inline constexpr double kFontBody{ 12.5 };       // 正文、行标题
        inline constexpr double kFontSubtitle{ 14.5 };   // 卡片标题
        inline constexpr double kFontTitle{ 16.0 };      // 页面标题、指标数值
        inline constexpr double kFontHero{ 20.0 };       // 主数值
        inline constexpr double kFontMono{ 12.0 };       // 等宽文本（路径、日志）

        inline constexpr FontWeight kWeightRegular{ 400 };
        inline constexpr FontWeight kWeightMedium{ 500 };
        inline constexpr FontWeight kWeightSemiBold{ 600 };

        // -------------------------------------------------------------- 行高
        // 行高规范：单行文本 1.30 倍字号，多行文本 1.50 倍字号。所有文本工厂
        // （Text / TextWrapped / Mono）统一套用，页面代码不得再各自设置 LineHeight。
        inline constexpr double kLineHeightTight{ 1.3 };
        inline constexpr double kLineHeightRelaxed{ 1.5 };
        inline constexpr double kLineHeightMono{ 1.38 };

        // -------------------------------------------------------------- 间距
        // 紧凑窗口下间距阶梯整体收紧一档，保持留白节奏但不浪费纵向空间。
        inline constexpr double kSpaceXs{ 4.0 };
        inline constexpr double kSpaceSm{ 8.0 };
        inline constexpr double kSpaceMd{ 12.0 };
        inline constexpr double kSpaceLg{ 15.0 };
        inline constexpr double kSpaceXl{ 18.0 };
        inline constexpr double kSpaceXxl{ 22.0 };

        // -------------------------------------------------------------- 圆角
        inline constexpr double kRadiusSm{ 6.0 };
        inline constexpr double kRadiusMd{ 8.0 };
        inline constexpr double kRadiusLg{ 13.0 };

        // -------------------------------------------------------------- 尺寸
        inline constexpr double kTitleBarHeight{ 44.0 };
        inline constexpr double kNavWidth{ 192.0 };
        inline constexpr double kNavCollapsedWidth{ 56.0 };
        inline constexpr double kNavItemHeight{ 34.0 };
        inline constexpr double kSplitterHeight{ 5.0 };
        inline constexpr double kLogHeaderHeight{ 34.0 };
        // 需求：窗口整体缩减约 30% 后，日志面板默认高度同步从 264 收紧到 190。
        inline constexpr double kLogDefaultHeight{ 190.0 };
        inline constexpr double kLogMinHeight{ 116.0 };
        inline constexpr double kLogMaxRatio{ 0.6 };
        // 需求：默认窗口由 1340x980 缩减到 940x690（宽高各约 -30%）。
        inline constexpr double kWindowDefaultWidth{ 940.0 };
        inline constexpr double kWindowDefaultHeight{ 690.0 };
        inline constexpr double kWindowMinWidth{ 880.0 };
        inline constexpr double kWindowMinHeight{ 600.0 };

        // ------------------------------------------------------------ 图标字形
        // 全部取自 Segoe MDL2 Assets / Segoe Fluent Icons 的常用码位。
        inline const wchar_t* const kGlyphOverview{ L"\uE80F" };   // Home
        inline const wchar_t* const kGlyphProcess{ L"\uE7B8" };    // Devices
        inline const wchar_t* const kGlyphShield{ L"\uE72E" };     // Lock
        inline const wchar_t* const kGlyphAbout{ L"\uE946" };      // Info
        inline const wchar_t* const kGlyphLog{ L"\uE756" };        // CommandPrompt
        inline const wchar_t* const kGlyphAdd{ L"\uE710" };        // Add
        inline const wchar_t* const kGlyphPlay{ L"\uE768" };       // Play
        inline const wchar_t* const kGlyphPause{ L"\uE769" };      // Pause
        inline const wchar_t* const kGlyphRemove{ L"\uE74D" };     // Delete
        inline const wchar_t* const kGlyphCopy{ L"\uE8C8" };       // Copy
        inline const wchar_t* const kGlyphClear{ L"\uE74D" };      // Delete
        inline const wchar_t* const kGlyphRefresh{ L"\uE72C" };    // Refresh
        inline const wchar_t* const kGlyphPin{ L"\uE718" };        // Pin
        inline const wchar_t* const kGlyphChevronDown{ L"\uE70D" };
        inline const wchar_t* const kGlyphChevronUp{ L"\uE70E" };
        inline const wchar_t* const kGlyphChevronLeft{ L"\uE76B" };
        inline const wchar_t* const kGlyphChevronRight{ L"\uE76C" };
        inline const wchar_t* const kGlyphFolder{ L"\uE8B7" };    // Folder
        inline const wchar_t* const kGlyphCheck{ L"\uE73E" };      // CheckMark
        inline const wchar_t* const kGlyphWarning{ L"\uE7BA" };    // Warning
        inline const wchar_t* const kGlyphError{ L"\uEA39" };      // StatusErrorFull
        inline const wchar_t* const kGlyphTimer{ L"\uE916" };      // Stopwatch
        inline const wchar_t* const kGlyphServer{ L"\uE968" };     // Server
        inline const wchar_t* const kGlyphSymbols{ L"\uE943" };    // Code
        inline const wchar_t* const kGlyphFilter{ L"\uE71C" };     // Filter

        inline const wchar_t* const kIconFontFamily{ L"Segoe Fluent Icons,Segoe MDL2 Assets" };

        // -------------------------------------------------------------- 字体族
        // 界面主字体：拉丁字形走 Windows 11 的 Segoe UI Variable（大字号用 Display
        // 变体，字形更舒展；正文用 Text 变体，小字号下更清晰），中文字形走
        // Microsoft YaHei UI（雅黑 UI 专为界面 hinting，比传统雅黑更锐利）。
        // 逗号列表即 DirectWrite 的字体回退链，缺字时按顺序回退。
        inline const wchar_t* const kUiFontFamily{
            L"Segoe UI Variable Text, Microsoft YaHei UI, Microsoft YaHei, Segoe UI" };
        inline const wchar_t* const kUiDisplayFontFamily{
            L"Segoe UI Variable Display, Segoe UI Variable Text, Microsoft YaHei UI, Segoe UI" };
        inline const wchar_t* const kMonoFontFamily{ L"Cascadia Mono, Consolas, Courier New" };

        // 字号 -> 行高（像素）换算，供文本工厂统一套用。
        inline double LineHeightFor(const double size, const double ratio) noexcept
        {
            const double value = size * ratio;
            return static_cast<double>(static_cast<int>(value * 2.0 + 0.5)) / 2.0;  // 取 0.5 的整数倍
        }

        // ------------------------------------------------------------ 基础图元
        inline SolidColorBrush Brush(const Color color)
        {
            return SolidColorBrush(color);
        }

        // 所有文本统一走这里：字号（外部传入）+ 字体族（按字号自动切 Display/Text
        // 变体）+ 行高（按行高规范换算），页面代码不再重复设置字体属性。
        inline TextBlock Text(const winrt::hstring& value, const double size, const Color color,
            const FontWeight weight = kWeightRegular)
        {
            TextBlock block;
            block.Text(value);
            block.FontSize(size);
            block.FontFamily(FontFamily(size >= kFontSubtitle ? kUiDisplayFontFamily : kUiFontFamily));
            block.Foreground(Brush(color));
            block.FontWeight(weight);
            block.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
            block.LineStackingStrategy(winrt::Microsoft::UI::Xaml::LineStackingStrategy::BlockLineHeight);
            block.LineHeight(LineHeightFor(size, kLineHeightTight));
            return block;
        }

        // 多行正文走彻底放宽的行高（1.55），这是"读得舒服"的关键。
        inline TextBlock TextWrapped(const winrt::hstring& value, const double size, const Color color,
            const FontWeight weight = kWeightRegular)
        {
            TextBlock block = Text(value, size, color, weight);
            block.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);
            block.LineHeight(LineHeightFor(size, kLineHeightRelaxed));
            return block;
        }

        inline TextBlock Mono(const winrt::hstring& value, const double size, const Color color)
        {
            TextBlock block = Text(value, size, color);
            block.FontFamily(FontFamily(kMonoFontFamily));
            block.LineHeight(LineHeightFor(size, kLineHeightMono));
            return block;
        }

        inline FontIcon Icon(const winrt::hstring& glyph, const double size, const Color color)
        {
            FontIcon icon;
            icon.Glyph(glyph);
            icon.FontSize(size);
            icon.Foreground(Brush(color));
            icon.FontFamily(FontFamily(kIconFontFamily));
            icon.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
            icon.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
            icon.IsHitTestVisible(false);
            return icon;
        }

        // 圆角 + 边框的矩形块，界面里所有"面"都由它拼装。
        inline Border Surface(const Color background, const Color border, const double radius = kRadiusLg,
            const double padding = kSpaceLg, const double borderThickness = 1.0)
        {
            Border surface;
            surface.Background(Brush(background));
            surface.BorderBrush(Brush(border));
            surface.BorderThickness(winrt::Microsoft::UI::Xaml::Thickness{ borderThickness, borderThickness, borderThickness, borderThickness });
            surface.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{ radius, radius, radius, radius });
            surface.Padding(winrt::Microsoft::UI::Xaml::Thickness{ padding, padding, padding, padding });
            return surface;
        }

        // 带底色的小圆点，用于状态指示。
        inline Border Dot(const Color color, const double size = 8.0)
        {
            Border dot;
            dot.Width(size);
            dot.Height(size);
            dot.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{ size / 2, size / 2, size / 2, size / 2 });
            dot.Background(Brush(color));
            dot.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
            return dot;
        }

        // 胶囊标签：状态、计数、分级都用它。
        inline Border Pill(const winrt::hstring& value, const Color foreground, const Color background,
            const double fontSize = kFontMicro)
        {
            Border pill;
            pill.Background(Brush(background));
            pill.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{ 999, 999, 999, 999 });
            pill.Padding(winrt::Microsoft::UI::Xaml::Thickness{ 10, 3, 10, 3 });
            pill.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
            TextBlock label = Text(value, fontSize, foreground);
            label.TextAlignment(winrt::Microsoft::UI::Xaml::TextAlignment::Center);
            pill.Child(label);
            return pill;
        }

        inline Border Separator(const Color color, const double thickness = 1.0)
        {
            Border line;
            line.Height(thickness);
            line.Background(Brush(color));
            line.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
            return line;
        }

        inline winrt::Microsoft::UI::Xaml::Thickness Insets(const double left, const double top,
            const double right, const double bottom)
        {
            return winrt::Microsoft::UI::Xaml::Thickness{ left, top, right, bottom };
        }

        inline void SetToolTip(const winrt::Microsoft::UI::Xaml::DependencyObject& element, const winrt::hstring& value)
        {
            winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(element, winrt::box_value(value));
        }
    }

    // 兼容简写：页面代码里用 ui::Text(...) 而不是 token::Text(...)。
    namespace ui = token;
}
