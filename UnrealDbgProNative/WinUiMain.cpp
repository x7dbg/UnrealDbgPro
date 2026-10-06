// UnrealDbgProNative - WinUI 3 前端主界面（重做版）
//
// 设计约定：
//   1. 界面层不直接调用后端 DLL，一律通过 WinUiController 取数/发指令；
//   2. 所有颜色、字号、间距、圆角、控件尺寸取自 UiTheme.h 的设计令牌；
//   3. 页面只由 UiComponents.h 里的工厂函数拼装，禁止页面内散落魔法数字；
//   4. 状态只有一个来源（controller_），界面元素只做"渲染"，不做状态缓存。

#include "WinUiController.h"
#include "LogPanel.h"
#include "UiComponents.h"
#include "UiTheme.h"
#include "SymbolCache.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <microsoft.ui.xaml.window.h>
#include <MddBootstrap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

using namespace winrt;
using Windows::Foundation::IInspectable;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using winrt::Windows::UI::Color;

namespace ui = unrealdbg_ui;
namespace tok = unrealdbg_ui::token;   // 设计令牌与基础图元
namespace xaml_input = winrt::Microsoft::UI::Xaml::Input;

namespace
{
    constexpr int kPageCount = 4;

    // --ui-smoke-test 的退出码，由界面自检结果决定。
    int g_smokeTestExitCode = 0;

    enum class PageId
    {
        Overview = 0,
        Debuggers = 1,
        Tl = 2,
        Diagnostics = 3,
    };

    // 设置按钮的启用状态和透明度
    void SetButtonEnabled(const Button& button, const bool enabled)
    {
        if (button == nullptr)
        {
            return;
        }
        button.IsEnabled(enabled);
        button.Opacity(enabled ? 1.0 : 0.45);
    }

    // 按钮内容统一是 [图标, 文本] 的 StackPanel，这里只替换文本节点
    void SetButtonCaption(const Button& button, const hstring& caption)
    {
        if (button == nullptr)
        {
            return;
        }
        const StackPanel panel = button.Content().try_as<StackPanel>();
        if (panel == nullptr)
        {
            return;
        }
        const uint32_t count = panel.Children().Size();
        for (uint32_t index = 0; index < count; ++index)
        {
            TextBlock text = panel.Children().GetAt(index).try_as<TextBlock>();
            if (text != nullptr)
            {
                text.Text(caption);
                return;
            }
        }
    }

    // 在资源管理器中打开指定路径
    void OpenInExplorer(const std::wstring& path)
    {
        if (path.empty())
        {
            return;
        }
        ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    // 将 hstring 转换为宽字符串
    std::wstring ToWide(const hstring& value)
    {
        return std::wstring(value.c_str(), value.size());
    }

    // 界面文案兜底：采集结果为空时也必须给出确定文字，界面上不允许出现"读取中"
    hstring NonEmptyOr(const std::wstring& value, const wchar_t* fallback)
    {
        return hstring(value.empty() ? fallback : value.c_str());
    }
}

// ============================================================================
// 主窗口类
// ============================================================================

class Shell final
{
public:
    Shell(std::wstring directory, const bool smokeTest)
        : controller_(std::move(directory)), smokeTest_(smokeTest)
    {
        // 需求 6：统一以"真实运行目录"为基准解析 bin / Config / Log
        // 该目录由 Core 侧探测（exe 同级优先），因此从项目根目录启动与从
        // 产物目录启动得到完全一致的路径结果
        applicationDirectory_ = unrealdbg_native::GetRuntimeDirectory();
    }

    // 启动主窗口
    void Launch()
    {
        window_ = Window();
        window_.Title(L"虚幻调试器");
        window_.Closed({ this, &Shell::OnClosed });

        BuildLayout();
        window_.Content(root_);
        window_.ExtendsContentIntoTitleBar(true);
        window_.SetTitleBar(titleBar_);
        window_.Activate();
        ApplyInitialWindowSize();

        if (!controller_.Start())
        {
            backendReady_ = false;
        }

        RefreshSystemInfo();
        RefreshDebuggerList();
        AppendPendingLogs();
        RefreshStatusPill();
        RefreshDiagnostics();
        RefreshLiveState();
        RefreshNavEnvironment();
        SelectPage(PageId::Overview);

        timer_ = DispatcherTimer();
        timer_.Interval(std::chrono::milliseconds(80));
        timer_.Tick({ this, &Shell::OnTimer });
        timer_.Start();

        if (smokeTest_)
        {
            StartUiSmokeTest();
        }
    }

private:
    // ---------------------------------------------------------------- 布局构建

    // 创建页面根容器（固定布局，不滚动）
    Grid MakePageRoot()
    {
        Grid page;
        page.Padding(tok::Insets(tok::kSpaceMd, tok::kSpaceMd, tok::kSpaceMd, tok::kSpaceMd));
        page.RowSpacing(tok::kSpaceMd);
        page.HorizontalAlignment(HorizontalAlignment::Stretch);
        page.VerticalAlignment(VerticalAlignment::Stretch);
        return page;
    }

    // 固定布局专用：按 true=Star / false=Auto 批量声明行定义，省掉每页的行样板代码
    void DeclareRows(Grid& grid, const std::initializer_list<bool> starRows)
    {
        for (const bool star : starRows)
        {
            RowDefinition row;
            row.Height(star ? GridLength{ 1, GridUnitType::Star } : GridLength{ 1, GridUnitType::Auto });
            grid.RowDefinitions().Append(row);
        }
    }

    // 需求 3：内容区彻底固定不滚动。窗口变小时不再出现滚动条，而是按内容区实际
    // 尺寸设置裁剪矩形，保证页面永远铺满可视区域且不会画到下方日志面板上
    void OnPageHostSizeChanged(const IInspectable&, const winrt::Microsoft::UI::Xaml::SizeChangedEventArgs& args)
    {
        ApplyContentClip(args.NewSize());
    }

    // 应用内容裁剪矩形
    void ApplyContentClip(const winrt::Windows::Foundation::Size& size)
    {
        if (pageHost_ == nullptr)
        {
            return;
        }
        RectangleGeometry clip;
        clip.Rect(winrt::Windows::Foundation::Rect{ 0.0f, 0.0f,
            (std::max)(0.0f, size.Width),
            (std::max)(0.0f, size.Height) });
        pageHost_.Clip(clip);
    }

    // 构建主窗口布局
    void BuildLayout()
    {
        root_ = Grid();
        root_.RequestedTheme(ElementTheme::Dark);

        // 创建渐变背景
        LinearGradientBrush backdrop;
        GradientStop top;
        top.Color(tok::kWindowTop);
        top.Offset(0.0);
        GradientStop bottom;
        bottom.Color(tok::kWindowBottom);
        bottom.Offset(1.0);
        backdrop.GradientStops().Append(top);
        backdrop.GradientStops().Append(bottom);
        root_.Background(backdrop);

        // 定义行：标题栏 / 主体 / 分割线 / 日志
        RowDefinition titleRow;
        titleRow.Height(GridLength{ tok::kTitleBarHeight });
        root_.RowDefinitions().Append(titleRow);
        RowDefinition bodyRow;
        bodyRow.Height(GridLength{ 1, GridUnitType::Star });
        root_.RowDefinitions().Append(bodyRow);
        RowDefinition splitterRow;
        splitterRow.Height(GridLength{ tok::kSplitterHeight });
        root_.RowDefinitions().Append(splitterRow);
        RowDefinition logRow;
        logRow.Height(GridLength{ 1, GridUnitType::Auto });
        root_.RowDefinitions().Append(logRow);

        titleBar_ = BuildTitleBar();
        Grid::SetRow(titleBar_, 0);
        root_.Children().Append(titleBar_);

        // 构建主体区域（侧边栏 + 内容区）
        Grid body;
        ColumnDefinition navColumn;
        navColumn.Width(GridLength{ tok::kNavWidth });
        body.ColumnDefinitions().Append(navColumn);
        ColumnDefinition contentColumn;
        contentColumn.Width(GridLength{ 1, GridUnitType::Star });
        body.ColumnDefinitions().Append(contentColumn);

        Border sidebar = BuildSidebar();
        Grid::SetColumn(sidebar, 0);
        body.Children().Append(sidebar);

        // 构建四个页面
        pages_[0] = BuildPageOverview();
        pages_[1] = BuildPageDebuggers();
        pages_[2] = BuildPageTl();
        pages_[3] = BuildPageDiagnostics();

        pageHost_ = Grid();
        pageHost_.MaxWidth(1000);
        pageHost_.HorizontalAlignment(HorizontalAlignment::Stretch);
        for (int index = 0; index < kPageCount; ++index)
        {
            pages_[index].Visibility(Visibility::Collapsed);
            pageHost_.Children().Append(pages_[index]);
        }

        // 需求：内容区不再滚动。页面宿主直接挂进 Grid，高度由 Star 行决定，每页内部
        // 用自己的行比例铺满；窗口过小时由 ApplyContentClip 裁剪，保证既不出现滚动条，
        // 也不会把内容画到下方日志面板上
        Grid::SetColumn(pageHost_, 1);
        body.Children().Append(pageHost_);
        pageHost_.SizeChanged({ this, &Shell::OnPageHostSizeChanged });

        Grid::SetRow(body, 1);
        root_.Children().Append(body);

        // 构建日志面板分割线
        splitter_ = Border();
        splitter_.Background(tok::Brush(tok::kWindowTop));
        splitter_.BorderBrush(tok::Brush(tok::kBorder));
        splitter_.BorderThickness(Thickness{ 0, 1, 0, 1 });
        Grid splitterHost;
        grip_ = Border();
        grip_.Width(46);
        grip_.Height(4);
        grip_.CornerRadius(CornerRadius{ 2, 2, 2, 2 });
        grip_.Background(tok::Brush(tok::kBorderStrong));
        grip_.HorizontalAlignment(HorizontalAlignment::Center);
        grip_.VerticalAlignment(VerticalAlignment::Center);
        splitterHost.Children().Append(grip_);
        splitter_.Child(splitterHost);
        splitter_.PointerPressed({ this, &Shell::OnSplitterPressed });
        splitter_.PointerEntered({ this, &Shell::OnSplitterEntered });
        splitter_.PointerExited({ this, &Shell::OnSplitterExited });
        try
        {
            splitter_.as<winrt::Microsoft::UI::Xaml::IUIElementProtected>().ProtectedCursor(
                winrt::Microsoft::UI::Input::InputSystemCursor::Create(
                    winrt::Microsoft::UI::Input::InputSystemCursorShape::SizeNorthSouth));
        }
        catch (const hresult_error&)
        {
            // 光标设置失败不影响拖拽功能，忽略
        }
        Grid::SetRow(splitter_, 2);
        root_.Children().Append(splitter_);

        // 初始化日志面板
        panel_.SetFoldHandler([this](const bool) { ApplyLogHeight(); });
        logRoot_ = panel_.Root().as<FrameworkElement>();
        ApplyLogHeight();
        Grid::SetRow(logRoot_, 3);
        root_.Children().Append(logRoot_);

        // 注册鼠标事件
        root_.PointerMoved({ this, &Shell::OnPointerMoved });
        root_.PointerReleased({ this, &Shell::OnPointerReleased });
        root_.PointerCaptureLost({ this, &Shell::OnPointerCaptureLost });
    }

    // 构建标题栏
    Grid BuildTitleBar()
    {
        titleBar_ = Grid();
        titleBar_.Padding(Thickness{ tok::kSpaceLg, 0, 138, 0 });
        for (int index = 0; index < 5; ++index)
        {
            ColumnDefinition column;
            column.Width(index == 3 ? GridLength{ 1, GridUnitType::Star } : GridLength{ 1, GridUnitType::Auto });
            titleBar_.ColumnDefinitions().Append(column);
        }

        // 左侧图标
        Border icon = tok::Surface(tok::kAccentSoft, tok::kAccentSoft, tok::kRadiusSm, 0);
        icon.Width(26);
        icon.Height(26);
        icon.VerticalAlignment(VerticalAlignment::Center);
        icon.Child(tok::Icon(tok::kGlyphShield, 14, tok::kAccentText));
        Grid::SetColumn(icon, 0);
        titleBar_.Children().Append(icon);

        // 标题文字
        StackPanel titles;
        titles.Orientation(Orientation::Horizontal);
        titles.Spacing(tok::kSpaceSm);
        titles.Margin(Thickness{ tok::kSpaceMd, 0, 0, 0 });
        titles.VerticalAlignment(VerticalAlignment::Center);
        titles.Children().Append(tok::Text(L"虚幻调试器", tok::kFontBody + 1, tok::kTextPrimary, tok::kWeightSemiBold));
        titles.Children().Append(tok::Text(L"VT 调试控制台", tok::kFontCaption - 0.5, tok::kTextMuted));
        Grid::SetColumn(titles, 1);
        titleBar_.Children().Append(titles);

        // 需求：原"就绪"胶囊只有一个灰点 + 四个字，观感单薄。这里重做为
        // 「状态图标 + 主状态 + 分隔线 + 上下文说明」的紧凑状态条，颜色由状态联动
        // （见 RefreshStatusPill），既能一眼看出结论，也能看到结论的依据
        statusPill_ = Border();
        statusPill_.CornerRadius(CornerRadius{ tok::kRadiusSm, tok::kRadiusSm, tok::kRadiusSm, tok::kRadiusSm });
        statusPill_.Padding(Thickness{ 10, 4, 12, 4 });
        statusPill_.Background(tok::Brush(tok::kSunken));
        statusPill_.BorderBrush(tok::Brush(tok::kBorder));
        statusPill_.BorderThickness(Thickness{ 1, 1, 1, 1 });
        statusPill_.VerticalAlignment(VerticalAlignment::Center);
        statusPill_.Margin(Thickness{ tok::kSpaceLg, 0, 0, 0 });
        tok::SetToolTip(statusPill_, L"当前运行状态与判定依据");
        StackPanel statusContent;
        statusContent.Orientation(Orientation::Horizontal);
        statusContent.Spacing(6);
        statusIcon_ = tok::Icon(tok::kGlyphTimer, 12, tok::kTextSecondary);
        statusText_ = tok::Text(L"正在启动", tok::kFontCaption, tok::kTextPrimary, tok::kWeightSemiBold);
        statusText_.VerticalAlignment(VerticalAlignment::Center);
        statusDivider_ = Border();
        statusDivider_.Width(1);
        statusDivider_.Height(11);
        statusDivider_.Background(tok::Brush(tok::kBorderStrong));
        statusDivider_.VerticalAlignment(VerticalAlignment::Center);
        statusDetail_ = tok::Text(L"载入后端组件", tok::kFontMicro + 1, tok::kTextMuted);
        statusDetail_.VerticalAlignment(VerticalAlignment::Center);
        statusContent.Children().Append(statusIcon_);
        statusContent.Children().Append(statusText_);
        statusContent.Children().Append(statusDivider_);
        statusContent.Children().Append(statusDetail_);
        statusPill_.Child(statusContent);
        Grid::SetColumn(statusPill_, 2);
        titleBar_.Children().Append(statusPill_);

        // 右侧操作按钮
        StackPanel actions;
        actions.Orientation(Orientation::Horizontal);
        actions.Spacing(tok::kSpaceSm);
        actions.VerticalAlignment(VerticalAlignment::Center);
        Button openDirectory = ui::MakeButton(L"运行目录", tok::kGlyphFolder, ui::ButtonVariant::Ghost);
        openDirectory.Click({ this, &Shell::OnOpenWorkingDirectory });
        Button refresh = ui::MakeButton(L"刷新", tok::kGlyphRefresh, ui::ButtonVariant::Ghost);
        refresh.Click({ this, &Shell::OnRefreshClicked });
        actions.Children().Append(openDirectory);
        actions.Children().Append(refresh);
        Grid::SetColumn(actions, 4);
        titleBar_.Children().Append(actions);

        return titleBar_;
    }

    // 构建侧边栏
    Border BuildSidebar()
    {
        Border sidebar;
        sidebar.Background(tok::Brush(tok::kSidebar));
        sidebar.BorderBrush(tok::Brush(tok::kBorder));
        sidebar.BorderThickness(Thickness{ 0, 0, 1, 0 });
        sidebar.Padding(Thickness{ tok::kSpaceMd, tok::kSpaceLg, tok::kSpaceMd, tok::kSpaceMd });

        Grid layout;
        RowDefinition captionRow;
        captionRow.Height(GridLength{ 1, GridUnitType::Auto });
        layout.RowDefinitions().Append(captionRow);
        for (int index = 0; index < kPageCount; ++index)
        {
            RowDefinition itemRow;
            itemRow.Height(GridLength{ 1, GridUnitType::Auto });
            layout.RowDefinitions().Append(itemRow);
        }
        // 需求：导航下方原本是一片空白。这里插入"运行环境"摘要行（Auto），
        // 剩余空白才交给 spacer 吸收，让侧栏信息密度更合理
        // 调优：spacer 提到摘要行之前，让"运行环境 + 运行时目录"两块信息在侧栏底部
        // 连成一个整体信息块，避免两块卡片被大片空白拉开造成布局断裂感
        RowDefinition spacerRow;
        spacerRow.Height(GridLength{ 1, GridUnitType::Star });
        layout.RowDefinitions().Append(spacerRow);
        RowDefinition environmentRow;
        environmentRow.Height(GridLength{ 1, GridUnitType::Auto });
        layout.RowDefinitions().Append(environmentRow);
        RowDefinition footerRow;
        footerRow.Height(GridLength{ 1, GridUnitType::Auto });
        layout.RowDefinitions().Append(footerRow);

        TextBlock caption = tok::Text(L"控制台", tok::kFontMicro + 1, tok::kTextMuted, tok::kWeightSemiBold);
        caption.Margin(Thickness{ tok::kSpaceSm, 0, 0, tok::kSpaceSm });
        Grid::SetRow(caption, 0);
        layout.Children().Append(caption);

        // 创建四个导航项
        navItems_[0] = BuildNavItem(tok::kGlyphOverview, L"总览", L"运行状态与系统信息", 0);
        navItems_[1] = BuildNavItem(tok::kGlyphProcess, L"调试器", L"管理受控目标进程", 1);
        navItems_[2] = BuildNavItem(tok::kGlyphShield, L"防检测对抗", L"TL 运行时开关", 2);
        navItems_[3] = BuildNavItem(tok::kGlyphCheck, L"诊断与自检", L"依赖检查与界面冒烟测试", 3);
        for (int index = 0; index < kPageCount; ++index)
        {
            navItems_[index].Margin(Thickness{ 0, 0, 0, tok::kSpaceXs });
            Grid::SetRow(navItems_[index], index + 1);
            layout.Children().Append(navItems_[index]);
        }

        // 侧栏中段的"运行环境"摘要：权限、后端组件、目标进程、符号缓存与日志条数
        // 集中在这里，既填掉了原本的大片空白，也省去来回切页看状态
        StackPanel environment;
        environment.Spacing(8);
        environment.Children().Append(ui::MakeSectionLabel(L"运行环境"));
        Border environmentCard = tok::Surface(tok::kCardInset, tok::kBorder, tok::kRadiusMd, tok::kSpaceMd);
        StackPanel environmentBody;
        environmentBody.Spacing(4);
        environmentBody.Children().Append(ui::MakeStatusRow(tok::kGlyphShield, L"权限", L"检测中",
            tok::kTextMuted, &navPermValue_));
        environmentBody.Children().Append(ui::MakeStatusRow(tok::kGlyphServer, L"后端组件", L"检测中",
            tok::kTextMuted, &navBackendValue_));
        environmentBody.Children().Append(ui::MakeStatusRow(tok::kGlyphPin, L"受控目标", L"未配置",
            tok::kTextMuted, &navTargetValue_));
        environmentBody.Children().Append(ui::MakeStatusRow(tok::kGlyphSymbols, L"符号缓存", L"待命",
            tok::kTextMuted, &navSymbolValue_));
        environmentBody.Children().Append(ui::MakeStatusRow(tok::kGlyphLog, L"界面日志", L"0 条",
            tok::kTextMuted, &navLogValue_));
        environmentCard.Child(environmentBody);
        environment.Children().Append(environmentCard);
        environment.Margin(Thickness{ 0, 0, 0, tok::kSpaceSm });
        Grid::SetRow(environment, kPageCount + 2);
        layout.Children().Append(environment);

        // 底部运行时目录信息
        StackPanel footer;
        footer.Spacing(tok::kSpaceSm);
        Border footerCard = tok::Surface(tok::kCardInset, tok::kBorder, tok::kRadiusMd, tok::kSpaceMd);
        StackPanel footerBody;
        footerBody.Spacing(8);
        footerBody.Children().Append(tok::Text(L"运行时目录", tok::kFontMicro + 1, tok::kTextMuted, tok::kWeightMedium));
        TextBlock directory = tok::Mono(hstring(applicationDirectory_), tok::kFontMicro + 1, tok::kTextSecondary);
        directory.TextTrimming(TextTrimming::CharacterEllipsis);
        tok::SetToolTip(directory, hstring(applicationDirectory_));
        footerBody.Children().Append(directory);
        footerBody.Children().Append(tok::Text(L"管理员权限下运行可获得完整驱动能力", tok::kFontMicro, tok::kTextMuted));
        footerCard.Child(footerBody);
        footer.Children().Append(footerCard);
        Grid::SetRow(footer, kPageCount + 3);
        layout.Children().Append(footer);

        sidebar.Child(layout);
        return sidebar;
    }

    // 构建导航项
    Border BuildNavItem(const hstring& glyph, const hstring& label, const hstring& detail, const int index)
    {
        Border item;
        item.Height(tok::kNavItemHeight);
        item.CornerRadius(CornerRadius{ tok::kRadiusMd, tok::kRadiusMd, tok::kRadiusMd, tok::kRadiusMd });
        item.Padding(Thickness{ tok::kSpaceSm, 0, tok::kSpaceSm, 0 });
        item.Background(tok::Brush(tok::kSidebar));
        tok::SetToolTip(item, detail);

        Grid row;
        ColumnDefinition barColumn;
        barColumn.Width(GridLength{ 3 });
        row.ColumnDefinitions().Append(barColumn);
        ColumnDefinition iconColumn;
        iconColumn.Width(GridLength{ 22 });
        row.ColumnDefinitions().Append(iconColumn);
        ColumnDefinition labelColumn;
        labelColumn.Width(GridLength{ 1, GridUnitType::Star });
        row.ColumnDefinitions().Append(labelColumn);

        // 左侧指示条
        Border bar = Border();
        bar.Width(3);
        bar.Height(14);
        bar.CornerRadius(CornerRadius{ 2, 2, 2, 2 });
        bar.Background(tok::Brush(tok::kSidebar));
        bar.VerticalAlignment(VerticalAlignment::Center);
        navBars_[index] = bar;
        Grid::SetColumn(bar, 0);
        row.Children().Append(bar);

        // 图标
        FontIcon icon = tok::Icon(glyph, 14, tok::kTextSecondary);
        icon.HorizontalAlignment(HorizontalAlignment::Left);
        navIcons_[index] = icon;
        Grid::SetColumn(icon, 1);
        row.Children().Append(icon);

        // 文本
        TextBlock text = tok::Text(label, tok::kFontBody, tok::kTextSecondary);
        text.VerticalAlignment(VerticalAlignment::Center);
        text.Margin(Thickness{ tok::kSpaceSm, 0, 0, 0 });
        navLabels_[index] = text;
        Grid::SetColumn(text, 2);
        row.Children().Append(text);

        item.Child(row);
        item.Tag(box_value(index));
        item.Tapped([this, index](const IInspectable&, const xaml_input::TappedRoutedEventArgs&)
        {
            SelectPage(static_cast<PageId>(index));
        });
        item.PointerEntered([this, index](const IInspectable&, const xaml_input::PointerRoutedEventArgs&)
        {
            if (static_cast<int>(currentPage_) == index)
            {
                return;
            }
            navItems_[index].Background(tok::Brush(tok::kCardHover));
            navLabels_[index].Foreground(tok::Brush(tok::kTextPrimary));
        });
        item.PointerExited([this, index](const IInspectable&, const xaml_input::PointerRoutedEventArgs&)
        {
            if (static_cast<int>(currentPage_) == index)
            {
                return;
            }
            navItems_[index].Background(tok::Brush(tok::kSidebar));
            navLabels_[index].Foreground(tok::Brush(tok::kTextSecondary));
        });
        return item;
    }

    // 选择页面
    void SelectPage(const PageId page)
    {
        currentPage_ = page;
        for (int index = 0; index < kPageCount; ++index)
        {
            const bool active = (static_cast<int>(page) == index);
            navItems_[index].Background(tok::Brush(active ? tok::kAccentSoft : tok::kSidebar));
            navBars_[index].Background(tok::Brush(active ? tok::kAccent : tok::kSidebar));
            navIcons_[index].Foreground(tok::Brush(active ? tok::kAccentText : tok::kTextSecondary));
            navLabels_[index].Foreground(tok::Brush(active ? tok::kAccentText : tok::kTextSecondary));
            navLabels_[index].FontWeight(active ? tok::kWeightSemiBold : tok::kWeightRegular);
            pages_[index].Visibility(active ? Visibility::Visible : Visibility::Collapsed);
        }
    }

    // ------------------------------------------------------------ 页面：总览

    // 构建总览页面
    Grid BuildPageOverview()
    {
        // 固定布局：磁贴（Auto）/ 通栏系统信息卡（Star，吸收剩余高度）/ 调试会话（Auto）
        // 密度收敛：独立符号缓存卡与说明性步骤行已删除，符号状态并入系统信息卡，
        // 剩余高度由属性行均分留白，避免小窗口下文本互相挤压
        Grid page = MakePageRoot();
        DeclareRows(page, { false, true, false });

        // 四个状态磁贴
        Grid tiles;
        Grid::SetRow(tiles, 0);
        tiles.ColumnSpacing(tok::kSpaceLg);
        for (int index = 0; index < 4; ++index)
        {
            ColumnDefinition column;
            column.Width(GridLength{ 1, GridUnitType::Star });
            tiles.ColumnDefinitions().Append(column);
        }
        const FrameworkElement statusTile = ui::MakeStatTile(tok::kGlyphShield, tok::kAccent, tok::kAccentSoft,
            L"运行状态", L"未初始化", L"等待进入调试模式", &statusTileValue_, nullptr)
            .as<FrameworkElement>();
        const FrameworkElement debuggerTile = ui::MakeStatTile(tok::kGlyphProcess, tok::kInfo, tok::kInfoSoft,
            L"调试器", L"0 个", L"已保存的受控目标", &debuggerTileValue_, &debuggerTileHint_)
            .as<FrameworkElement>();
        const FrameworkElement symbolTile = ui::MakeStatTile(tok::kGlyphSymbols, tok::kSuccess, tok::kSuccessSoft,
            L"符号缓存", L"待命", L"等待后端上报", &symbolTileValue_, &symbolTileHint_)
            .as<FrameworkElement>();
        const FrameworkElement logTile = ui::MakeStatTile(tok::kGlyphLog, tok::kWarning, tok::kWarningSoft,
            L"界面日志", L"0 条", L"", &logTileValue_, nullptr)
            .as<FrameworkElement>();
        Grid::SetColumn(statusTile, 0);
        Grid::SetColumn(debuggerTile, 1);
        Grid::SetColumn(symbolTile, 2);
        Grid::SetColumn(logTile, 3);
        tiles.Children().Append(statusTile);
        tiles.Children().Append(debuggerTile);
        tiles.Children().Append(symbolTile);
        tiles.Children().Append(logTile);
        page.Children().Append(tiles);

        // 系统信息通栏卡片：六项关键信息（含符号状态）排成两列三行，Star 行把卡片
        // 剩余高度转化为行间留白；符号准备进度环挂在标题行右侧
        ui::CardParts systemCard = ui::MakeSectionCard(L"系统信息", L"", tok::kGlyphServer);
        systemCard.Actions.Children().Append(symbolProgress_ = ProgressRing());
        symbolProgress_.IsActive(false);
        symbolProgress_.Width(18);
        symbolProgress_.Height(18);
        symbolProgress_.Visibility(Visibility::Collapsed);

        Grid properties;
        properties.RowSpacing(tok::kSpaceSm);
        properties.ColumnSpacing(tok::kSpaceXl);
        DeclareRows(properties, { true, true, true });
        for (int index = 0; index < 2; ++index)
        {
            ColumnDefinition column;
            column.Width(GridLength{ 1, GridUnitType::Star });
            properties.ColumnDefinitions().Append(column);
        }
        FrameworkElement propertyRows[] = {
            ui::MakePropertyRow(L"系统名称", hstring(controller_.SystemName()), false, &sysNameValue_).as<FrameworkElement>(),
            ui::MakePropertyRow(L"内核版本", hstring(controller_.SystemVersion()), false, &sysKernelValue_).as<FrameworkElement>(),
            ui::MakePropertyRow(L"处理器", hstring(controller_.CpuName()), false, &sysCpuValue_).as<FrameworkElement>(),
            ui::MakePropertyRow(L"物理内存", hstring(controller_.MemoryText()), false, &sysMemoryValue_).as<FrameworkElement>(),
            ui::MakePropertyRow(L"运行架构", hstring(controller_.ArchText()), false, &sysArchValue_).as<FrameworkElement>(),
            ui::MakePropertyRow(L"符号状态", L"等待后端上报符号准备状态", false, &symbolDetail_).as<FrameworkElement>(),
        };
        for (int index = 0; index < 6; ++index)
        {
            Grid::SetColumn(propertyRows[index], index % 2);
            Grid::SetRow(propertyRows[index], index / 2);
            properties.Children().Append(propertyRows[index]);
        }
        systemCard.Body.Children().Append(properties);
        Grid::SetRow(systemCard.Root, 1);
        page.Children().Append(systemCard.Root);

        // 调试会话卡片
        ui::CardParts initCard = ui::MakeSectionCard(L"调试会话", L"", tok::kGlyphPlay,
            tok::kAccent, tok::kAccentSoft);
        initializeButton_ = ui::MakeButton(L"进入 VT 调试模式", tok::kGlyphPlay, ui::ButtonVariant::Primary,
            tok::kFontBody);
        initializeButton_.Click({ this, &Shell::OnInitializeClicked });
        initCard.Actions.Children().Append(initializeButton_);
        initStateText_ = tok::TextWrapped(L"点击右侧按钮初始化。初始化期间界面保持可用，进度与错误会写入底部日志。",
            tok::kFontCaption - 0.5, tok::kTextSecondary);
        initCard.Body.Children().Append(initStateText_);

        // 错误提示横幅
        initErrorBanner_ = tok::Surface(tok::kDangerSoft, tok::kDanger, tok::kRadiusMd, tok::kSpaceMd);
        initErrorBanner_.Visibility(Visibility::Collapsed);
        StackPanel errorRow;
        errorRow.Orientation(Orientation::Horizontal);
        errorRow.Spacing(tok::kSpaceSm);
        errorRow.Children().Append(tok::Icon(tok::kGlyphError, 15, tok::kDanger));
        initErrorText_ = tok::TextWrapped(L"", tok::kFontCaption - 0.5, tok::kDanger);
        initErrorText_.VerticalAlignment(VerticalAlignment::Center);
        errorRow.Children().Append(initErrorText_);
        initErrorBanner_.Child(errorRow);
        initCard.Body.Children().Append(initErrorBanner_);
        Grid::SetRow(initCard.Root, 2);
        page.Children().Append(initCard.Root);

        return page;
    }

    // ---------------------------------------------------------- 页面：调试器

    // 构建调试器管理页面
    Grid BuildPageDebuggers()
    {
        // 固定布局：左列是弹性的调试器列表卡片，右列纵向排布目标锁定与使用流程，
        // 整页在窗口内一次铺满，不产生滚动
        Grid page = MakePageRoot();
        DeclareRows(page, { true });

        Grid columns;
        columns.ColumnSpacing(tok::kSpaceLg);
        ColumnDefinition listColumn;
        listColumn.Width(GridLength{ 3, GridUnitType::Star });
        columns.ColumnDefinitions().Append(listColumn);
        ColumnDefinition sideColumn;
        sideColumn.Width(GridLength{ 2, GridUnitType::Star });
        columns.ColumnDefinitions().Append(sideColumn);

        // 调试器列表用定制卡片：头部 / 工具行 / 沉底式列表区 / 状态提示四行结构
        // 列表区吃掉卡片全部剩余高度，空状态居中覆盖其上，空卡不再显得空荡；
        // 按钮不放卡片头部（窄头会被按钮挤爆标题列），放列表正上方的工具行
        Border listCard = tok::Surface(tok::kCard, tok::kBorder, tok::kRadiusLg, tok::kSpaceMd);
        Grid listLayout;
        listLayout.RowSpacing(tok::kSpaceMd);
        DeclareRows(listLayout, { false, true, false });

        // 列表头部
        Grid listHeader;
        ColumnDefinition headIconColumn;
        headIconColumn.Width(GridLength{ 1, GridUnitType::Auto });
        listHeader.ColumnDefinitions().Append(headIconColumn);
        ColumnDefinition headTextColumn;
        headTextColumn.Width(GridLength{ 1, GridUnitType::Star });
        listHeader.ColumnDefinitions().Append(headTextColumn);
        Border headChip;
        headChip.Width(28);
        headChip.Height(28);
        headChip.Background(tok::Brush(tok::kAccentSoft));
        headChip.CornerRadius(CornerRadius{ tok::kRadiusMd, tok::kRadiusMd, tok::kRadiusMd, tok::kRadiusMd });
        headChip.Child(tok::Icon(tok::kGlyphProcess, 13, tok::kAccent));
        headChip.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(headChip, 0);
        listHeader.Children().Append(headChip);
        StackPanel headText;
        headText.Spacing(3);
        headText.Margin(Thickness{ tok::kSpaceMd, 0, 0, 0 });
        headText.Children().Append(tok::Text(L"调试器列表", tok::kFontSubtitle, tok::kTextPrimary, tok::kWeightSemiBold));
        headText.Children().Append(tok::TextWrapped(L"内容保存在 Config\\DebuggerList.ini", tok::kFontCaption, tok::kTextMuted));
        Grid::SetColumn(headText, 1);
        listHeader.Children().Append(headText);
        Grid::SetRow(listHeader, 0);
        listLayout.Children().Append(listHeader);

        // 列表主体
        Grid listBody;
        listBody.RowSpacing(tok::kSpaceSm);
        DeclareRows(listBody, { false, true });
        addButton_ = ui::MakeButton(L"添加", tok::kGlyphAdd, ui::ButtonVariant::Primary);
        addButton_.Click({ this, &Shell::OnAddDebugger });
        startButton_ = ui::MakeButton(L"启动", tok::kGlyphPlay, ui::ButtonVariant::Secondary);
        startButton_.Click({ this, &Shell::OnStartDebugger });
        deleteButton_ = ui::MakeButton(L"删除", tok::kGlyphRemove, ui::ButtonVariant::Danger);
        deleteButton_.Click({ this, &Shell::OnDeleteDebugger });
        StackPanel listToolbar;
        listToolbar.Orientation(Orientation::Horizontal);
        listToolbar.Spacing(tok::kSpaceSm);
        listToolbar.Children().Append(addButton_);
        listToolbar.Children().Append(startButton_);
        listToolbar.Children().Append(deleteButton_);
        Grid::SetRow(listToolbar, 0);
        listBody.Children().Append(listToolbar);

        // 沉底式列表容器：内嵌的凹槽承载列表，空状态居中覆盖其上
        Border listBox = tok::Surface(tok::kCardInset, tok::kBorder, tok::kRadiusMd, 0);
        Grid listBoxHost;
        debuggerList_ = ListView();
        debuggerList_.SelectionMode(ListViewSelectionMode::Single);
        debuggerList_.Background(tok::Brush(tok::Rgb(0, 0, 0, 0)));
        debuggerList_.BorderThickness(Thickness{ 0, 0, 0, 0 });
        debuggerList_.Padding(Thickness{ 6, 6, 6, 6 });
        debuggerList_.SelectionChanged({ this, &Shell::OnDebuggerSelectionChanged });
        tok::SetToolTip(debuggerList_, L"添加可执行文件后在此选择目标并启动");
        listBoxHost.Children().Append(debuggerList_);

        debuggerEmpty_ = ui::MakeEmptyState(tok::kGlyphProcess, L"还没有可用的调试器",
            L"点击上方「添加」选择可执行文件，路径会写入 Config\\DebuggerList.ini。", nullptr);
        debuggerEmpty_.Visibility(Visibility::Collapsed);
        listBoxHost.Children().Append(debuggerEmpty_);
        listBox.Child(listBoxHost);
        Grid::SetRow(listBox, 1);
        listBody.Children().Append(listBox);
        Grid::SetRow(listBody, 1);
        listLayout.Children().Append(listBody);

        debuggerHint_ = tok::Text(L"流程：添加调试器 → 启动 → 切到目标窗口锁定前台进程", tok::kFontCaption - 0.5, tok::kTextMuted);
        debuggerHint_.TextTrimming(TextTrimming::CharacterEllipsis);
        Grid::SetRow(debuggerHint_, 2);
        listLayout.Children().Append(debuggerHint_);

        listCard.Child(listLayout);
        Grid::SetColumn(listCard, 0);
        columns.Children().Append(listCard);

        // 右侧面板
        StackPanel side;
        side.Spacing(tok::kSpaceMd);
        Grid::SetColumn(side, 1);
        columns.Children().Append(side);

        // 前台目标卡片
        ui::CardParts targetCard = ui::MakeSectionCard(L"前台目标", L"锁定后防检测对抗才能作用于目标进程", tok::kGlyphShield,
            tok::kWarning, tok::kWarningSoft);
        targetButton_ = ui::MakeButton(L"锁定当前前台窗口", tok::kGlyphPin, ui::ButtonVariant::Secondary);
        targetButton_.Click({ this, &Shell::OnSelectTarget });
        targetButton_.HorizontalAlignment(HorizontalAlignment::Stretch);
        targetCard.Body.Children().Append(targetButton_);
        targetText_ = tok::TextWrapped(L"尚未锁定目标进程。请先切换到目标窗口，再点击上方按钮。",
            tok::kFontCaption - 0.5, tok::kTextSecondary);
        targetCard.Body.Children().Append(targetText_);
        side.Children().Append(targetCard.Root);

        // 使用流程卡片
        ui::CardParts flowCard = ui::MakeSectionCard(L"使用流程", L"按顺序执行可获得稳定结果", tok::kGlyphTimer);
        flowCard.Body.Children().Append(ui::MakeStepRow(1, L"添加调试器", L"选择受控可执行文件，重复路径会被自动忽略"));
        flowCard.Body.Children().Append(ui::MakeStepRow(2, L"启动调试器", L"以当前用户身份拉起目标，需要管理员权限"));
        flowCard.Body.Children().Append(ui::MakeStepRow(3, L"锁定前台目标", L"锁定后切换到防检测对抗页开启开关"));
        side.Children().Append(flowCard.Root);

        page.Children().Append(columns);

        return page;
    }

    // ------------------------------------------------------ 页面：防检测对抗

    // 构建防检测对抗页面
    Grid BuildPageTl()
    {
        // 固定布局：左列是对抗开关（弹性吸收剩余高度），右列是推荐步骤与风险提示
        Grid page = MakePageRoot();
        DeclareRows(page, { true });

        Grid columns;
        columns.ColumnSpacing(tok::kSpaceLg);
        ColumnDefinition tlColumn;
        tlColumn.Width(GridLength{ 5, GridUnitType::Star });
        columns.ColumnDefinitions().Append(tlColumn);
        ColumnDefinition stepColumn;
        stepColumn.Width(GridLength{ 3, GridUnitType::Star });
        columns.ColumnDefinitions().Append(stepColumn);
        page.Children().Append(columns);

        // TL 运行时对抗卡片
        ui::CardParts tlCard = ui::MakeSectionCard(L"TL 运行时对抗", L"开关会即时写入 Config\\Config.ini", tok::kGlyphShield,
            tok::kAccent, tok::kAccentSoft);
        tlSummaryPill_ = tok::Pill(L"全部停用", tok::kTextSecondary, tok::kSunken, tok::kFontMicro + 0.5);
        tlCard.Actions.Children().Append(tlSummaryPill_);

        tlCard.Body.Children().Append(ui::MakeToggleRow(tok::kGlyphShield, L"启用定制化对抗",
            L"总开关。关闭时以下两项不生效。", tok::kAccent, &tlEnabledSwitch_));
        tlCard.Body.Children().Append(ui::MakeToggleRow(tok::kGlyphTimer, L"处理 GetTickCount 检测",
            L"拦截目标对运行时间的探测，降低时间差检测命中率。", tok::kInfo, &tlGetTickSwitch_));
        tlCard.Body.Children().Append(ui::MakeToggleRow(tok::kGlyphProcess, L"阻止游戏恢复线程",
            L"阻止目标重新恢复被挂起的线程，保持调试暂停状态。", tok::kWarning, &tlBlockResumeSwitch_));
        tlEnabledSwitch_.Toggled({ this, &Shell::OnTlToggled });
        tlGetTickSwitch_.Toggled({ this, &Shell::OnTlToggled });
        tlBlockResumeSwitch_.Toggled({ this, &Shell::OnTlToggled });

        tlStateText_ = tok::TextWrapped(L"当前未启用任何对抗项。", tok::kFontCaption - 0.5, tok::kTextSecondary);
        tlCard.Body.Children().Append(tlStateText_);
        Grid::SetColumn(tlCard.Root, 0);
        columns.Children().Append(tlCard.Root);

        // 推荐步骤卡片
        ui::CardParts stepCard = ui::MakeSectionCard(L"推荐步骤", L"基于目标版本的固定偏移，需按序验证", tok::kGlyphTimer);
        stepCard.Body.Children().Append(ui::MakeStepRow(1, L"锁定目标", L"在调试器页确认目标已锁定且在运行"));
        stepCard.Body.Children().Append(ui::MakeStepRow(2, L"开启总开关", L"先启用定制化对抗，再逐项打开子开关"));
        stepCard.Body.Children().Append(ui::MakeStepRow(3, L"观察日志", L"切换开关后立刻关注底部日志中的偏移匹配结果"));
        stepCard.Body.Children().Append(ui::MakeStepRow(4, L"回退验证", L"缺少命中记录时关闭对应开关，避免影响目标稳定性"));
        Border warning = tok::Surface(tok::kWarningSoft, tok::kWarning, tok::kRadiusMd, tok::kSpaceMd);
        StackPanel warningRow;
        warningRow.Orientation(Orientation::Horizontal);
        warningRow.Spacing(tok::kSpaceSm);
        warningRow.Children().Append(tok::Icon(tok::kGlyphWarning, 15, tok::kWarning));
        TextBlock warningText = tok::TextWrapped(
            L"对抗逻辑依赖目标版本与固定偏移，目标更新后可能失效；失效时请先关闭全部开关并保留日志。",
            tok::kFontCaption - 0.5, tok::kWarning);
        warningText.VerticalAlignment(VerticalAlignment::Center);
        warningRow.Children().Append(warningText);
        warning.Child(warningRow);
        stepCard.Body.Children().Append(warning);
        Grid::SetColumn(stepCard.Root, 1);
        columns.Children().Append(stepCard.Root);

        return page;
    }

    // ------------------------------------------------------ 页面：诊断与自检

    // 构建诊断与自检页面
    Grid BuildPageDiagnostics()
    {
        // 固定布局：左列（2/3）是运行时依赖明细，右列（1/3）放紧凑的日志会话卡与
        // 冒烟自检卡；长路径不再摆进右列（窄列必截断），改由"打开日志目录"按钮的
        // 悬浮提示承载完整路径
        Grid page = MakePageRoot();
        DeclareRows(page, { true });

        Grid columns;
        columns.ColumnSpacing(tok::kSpaceLg);
        ColumnDefinition checkColumn;
        checkColumn.Width(GridLength{ 2, GridUnitType::Star });
        columns.ColumnDefinitions().Append(checkColumn);
        ColumnDefinition sideColumn;
        sideColumn.Width(GridLength{ 1, GridUnitType::Star });
        columns.ColumnDefinitions().Append(sideColumn);
        page.Children().Append(columns);

        // 运行时依赖检查卡片
        ui::CardParts checkCard = ui::MakeSectionCard(L"运行时依赖", L"按当前运行目录实时检查", tok::kGlyphCheck,
            tok::kSuccess, tok::kSuccessSoft);
        Button recheck = ui::MakeButton(L"重新检查", tok::kGlyphRefresh, ui::ButtonVariant::Ghost);
        recheck.Click({ this, &Shell::OnRefreshDiagnosticsClicked });
        checkCard.Actions.Children().Append(recheck);
        diagnosticsSummary_ = tok::Text(L"尚未检查", tok::kFontCaption, tok::kTextSecondary, tok::kWeightMedium);
        checkCard.Body.Children().Append(diagnosticsSummary_);
        diagnosticsList_ = StackPanel();
        diagnosticsList_.Spacing(tok::kSpaceXs);
        checkCard.Body.Children().Append(diagnosticsList_);
        Grid::SetColumn(checkCard.Root, 0);
        columns.Children().Append(checkCard.Root);

        // 右列用 Grid：两行都声明为 Star（有限高度测量）。经验证卡片放在 Auto 行
        // 或 StackPanel（无限高度测量）时头部会被撑坏，Star 行与其他页面一致
        Grid side;
        side.RowSpacing(tok::kSpaceMd);
        Grid::SetColumn(side, 1);
        DeclareRows(side, { true, true });
        columns.Children().Append(side);

        // 日志与会话卡片
        ui::CardParts logCard = ui::MakeSectionCard(L"日志与会话", L"日志同时写入磁盘，清空界面不影响文件", tok::kGlyphLog,
            tok::kWarning, tok::kWarningSoft);
        // 窄卡（右列约 170px）头部放不下按钮：会把标题文本列挤到 0 宽、副标题逐字
        // 竖排撑爆整卡布局，因此按钮放主体内并通栏拉伸；完整路径放悬浮提示
        Button openLog = ui::MakeButton(L"打开日志目录", tok::kGlyphFolder, ui::ButtonVariant::Secondary);
        openLog.Click({ this, &Shell::OnOpenLogDirectory });
        openLog.HorizontalAlignment(HorizontalAlignment::Stretch);
        tok::SetToolTip(openLog, hstring(applicationDirectory_ + L"\\Log"));
        logCard.Body.Children().Append(openLog);
        logCard.Body.Children().Append(ui::MakePropertyRow(L"界面日志条数", L"0 条", false, &logCountText_));
        Grid::SetRow(logCard.Root, 0);
        side.Children().Append(logCard.Root);

        // 界面冒烟自检卡片
        ui::CardParts smokeCard = ui::MakeSectionCard(L"界面冒烟自检", L"覆盖导航、日志筛选、面板拖拽与依赖检查",
            tok::kGlyphSymbols, tok::kInfo, tok::kInfoSoft);
        smokeButton_ = ui::MakeButton(L"运行界面自检", tok::kGlyphCheck, ui::ButtonVariant::Secondary);
        smokeButton_.Click({ this, &Shell::OnRunUiSmokeTest });
        smokeButton_.HorizontalAlignment(HorizontalAlignment::Stretch);
        smokeCard.Body.Children().Append(smokeButton_);
        smokeCard.Body.Children().Append(tok::TextWrapped(
            L"在界面内执行一组只读检查，结果写入日志面板。",
            tok::kFontCaption - 0.5, tok::kTextSecondary));
        smokeResult_ = tok::TextWrapped(L"尚未运行自检。", tok::kFontCaption - 0.5, tok::kTextMuted);
        smokeCard.Body.Children().Append(smokeResult_);
        Grid::SetRow(smokeCard.Root, 1);
        side.Children().Append(smokeCard.Root);

        return page;
    }

    // ------------------------------------------------------------ 状态刷新

    // 刷新系统信息
    void RefreshSystemInfo()
    {
        // 需求：系统信息必须快速、稳定地显示真实值，不允许长期停留在"读取中"
        // 控制器已改为本地注册表 + Win32 API 同步采集（毫秒级），这里只做一次赋值，
        // 并对每个字段做非空兜底，界面上永远不会出现未确定文案
        if (sysNameValue_ != nullptr)
        {
            sysNameValue_.Text(NonEmptyOr(controller_.SystemName(), L"未知系统"));
        }
        if (sysKernelValue_ != nullptr)
        {
            sysKernelValue_.Text(NonEmptyOr(controller_.SystemVersion(), L"未知版本"));
        }
        if (sysCpuValue_ != nullptr)
        {
            sysCpuValue_.Text(NonEmptyOr(controller_.CpuName(), L"未知处理器"));
        }
        if (sysMemoryValue_ != nullptr)
        {
            sysMemoryValue_.Text(NonEmptyOr(controller_.MemoryText(), L"未知"));
        }
        if (sysArchValue_ != nullptr)
        {
            sysArchValue_.Text(NonEmptyOr(controller_.ArchText(), L"未知"));
        }

        suppressTlEvents_ = true;
        tlEnabledSwitch_.IsOn(controller_.TlEnabled());
        tlGetTickSwitch_.IsOn(controller_.TlGetTickCount());
        tlBlockResumeSwitch_.IsOn(controller_.TlBlockResumeThread());
        suppressTlEvents_ = false;
        RefreshTlSummary();
    }

    // 刷新 TL 摘要信息
    void RefreshTlSummary()
    {
        const bool enabled = tlEnabledSwitch_.IsOn();
        std::wstring summary = enabled ? L"已启用" : L"已停用";
        int subCount = 0;
        if (enabled && tlGetTickSwitch_.IsOn())
        {
            summary += L" · GetTickCount 处理";
            ++subCount;
        }
        if (enabled && tlBlockResumeSwitch_.IsOn())
        {
            summary += L" · 阻止线程恢复";
            ++subCount;
        }
        if (enabled && subCount == 0)
        {
            summary += L"（未选择子项）";
        }

        const Color foreground = enabled ? tok::kAccentText : tok::kTextSecondary;
        const Color background = enabled ? tok::kAccentSoft : tok::kSunken;
        if (const TextBlock label = tlSummaryPill_.Child().try_as<TextBlock>())
        {
            label.Text(hstring(enabled ? (subCount > 0 ? L"已启用" : L"待选子项") : L"全部停用"));
            label.Foreground(tok::Brush(foreground));
        }
        tlSummaryPill_.Background(tok::Brush(background));
        tlSummaryPill_.BorderBrush(tok::Brush(enabled ? tok::kAccent : tok::kBorder));
        tlSummaryPill_.BorderThickness(Thickness{ 1, 1, 1, 1 });
        tlStateText_.Text(hstring(summary));
    }

    // 刷新调试器列表
    void RefreshDebuggerList()
    {
        debuggerList_.Items().Clear();
        const auto& entries = controller_.Debuggers();
        for (size_t index = 0; index < entries.size(); ++index)
        {
            debuggerList_.Items().Append(BuildDebuggerRow(entries[index], index));
        }
        const bool hasItems = !entries.empty();
        debuggerList_.Visibility(hasItems ? Visibility::Visible : Visibility::Collapsed);
        debuggerEmpty_.Visibility(hasItems ? Visibility::Collapsed : Visibility::Visible);
        debuggerTileValue_.Text(hstring(std::to_wstring(entries.size()) + L" 个"));
        debuggerTileHint_.Text(hstring(hasItems ? L"选中一项后可启动" : L"点击「添加」选择可执行文件"));
        RefreshDebuggerSelection();
    }

    // 构建调试器列表项
    UIElement BuildDebuggerRow(const unrealdbg_native::DebuggerEntry& entry, const size_t index)
    {
        Border card = tok::Surface(tok::kCardInset, tok::kBorder, tok::kRadiusMd, tok::kSpaceMd);
        Grid row;
        ColumnDefinition indexColumn;
        indexColumn.Width(GridLength{ 24 });
        row.ColumnDefinitions().Append(indexColumn);
        ColumnDefinition textColumn;
        textColumn.Width(GridLength{ 1, GridUnitType::Star });
        row.ColumnDefinitions().Append(textColumn);
        ColumnDefinition badgeColumn;
        badgeColumn.Width(GridLength{ 1, GridUnitType::Auto });
        row.ColumnDefinitions().Append(badgeColumn);

        TextBlock order = tok::Text(hstring(std::to_wstring(index + 1)), tok::kFontCaption, tok::kTextMuted, tok::kWeightSemiBold);
        order.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(order, 0);
        row.Children().Append(order);

        StackPanel texts;
        texts.Spacing(5);
        texts.Children().Append(tok::Text(hstring(entry.name), tok::kFontBody, tok::kTextPrimary, tok::kWeightMedium));
        TextBlock path = tok::Mono(hstring(entry.path), tok::kFontMicro + 1, tok::kTextMuted);
        path.TextTrimming(TextTrimming::CharacterEllipsis);
        texts.Children().Append(path);
        tok::SetToolTip(texts, hstring(entry.path));
        Grid::SetColumn(texts, 1);
        row.Children().Append(texts);

        Border badge = ui::MakeBadge(L"已保存", tok::kInfo, tok::kInfoSoft);
        badge.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(badge, 2);
        row.Children().Append(badge);

        card.Child(row);
        ListViewItem container;
        container.Content(card);
        container.Padding(Thickness{ 0, 0, 0, 0 });
        container.MinHeight(0);
        container.Margin(Thickness{ 0, 0, 0, tok::kSpaceSm });
        container.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        container.Background(tok::Brush(tok::Rgb(0, 0, 0, 0)));
        container.BorderThickness(Thickness{ 0, 0, 0, 0 });
        return container;
    }

    // 调试器选择变更事件
    void OnDebuggerSelectionChanged(const IInspectable&, const SelectionChangedEventArgs&)
    {
        RefreshDebuggerSelection();
    }

    // 刷新调试器选择状态
    void RefreshDebuggerSelection()
    {
        const int32_t selected = debuggerList_.SelectedIndex();
        const bool hasSelection = selected >= 0;
        SetButtonEnabled(startButton_, hasSelection);
        SetButtonEnabled(deleteButton_, hasSelection);
        if (hasSelection)
        {
            debuggerHint_.Text(hstring(L"已选中第 " + std::to_wstring(selected + 1) + L" 项，可执行启动或删除"));
        }
        else
        {
            debuggerHint_.Text(controller_.Debuggers().empty()
                ? L"列表为空：请先添加受控可执行文件"
                : L"请选择列表中的一项后再执行启动或删除");
        }
    }

    // 刷新状态指示器
    void RefreshStatusPill()
    {
        // 需求 1：标题栏状态标识重做。颜色 + 图标 + 主结论 + 判定依据四者联动，
        // 不必切到诊断页就能看出"就绪"依据什么
        Color color = tok::kSuccess;
        Color background = tok::kSuccessSoft;
        const wchar_t* glyph = tok::kGlyphCheck;
        hstring text = L"就绪";
        hstring detail = L"运行目录与后端组件正常";

        if (!backendReady_)
        {
            color = tok::kDanger;
            background = tok::kDangerSoft;
            glyph = tok::kGlyphError;
            text = L"后端未就绪";
            detail = L"未找到 bin\\UnrealDbgProDll.dll";
        }
        else if (controller_.IsInitializing())
        {
            color = tok::kWarning;
            background = tok::kWarningSoft;
            glyph = tok::kGlyphTimer;
            text = L"初始化中";
            detail = L"正在建立驱动通道";
        }
        else if (controller_.IsInitialized())
        {
            color = tok::kSuccess;
            background = tok::kSuccessSoft;
            glyph = tok::kGlyphShield;
            text = L"已进入调试模式";
            detail = L"驱动通道已建立";
        }
        else if (controller_.IsSymbolPreparing())
        {
            color = tok::kInfo;
            background = tok::kInfoSoft;
            glyph = tok::kGlyphSymbols;
            text = L"符号准备中";
            detail = L"后台校验 / 下载符号缓存";
        }

        if (statusIcon_ != nullptr)
        {
            statusIcon_.Glyph(glyph);
            statusIcon_.Foreground(tok::Brush(color));
        }
        if (statusDetail_ != nullptr && statusDetail_.Text() != detail)
        {
            statusDetail_.Text(detail);
        }
        if (statusDivider_ != nullptr)
        {
            statusDivider_.Background(tok::Brush(color));
            statusDivider_.Opacity(0.4);
        }
        if (statusText_ != nullptr)
        {
            statusText_.Text(text);
            statusText_.Foreground(tok::Brush(color));
        }
        statusPill_.Background(tok::Brush(background));
        statusPill_.BorderBrush(tok::Brush(color));
        statusPill_.BorderThickness(Thickness{ 1, 1, 1, 1 });
    }

    // 需求 4：左侧导航下方补齐"运行环境"摘要，把权限 / 后端 / 目标 / 符号 / 日志
    // 五项关键结论固定展示在侧栏，填掉导航下方的空白区域。
    // includePrivilege 为 false 时跳过权限探测（令牌查询有系统调用开销），
    // 供高频刷新路径复用。
    void RefreshNavEnvironment(const bool includePrivilege = true)
    {
        if (navPermValue_ != nullptr && includePrivilege)
        {
            const bool elevated = unrealdbg_native::IsProcessElevated();
            navPermValue_.Text(elevated ? L"管理员" : L"标准用户");
            navPermValue_.Foreground(tok::Brush(elevated ? tok::kSuccess : tok::kWarning));
        }

        if (navBackendValue_ != nullptr)
        {
            navBackendValue_.Text(backendReady_ ? L"已就绪" : L"缺失");
            navBackendValue_.Foreground(tok::Brush(backendReady_ ? tok::kSuccess : tok::kDanger));
        }

        if (navTargetValue_ != nullptr)
        {
            const size_t count = controller_.Debuggers().size();
            navTargetValue_.Text(count == 0 ? hstring(L"未配置")
                                            : hstring(std::to_wstring(count) + L" 个已登记"));
            navTargetValue_.Foreground(tok::Brush(count == 0 ? tok::kTextSecondary : tok::kTextPrimary));
        }

        if (navSymbolValue_ != nullptr)
        {
            Color color = tok::kTextSecondary;
            hstring value = L"待命";
            if (controller_.IsSymbolPreparationFailed())
            {
                color = tok::kDanger;
                value = L"准备失败";
            }
            else if (controller_.IsSymbolPreparing())
            {
                color = tok::kInfo;
                value = L"准备中";
            }
            else if (controller_.IsSymbolCacheReady())
            {
                color = tok::kSuccess;
                value = L"已就绪";
            }
            navSymbolValue_.Text(value);
            navSymbolValue_.Foreground(tok::Brush(color));
        }

        if (navLogValue_ != nullptr)
        {
            navLogValue_.Text(hstring(std::to_wstring(panel_.TotalCount()) + L" 条"));
        }
    }

    void RefreshLiveState()
    {
        const bool initializing = controller_.IsInitializing();
        const bool initialized = controller_.IsInitialized();
        const bool preparing = controller_.IsSymbolPreparing();
        const bool cacheReady = controller_.IsSymbolCacheReady();
        const bool failed = controller_.IsSymbolPreparationFailed();

        const hstring symbolStatus = hstring(controller_.SymbolPreparationStatus());
        symbolDetail_.Text(symbolStatus);
        // 状态文字跟随语义变色：失败红 / 准备中琥珀 / 就绪绿 / 待命灰蓝。
        symbolDetail_.Foreground(tok::Brush(failed ? tok::kDanger : preparing ? tok::kWarning
            : cacheReady ? tok::kSuccess : tok::kTextSecondary));
        // 进度环只在"确实在后台准备符号"时可见并旋转，其余状态一律收起，
        // 避免界面出现"永远转圈的加载中"。
        if (symbolProgress_ != nullptr)
        {
            symbolProgress_.IsActive(preparing);
            symbolProgress_.Visibility(preparing ? Visibility::Visible : Visibility::Collapsed);
        }
        symbolTileHint_.Text(symbolStatus);

        if (failed)
        {
            symbolTileValue_.Text(L"准备失败");
            symbolTileValue_.Foreground(tok::Brush(tok::kDanger));
        }
        else if (preparing)
        {
            symbolTileValue_.Text(L"准备中");
            symbolTileValue_.Foreground(tok::Brush(tok::kWarning));
        }
        else
        {
            symbolTileValue_.Text(cacheReady ? L"已就绪" : L"待命");
            symbolTileValue_.Foreground(tok::Brush(tok::kSuccess));
        }

        if (initialized)
        {
            SetButtonCaption(initializeButton_, L"已进入 VT 调试模式");
            SetButtonEnabled(initializeButton_, false);
        }
        else if (initializing)
        {
            SetButtonCaption(initializeButton_, L"正在初始化…");
            SetButtonEnabled(initializeButton_, false);
        }
        else
        {
            SetButtonCaption(initializeButton_, L"进入 VT 调试模式");
            SetButtonEnabled(initializeButton_, true);
        }

        if (initialized)
        {
            statusTileValue_.Text(L"已就绪");
            statusTileValue_.Foreground(tok::Brush(tok::kSuccess));
            initStateText_.Text(L"调试通道已建立，可在「调试器」页拉起受控目标。");
        }
        else if (initializing)
        {
            statusTileValue_.Text(L"初始化中");
            statusTileValue_.Foreground(tok::Brush(tok::kWarning));
            initStateText_.Text(L"正在建立驱动通道，请保持窗口开启，进度会写入底部日志。");
        }
        else
        {
            statusTileValue_.Text(L"未初始化");
            statusTileValue_.Foreground(tok::Brush(tok::kAccent));
            initStateText_.Text(L"点击右侧按钮初始化。初始化期间界面保持可用，进度与错误会写入底部日志。");
        }

        const std::wstring error = controller_.LastInitializationError();
        if (!initialized && !initializing && !error.empty())
        {
            initErrorText_.Text(hstring(error));
            initErrorBanner_.Visibility(Visibility::Visible);
        }
        else
        {
            initErrorBanner_.Visibility(Visibility::Collapsed);
        }

        // 侧栏环境摘要跟随同一份状态源刷新（权限探测开销大，交给显式刷新路径）。
        RefreshNavEnvironment(false);
    }

    void AppendPendingLogs()
    {
        std::vector<unrealdbg_native::LogRecord> records;
        controller_.DrainLogs(records);
        try
        {
            if (!records.empty())
            {
                panel_.Append(records);
            }
        }
        catch (const hresult_error& error)
        {
            if (auto* sink = unrealdbg_native::GetWinUiLogSink())
            {
                sink->Error(std::wstring(L"[界面异常] 日志面板追加失败：") + error.message().c_str());
            }
        }
        const uint32_t total = panel_.TotalCount();
        logTileValue_.Text(hstring(std::to_wstring(total) + L" 条"));
        if (logCountText_ != nullptr)
        {
            logCountText_.Text(hstring(std::to_wstring(total) + L" 条"));
        }
        if (navLogValue_ != nullptr)
        {
            navLogValue_.Text(hstring(std::to_wstring(total) + L" 条"));
        }
    }

    void RefreshDiagnostics()
    {
        diagnosticsList_.Children().Clear();

        struct CheckItem
        {
            const wchar_t* label;
            const wchar_t* relative;
            bool directory;
        };
        const CheckItem items[] = {
            { L"主控 DLL", L"bin\\UnrealDbgProDll.dll", false },
            { L"内核驱动", L"bin\\VT_Driver.sys", false },
            { L"用户态 Hook", L"bin\\Hook64.dll", false },
            { L"兼容打印模块", L"bin\\AIHelper.dll", false },
            { L"驱动签名证书", L"Certificates", true },
            { L"符号缓存目录", L"Symbols", true },
            { L"运行时配置", L"Config\\Config.ini", false },
            { L"调试器清单", L"Config\\DebuggerList.ini", false },
            { L"版权信息库", L"Config\\copyright.db", false },
            { L"日志目录", L"Log", true },
        };

        int missing = 0;
        for (const CheckItem& item : items)
        {
            const std::wstring path = applicationDirectory_ + L"\\" + item.relative;
            const bool ok = item.directory
                ? unrealdbg_native::DirectoryExists(path)
                : unrealdbg_native::FileExists(path);
            if (!ok)
            {
                ++missing;
            }
            diagnosticsList_.Children().Append(ui::MakeCheckRow(hstring(item.label), hstring(path), ok, nullptr));
        }

        if (missing == 0)
        {
            diagnosticsSummary_.Text(L"全部依赖就绪");
            diagnosticsSummary_.Foreground(tok::Brush(tok::kSuccess));
        }
        else
        {
            diagnosticsSummary_.Text(hstring(L"缺少 " + std::to_wstring(missing) + L" 项，请对照构建输出目录检查"));
            diagnosticsSummary_.Foreground(tok::Brush(tok::kWarning));
        }
    }

    std::wstring SessionLogPath() const
    {
        const auto* sink = unrealdbg_native::GetWinUiLogSink();
        if (sink == nullptr)
        {
            return applicationDirectory_ + L"\\Log";
        }
        const std::wstring path = sink->SessionLogPath();
        return path.empty() ? applicationDirectory_ + L"\\Log" : path;
    }

    // -------------------------------------------------------------- 日志面板

    void ApplyLogHeight()
    {
        if (logRoot_ == nullptr)
        {
            return;
        }
        logRoot_.Height(panel_.IsFolded() ? panel_.HeaderHeight() : logHeight_);
    }

    void OnSplitterPressed(const IInspectable& sender, const xaml_input::PointerRoutedEventArgs& args)
    {
        if (panel_.IsFolded())
        {
            panel_.SetFolded(false);
            logHeight_ = (std::max)(logHeight_, tok::kLogMinHeight);
            ApplyLogHeight();
        }
        splitterDrag_ = true;
        dragStartY_ = args.GetCurrentPoint(root_).Position().Y;
        dragStartHeight_ = logHeight_;
        UIElement element = sender.try_as<UIElement>();
        if (element != nullptr)
        {
            element.CapturePointer(args.Pointer());
        }
        grip_.Background(tok::Brush(tok::kAccent));
        args.Handled(true);
    }

    void OnPointerMoved(const IInspectable&, const xaml_input::PointerRoutedEventArgs& args)
    {
        if (!splitterDrag_)
        {
            return;
        }
        const double pointerY = args.GetCurrentPoint(root_).Position().Y;
        const double maxHeight = (std::max)(tok::kLogMinHeight, root_.ActualHeight() * tok::kLogMaxRatio);
        double next = dragStartHeight_ + (dragStartY_ - pointerY);
        next = (std::min)((std::max)(next, tok::kLogMinHeight), maxHeight);
        if (std::fabs(next - logHeight_) < 0.5)
        {
            return;
        }
        logHeight_ = next;
        ApplyLogHeight();
        args.Handled(true);
    }

    void OnPointerReleased(const IInspectable&, const xaml_input::PointerRoutedEventArgs& args)
    {
        if (!splitterDrag_)
        {
            return;
        }
        splitterDrag_ = false;
        splitter_.ReleasePointerCaptures();
        grip_.Background(tok::Brush(tok::kBorderStrong));
        args.Handled(true);
    }

    void OnPointerCaptureLost(const IInspectable&, const xaml_input::PointerRoutedEventArgs&)
    {
        splitterDrag_ = false;
        grip_.Background(tok::Brush(tok::kBorderStrong));
    }

    void OnSplitterEntered(const IInspectable&, const xaml_input::PointerRoutedEventArgs&)
    {
        grip_.Background(tok::Brush(tok::kAccent));
    }

    void OnSplitterExited(const IInspectable&, const xaml_input::PointerRoutedEventArgs&)
    {
        if (!splitterDrag_)
        {
            grip_.Background(tok::Brush(tok::kBorderStrong));
        }
    }

    // ------------------------------------------------------------ 用户命令

    void OnTimer(const IInspectable&, const IInspectable&)
    {
        const wchar_t* stage = L"PollBackendLogs";
        try
        {
            controller_.PollBackendLogs();
            stage = L"AppendPendingLogs";
            AppendPendingLogs();
            stage = L"RefreshStatusPill";
            RefreshStatusPill();
            stage = L"RefreshLiveState";
            RefreshLiveState();
        }
        catch (const hresult_error& error)
        {
            // 界面刷新出错时记录阶段与原因并暂停刷新，避免异常反复重入把进程直接带走。
            if (auto* sink = unrealdbg_native::GetWinUiLogSink())
            {
                sink->Error(std::wstring(L"[界面异常] 定时刷新失败(阶段=") + stage + L")：" + error.message().c_str());
            }
            if (timer_ != nullptr)
            {
                timer_.Stop();
            }
        }
    }

    void OnInitializeClicked(const IInspectable&, const RoutedEventArgs&)
    {
        controller_.InitializeAsync();
        SetButtonCaption(initializeButton_, L"正在初始化…");
        SetButtonEnabled(initializeButton_, false);
    }

    void OnAddDebugger(const IInspectable&, const RoutedEventArgs&)
    {
        std::wstring buffer(32768, L'\0');
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hostWindow_;
        dialog.lpstrFilter = L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
        dialog.lpstrFile = buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(buffer.size());
        dialog.lpstrTitle = L"选择要受控调试的程序";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
        if (::GetOpenFileNameW(&dialog) == FALSE)
        {
            return;
        }
        const std::wstring path = buffer.c_str();
        if (path.empty())
        {
            return;
        }
        if (!controller_.AddDebugger(path))
        {
            tok::SetToolTip(addButton_, L"该路径已在列表中或无法保存，详见底部日志");
            return;
        }
        RefreshDebuggerList();
    }

    void OnDeleteDebugger(const IInspectable&, const RoutedEventArgs&)
    {
        const int32_t selected = debuggerList_.SelectedIndex();
        if (selected < 0)
        {
            return;
        }
        if (!controller_.DeleteDebugger(static_cast<size_t>(selected)))
        {
            return;
        }
        RefreshDebuggerList();
    }

    void OnStartDebugger(const IInspectable&, const RoutedEventArgs&)
    {
        const int32_t selected = debuggerList_.SelectedIndex();
        if (selected < 0)
        {
            return;
        }
        controller_.StartDebugger(static_cast<size_t>(selected));
    }

    void OnTlToggled(const IInspectable& sender, const RoutedEventArgs&)
    {
        if (suppressTlEvents_)
        {
            return;
        }
        const ToggleSwitch toggle = sender.try_as<ToggleSwitch>();
        if (toggle == nullptr)
        {
            return;
        }
        const bool value = toggle.IsOn();
        if (toggle == tlEnabledSwitch_)
        {
            controller_.SetTlEnabled(value);
        }
        else if (toggle == tlGetTickSwitch_)
        {
            controller_.SetTlGetTickCount(value);
        }
        else if (toggle == tlBlockResumeSwitch_)
        {
            controller_.SetTlBlockResumeThread(value);
        }
        RefreshTlSummary();
    }

    void OnSelectTarget(const IInspectable&, const RoutedEventArgs&)
    {
        if (controller_.SelectForegroundTarget(hostWindow_))
        {
            targetText_.Text(L"已锁定当前前台窗口，可在「防检测对抗」页启用开关。");
            targetText_.Foreground(tok::Brush(tok::kSuccess));
        }
        else
        {
            targetText_.Text(L"未能锁定前台窗口：请先激活目标程序窗口后重试，详细信息见底部日志。");
            targetText_.Foreground(tok::Brush(tok::kDanger));
        }
    }

    void OnRefreshClicked(const IInspectable&, const RoutedEventArgs&)
    {
        controller_.ReloadFromDisk();
        RefreshSystemInfo();
        RefreshDebuggerList();
        RefreshDiagnostics();
        RefreshLiveState();
        RefreshNavEnvironment();
    }

    void OnRefreshDiagnosticsClicked(const IInspectable&, const RoutedEventArgs&)
    {
        RefreshDiagnostics();
    }

    void OnOpenWorkingDirectory(const IInspectable&, const RoutedEventArgs&)
    {
        OpenInExplorer(applicationDirectory_);
    }

    void OnOpenLogDirectory(const IInspectable&, const RoutedEventArgs&)
    {
        OpenInExplorer(applicationDirectory_ + L"\\Log");
    }

    void OnClosed(const IInspectable&, const WindowEventArgs&)
    {
        if (timer_ != nullptr)
        {
            timer_.Stop();
        }
        if (smokeTimer_ != nullptr)
        {
            smokeTimer_.Stop();
        }
        if (smokeWatchdog_ != nullptr)
        {
            smokeWatchdog_.Stop();
        }
        controller_.Shutdown();
    }

    // ---------------------------------------------------------- 界面冒烟自检

    void StartUiSmokeTest()
    {
        tok::SetToolTip(smokeButton_, L"由 --ui-smoke-test 自动触发");
        smokeWatchdog_ = DispatcherTimer();
        smokeWatchdog_.Interval(std::chrono::milliseconds(15000));
        smokeWatchdog_.Tick([this](const IInspectable&, const IInspectable&)
        {
            if (smokeFinished_)
            {
                return;
            }
            smokeFinished_ = true;
            g_smokeTestExitCode = 1;
            if (auto* sink = unrealdbg_native::GetWinUiLogSink())
            {
                sink->Error(L"[UI-SMOKE] 自检超时未完成");
            }
            if (window_ != nullptr)
            {
                window_.Close();
            }
        });
        smokeWatchdog_.Start();

        smokeTimer_ = DispatcherTimer();
        smokeTimer_.Interval(std::chrono::milliseconds(1200));
        smokeTimer_.Tick([this](const IInspectable&, const IInspectable&)
        {
            smokeTimer_.Stop();
            const bool ok = RunUiChecks();
            g_smokeTestExitCode = ok ? 0 : 1;
            smokeFinished_ = true;
            if (auto* sink = unrealdbg_native::GetWinUiLogSink())
            {
                if (ok)
                {
                    sink->Info(L"[UI-SMOKE] 全部检查通过");
                }
                else
                {
                    sink->Error(L"[UI-SMOKE] 存在失败检查项");
                }
            }
            if (window_ != nullptr)
            {
                window_.Close();
            }
        });
        smokeTimer_.Start();
    }

    void OnRunUiSmokeTest(const IInspectable&, const RoutedEventArgs&)
    {
        SetButtonEnabled(smokeButton_, false);
        const bool ok = RunUiChecks();
        SetButtonEnabled(smokeButton_, true);
        smokeResult_.Text(hstring(ok ? L"最近一次自检：全部通过（明细见底部日志）" : L"最近一次自检：存在失败项（明细见底部日志）"));
        smokeResult_.Foreground(tok::Brush(ok ? tok::kSuccess : tok::kDanger));
    }

    bool RunUiChecks()
    {
        bool passed = true;
        auto report = [this, &passed](const hstring& name, const bool ok)
        {
            if (!ok)
            {
                passed = false;
            }
            unrealdbg_native::LogRecord record;
            record.level = ok ? unrealdbg_native::LogLevel::Info : unrealdbg_native::LogLevel::Error;
            record.text = std::wstring(L"[UI-SMOKE] ") + (ok ? L"通过 · " : L"失败 · ") + ToWide(name);
            std::vector<unrealdbg_native::LogRecord> single{ record };
            panel_.Append(single);
        };

        // 1. 设计令牌自洽
        const bool tokenOk = tok::kFontMicro < tok::kFontCaption && tok::kFontCaption < tok::kFontBody
            && tok::kFontBody <= tok::kFontHero && tok::kSpaceXs < tok::kSpaceSm && tok::kSpaceSm < tok::kSpaceMd
            && tok::kLogMinHeight < tok::kLogDefaultHeight && tok::kNavWidth > tok::kNavCollapsedWidth;
        report(L"设计令牌自洽（字号 / 间距 / 尺寸递增）", tokenOk);

        // 2. 导航与页面一一对应
        bool navigationOk = true;
        for (int index = 0; index < kPageCount; ++index)
        {
            SelectPage(static_cast<PageId>(index));
            for (int other = 0; other < kPageCount; ++other)
            {
                const Visibility expected = (other == index) ? Visibility::Visible : Visibility::Collapsed;
                if (pages_[other].Visibility() != expected)
                {
                    navigationOk = false;
                }
            }
            if (navItems_[index].Background().try_as<SolidColorBrush>() == nullptr)
            {
                navigationOk = false;
            }
        }
        report(L"导航切换与页面可见性一致", navigationOk);

        // 3. 日志分级筛选
        const uint32_t totalBefore = panel_.TotalCount();
        {
            std::vector<unrealdbg_native::LogRecord> samples;
            samples.push_back(unrealdbg_native::LogRecord{ unrealdbg_native::LogLevel::Debug, L"[UI-SMOKE] 调试样例" });
            samples.push_back(unrealdbg_native::LogRecord{ unrealdbg_native::LogLevel::Info, L"[UI-SMOKE] 信息样例" });
            samples.push_back(unrealdbg_native::LogRecord{ unrealdbg_native::LogLevel::Error, L"[UI-SMOKE] 错误样例" });
            panel_.Append(samples);
        }
        const uint32_t totalAfter = panel_.TotalCount();
        panel_.SetFilter(unrealdbg_ui::LogFilter::Error);
        const uint32_t errorRows = panel_.VisibleCount();
        panel_.SetFilter(unrealdbg_ui::LogFilter::Debug);
        const uint32_t debugRows = panel_.VisibleCount();
        panel_.SetFilter(unrealdbg_ui::LogFilter::All);
        const uint32_t allRows = panel_.VisibleCount();
        report(L"日志分级筛选（错误 / 调试 / 全部）",
            totalAfter == totalBefore + 3 && errorRows >= 1 && debugRows >= 1 && allRows >= errorRows && allRows >= debugRows);

        // 4. 日志面板折叠与拖拽高度
        const double expandedHeight = logHeight_;
        panel_.SetFolded(true);
        const bool foldedOk = std::fabs(logRoot_.Height() - panel_.HeaderHeight()) < 0.5;
        panel_.SetFolded(false);
        const bool expandedOk = std::fabs(logRoot_.Height() - expandedHeight) < 0.5;
        panel_.SetAutoScroll(false);
        const bool autoScrollOff = !panel_.AutoScroll();
        panel_.SetAutoScroll(true);
        report(L"日志面板折叠 / 展开 / 跟随开关", foldedOk && expandedOk && autoScrollOff && panel_.AutoScroll());

        // 5. 操作按钮禁用态
        debuggerList_.SelectedIndex(-1);
        RefreshDebuggerSelection();
        bool disabledOk = !startButton_.IsEnabled() && !deleteButton_.IsEnabled();
        if (!controller_.Debuggers().empty())
        {
            debuggerList_.SelectedIndex(0);
            RefreshDebuggerSelection();
            disabledOk = disabledOk && startButton_.IsEnabled() && deleteButton_.IsEnabled();
        }
        report(L"未选中项时启动 / 删除按钮为禁用态", disabledOk);

        // 6. 运行时依赖（构建产物完整性）
        const bool coreOk = unrealdbg_native::FileExists(applicationDirectory_ + L"\\bin\\UnrealDbgProDll.dll")
            && unrealdbg_native::FileExists(applicationDirectory_ + L"\\bin\\VT_Driver.sys");
        RefreshDiagnostics();
        report(L"主控 DLL 与内核驱动存在于运行目录", coreOk);

        // 7. 系统信息与 TL 状态已从控制器读出
        const bool infoOk = !controller_.SystemName().empty() && !controller_.CpuName().empty();
        report(L"系统信息采集非空", infoOk);

        // 8. 标题栏状态标识（需求 1）
        RefreshStatusPill();
        const bool statusOk = statusIcon_ != nullptr && statusText_ != nullptr
            && statusDivider_ != nullptr && statusDetail_ != nullptr
            && !statusText_.Text().empty() && !statusDetail_.Text().empty();
        report(L"标题栏状态条（图标 / 结论 / 分隔 / 依据）完整", statusOk);

        // 9. 侧栏运行环境摘要（需求 4）
        RefreshNavEnvironment();
        const bool navOk = navPermValue_ != nullptr && navBackendValue_ != nullptr
            && navTargetValue_ != nullptr && navSymbolValue_ != nullptr && navLogValue_ != nullptr
            && !navPermValue_.Text().empty() && !navBackendValue_.Text().empty()
            && !navTargetValue_.Text().empty() && !navSymbolValue_.Text().empty()
            && !navLogValue_.Text().empty();
        report(L"侧栏运行环境摘要五项均已填充", navOk);

        // 10. 内容区固定布局（需求 3）
        bool fixedLayoutOk = pageHost_ != nullptr && pageHost_.Clip() != nullptr;
        const auto hostChildren = pageHost_.Children();
        for (uint32_t index = 0; index < hostChildren.Size() && fixedLayoutOk; ++index)
        {
            fixedLayoutOk = hostChildren.GetAt(index).try_as<ScrollViewer>() == nullptr;
            if (fixedLayoutOk)
            {
                const Grid page = hostChildren.GetAt(index).try_as<Grid>();
                fixedLayoutOk = page != nullptr && page.VerticalAlignment() == VerticalAlignment::Stretch;
            }
        }
        report(L"内容区为固定布局（无页面级滚动容器 + 尺寸裁剪）", fixedLayoutOk);

        // 11. 日志面板默认高度随窗口缩减同步收紧（原 264 -> 190）
        const bool logHeightOk = std::fabs(logHeight_ - tok::kLogDefaultHeight) < 0.5
            && tok::kLogDefaultHeight < tok::kLogDefaultHeight + 1.0
            && tok::kLogDefaultHeight <= 200.0 && tok::kLogDefaultHeight >= 150.0;
        report(L"日志面板默认高度已随窗口缩减至 190", logHeightOk);

        // 12. 默认窗口尺寸（1340x980 -> 940x690，宽高各约 -30%）
        const bool windowSizeOk = tok::kWindowDefaultWidth <= 940.0 && tok::kWindowDefaultHeight <= 690.0
            && tok::kWindowDefaultWidth >= 900.0 && tok::kWindowDefaultHeight >= 660.0
            && tok::kWindowMinWidth < tok::kWindowDefaultWidth
            && tok::kWindowMinHeight < tok::kWindowDefaultHeight
            && tok::kTitleBarHeight <= 44.0 && tok::kNavWidth <= 192.0;
        report(L"默认窗口尺寸与标题栏 / 侧栏宽度已同步缩减", windowSizeOk);

        RefreshDebuggerSelection();
        SelectPage(PageId::Diagnostics);
        return passed;
    }

    // ---------------------------------------------------------------- 成员

    unrealdbg_native::WinUiController controller_;
    Window window_{ nullptr };
    Grid root_{ nullptr };
    Grid titleBar_{ nullptr };
    Grid pageHost_{ nullptr };
    std::array<Grid, kPageCount> pages_{};
    std::array<Border, kPageCount> navItems_{};
    std::array<Border, kPageCount> navBars_{};
    std::array<FontIcon, kPageCount> navIcons_{};
    std::array<TextBlock, kPageCount> navLabels_{};
    PageId currentPage_{ PageId::Overview };

    Border statusPill_{ nullptr };
    FontIcon statusIcon_{ nullptr };
    TextBlock statusText_{ nullptr };
    Border statusDivider_{ nullptr };
    TextBlock statusDetail_{ nullptr };

    // 侧栏"运行环境"摘要的值文本（需求 4）。
    TextBlock navPermValue_{ nullptr };
    TextBlock navBackendValue_{ nullptr };
    TextBlock navTargetValue_{ nullptr };
    TextBlock navSymbolValue_{ nullptr };
    TextBlock navLogValue_{ nullptr };

    TextBlock statusTileValue_{ nullptr };
    // 总览页"系统信息"卡片的取值文本，供 RefreshSystemInfo 一次性写入真实值（需求 2）。
    TextBlock sysNameValue_{ nullptr };
    TextBlock sysKernelValue_{ nullptr };
    TextBlock sysCpuValue_{ nullptr };
    TextBlock sysMemoryValue_{ nullptr };
    TextBlock sysArchValue_{ nullptr };
    TextBlock debuggerTileValue_{ nullptr };
    TextBlock debuggerTileHint_{ nullptr };
    TextBlock symbolTileValue_{ nullptr };
    TextBlock symbolTileHint_{ nullptr };
    TextBlock logTileValue_{ nullptr };
    TextBlock logCountText_{ nullptr };

    ProgressRing symbolProgress_{ nullptr };
    TextBlock symbolDetail_{ nullptr };
    Button initializeButton_{ nullptr };
    TextBlock initStateText_{ nullptr };
    Border initErrorBanner_{ nullptr };
    TextBlock initErrorText_{ nullptr };

    ListView debuggerList_{ nullptr };
    UIElement debuggerEmpty_{ nullptr };
    Button addButton_{ nullptr };
    Button startButton_{ nullptr };
    Button deleteButton_{ nullptr };
    TextBlock debuggerHint_{ nullptr };
    Button targetButton_{ nullptr };
    TextBlock targetText_{ nullptr };

    ToggleSwitch tlEnabledSwitch_{ nullptr };
    ToggleSwitch tlGetTickSwitch_{ nullptr };
    ToggleSwitch tlBlockResumeSwitch_{ nullptr };
    Border tlSummaryPill_{ nullptr };
    TextBlock tlStateText_{ nullptr };
    bool suppressTlEvents_{ false };

    TextBlock diagnosticsSummary_{ nullptr };
    StackPanel diagnosticsList_{ nullptr };
    Button smokeButton_{ nullptr };
    TextBlock smokeResult_{ nullptr };

    Border splitter_{ nullptr };
    Border grip_{ nullptr };
    FrameworkElement logRoot_{ nullptr };
    unrealdbg_ui::LogPanel panel_;
    double logHeight_{ tok::kLogDefaultHeight };
    double dragStartY_{ 0.0 };
    double dragStartHeight_{ 0.0 };
    bool splitterDrag_{ false };

    DispatcherTimer timer_{ nullptr };
    DispatcherTimer smokeTimer_{ nullptr };
    DispatcherTimer smokeWatchdog_{ nullptr };
    bool smokeTest_{ false };
    bool smokeFinished_{ false };

    std::wstring applicationDirectory_;
    bool backendReady_{ true };
    HWND hostWindow_{ nullptr };

    void ApplyInitialWindowSize()
    {
        auto native = window_.as<::IWindowNative>();
        HWND handle{};
        if (SUCCEEDED(native->get_WindowHandle(&handle)) && handle != nullptr)
        {
            hostWindow_ = handle;
            RECT work{};
            ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            const int workWidth = static_cast<int>(work.right - work.left);
            const int workHeight = static_cast<int>(work.bottom - work.top);
            const int width = (std::min)(static_cast<int>(tok::kWindowDefaultWidth), workWidth - 56);
            const int height = (std::min)(static_cast<int>(tok::kWindowDefaultHeight), workHeight - 56);
            ::SetWindowPos(handle, nullptr, work.left + (workWidth - width) / 2,
                work.top + (workHeight - height) / 2, width, height, SWP_NOZORDER);
        }
    }
};

// ============================================================================
// 应用入口
// ============================================================================

namespace
{
    // 临时启动诊断：日志系统就绪之前的失败无法写入会话日志，这里直接追加到
    // 系统临时目录下的纯文本文件，用于确认 OnLaunched 的执行阶段。
    void WriteStartupTrace(const std::wstring& text)
    {
        wchar_t directory[MAX_PATH]{};
        const DWORD length = ::GetTempPathW(MAX_PATH, directory);
        if (length == 0 || length >= MAX_PATH) return;
        const std::wstring path = std::wstring(directory, length) + L"unrealdbg_winui_startup_trace.txt";
        const HANDLE file = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        const std::wstring line = text + L"\r\n";
        DWORD written = 0;
        ::WriteFile(file, line.c_str(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
        ::CloseHandle(file);
    }
}

namespace
{
    std::vector<winrt::Windows::Foundation::IInspectable> g_probeKeepAlive;

    // 启动期控件构造探针（--ui-probe）：
    // 逐个把界面用到的控件挂到窗口上并留出渲染时间，配合启动追踪文件定位
    // 究竟是哪一个控件的默认样式在运行时找不到主题资源而触发 fail-fast。
    void RunUiControlProbe()
    {
        using winrt::Microsoft::UI::Xaml::Controls::Border;
        using winrt::Microsoft::UI::Xaml::Controls::Button;
        using winrt::Microsoft::UI::Xaml::Controls::ComboBox;
        using winrt::Microsoft::UI::Xaml::Controls::ComboBoxItem;
        using winrt::Microsoft::UI::Xaml::Controls::Frame;
        using winrt::Microsoft::UI::Xaml::Controls::InfoBar;
        using winrt::Microsoft::UI::Xaml::Controls::ListView;
        using winrt::Microsoft::UI::Xaml::Controls::ProgressRing;
        using winrt::Microsoft::UI::Xaml::Controls::ScrollViewer;
        using winrt::Microsoft::UI::Xaml::Controls::StackPanel;
        using winrt::Microsoft::UI::Xaml::Controls::TextBlock;
        using winrt::Microsoft::UI::Xaml::Controls::TextBox;
        using winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch;
        using winrt::Microsoft::UI::Xaml::FrameworkElement;
        using winrt::Microsoft::UI::Xaml::Window;

        using Factory = std::function<FrameworkElement()>;
        using Entry = std::pair<std::wstring, Factory>;

        auto entries = std::make_shared<std::vector<Entry>>();
        entries->emplace_back(L"TextBlock", []() -> FrameworkElement { TextBlock value; value.Text(L"probe"); return value; });
        entries->emplace_back(L"Border", []() -> FrameworkElement { Border value; value.Height(12.0); return value; });
        entries->emplace_back(L"Button", []() -> FrameworkElement { Button value; value.Content(winrt::box_value(L"probe")); return value; });
        entries->emplace_back(L"TextBox", []() -> FrameworkElement { TextBox value; value.Text(L"probe"); return value; });
        entries->emplace_back(L"ScrollViewer", []() -> FrameworkElement
        {
            ScrollViewer value;
            TextBlock inner; inner.Text(L"scroll");
            value.Content(inner);
            value.Height(60.0);
            return value;
        });
        entries->emplace_back(L"ListView", []() -> FrameworkElement
        {
            ListView value;
            value.Height(80.0);
            value.Items().Append(winrt::box_value(L"item-1"));
            value.Items().Append(winrt::box_value(L"item-2"));
            return value;
        });
        entries->emplace_back(L"ComboBox", []() -> FrameworkElement
        {
            ComboBox value;
            ComboBoxItem first; first.Content(winrt::box_value(L"item-1"));
            value.Items().Append(first);
            value.SelectedIndex(0);
            return value;
        });
        entries->emplace_back(L"ToggleSwitch", []() -> FrameworkElement { ToggleSwitch value; value.IsOn(true); return value; });
        entries->emplace_back(L"ProgressRing", []() -> FrameworkElement { ProgressRing value; value.IsActive(true); return value; });
        entries->emplace_back(L"InfoBar", []() -> FrameworkElement { InfoBar value; value.IsOpen(true); value.Message(L"probe"); return value; });
        entries->emplace_back(L"Frame", []() -> FrameworkElement { Frame value; value.Height(40.0); return value; });

        Window window;
        StackPanel root;
        root.Spacing(10.0);
        g_probeKeepAlive.push_back(window);
        g_probeKeepAlive.push_back(root);
        WriteStartupTrace(L"[probe] 探针窗口对象构造完成");
        window.Content(root);
        WriteStartupTrace(L"[probe] 已挂载根面板");
        window.Activate();
        WriteStartupTrace(L"[probe] 窗口已激活，开始逐控件探针");

        auto index = std::make_shared<size_t>(0);
        auto failed = std::make_shared<size_t>(0);
        auto timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        timer.Interval(std::chrono::milliseconds(1200));
        timer.Tick([entries, index, failed, root](winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer const& sender,
            winrt::Windows::Foundation::IInspectable const&)
        {
            if (*index >= entries->size())
            {
                sender.Stop();
                WriteStartupTrace(L"[probe] 全部候选控件构造完成，构造失败数=" + std::to_wstring(*failed));
                // 探针是自动化冒烟开关：遍历结束后立刻用退出码回报结果，
                // 避免进程停留在消息循环里，脚本侧只能靠超时判断。
                ::ExitProcess(*failed == 0 ? 0u : 1u);
            }
            const Entry& entry = (*entries)[*index];
            WriteStartupTrace(L"[probe] 开始构造：" + entry.first);
            try
            {
                root.Children().Append(entry.second());
                WriteStartupTrace(L"[probe] 挂载成功：" + entry.first);
            }
            catch (const winrt::hresult_error& error)
            {
                ++(*failed);
                WriteStartupTrace(L"[probe] 可捕获异常：" + entry.first + L" => " + std::wstring(error.message().c_str()));
            }
            ++(*index);
        });
        timer.Start();
        g_probeKeepAlive.push_back(timer);
    }
}

// 纯代码（无 XAML 编译步骤）的 WinUI 3 应用没有 XAML 编译器生成的元数据提供器，
// 必须先自行实现 IXamlMetadataProvider，否则 XamlControlsResources 里的主题资源
// （AcrylicBackgroundFillColorDefaultBrush 等）无法解析，控件默认样式与主题
// 资源字典都会加载失败（Cannot find a resource with the given key）。
// 这里直接转发给 WinUI 自带的 XamlControlsXamlMetaDataProvider，无需手写类型映射。
struct WinUiApp : ApplicationT<WinUiApp, Microsoft::UI::Xaml::Markup::IXamlMetadataProvider>
{
    Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type)
    {
        return metadataProvider_.GetXamlType(type);
    }

    Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(winrt::hstring const& fullName)
    {
        return metadataProvider_.GetXamlType(fullName);
    }

    winrt::com_array<Microsoft::UI::Xaml::Markup::XmlnsDefinition> GetXmlnsDefinitions()
    {
        return metadataProvider_.GetXmlnsDefinitions();
    }

    // 界面层出现未处理异常时，先落盘再交给系统处理：日志文件里留下明确的
    // 异常信息，便于定位是哪一个界面回调/布局路径出的问题。
    void OnUnhandledException(const IInspectable&, const UnhandledExceptionEventArgs& args)
    {
        const hstring message = args.Message();
        const std::wstring text = std::wstring(L"[界面异常] 未处理异常：") +
            (message.empty() ? L"(无消息)" : std::wstring(message.c_str()));
        if (auto* sink = unrealdbg_native::GetWinUiLogSink())
        {
            sink->Error(text);
        }
        ::OutputDebugStringW((L"UnrealDbgNative WinUI " + text + L"\n").c_str());
    }

    // 纯代码构建的 WinUI 3 应用不会自动合并 WinUI 主题资源字典，控件模板里的
    // {ThemeResource}（ComboBox 弹出层、InfoBar、ProgressRing 等用到的主题画刷）
    // 会在运行时查找失败并触发 fail-fast，因此这里显式合并主题资源。
    // 合并失败不阻断启动：界面主体使用设计令牌里的显式配色，缺少主题画刷只会
    // 影响少数系统控件的默认外观，如实记录后继续启动，避免出现"打不开的窗口"。
    bool MergeWinUiThemeResources()
    {
        try
        {
            Resources().MergedDictionaries().Append(Microsoft::UI::Xaml::Controls::XamlControlsResources());
            WriteStartupTrace(L"[trace] XamlControlsResources 合并完成");
            if (auto* sink = unrealdbg_native::GetWinUiLogSink())
            {
                sink->Info(L"[界面初始化] 已合并 XamlControlsResources（WinUI 主题资源字典）");
            }
            return true;
        }
        catch (const hresult_error& error)
        {
            const std::wstring text = std::wstring(L"[界面初始化] XamlControlsResources 合并失败，已降级继续启动：")
                + error.message().c_str();
            WriteStartupTrace(L"[trace] " + text);
            if (auto* sink = unrealdbg_native::GetWinUiLogSink())
            {
                sink->Error(text);
            }
            return false;
        }
    }

    void OnLaunched(LaunchActivatedEventArgs const&)
    {
        const wchar_t* commandLine = ::GetCommandLineW();
        const bool smokeTest = commandLine != nullptr && std::wcsstr(commandLine, L"--ui-smoke-test") != nullptr;
        const bool uiProbe = commandLine != nullptr && std::wcsstr(commandLine, L"--ui-probe") != nullptr;
        UnhandledException({ this, &WinUiApp::OnUnhandledException });
        if (uiProbe)
        {
            WriteStartupTrace(L"[trace] 进入控件构造探针模式");
            MergeWinUiThemeResources();
            RunUiControlProbe();
            return;
        }
        try
        {
            WriteStartupTrace(L"[trace] OnLaunched 进入");
            MergeWinUiThemeResources();
            WriteStartupTrace(L"[trace] 即将创建 Shell");
            shell_ = std::make_unique<Shell>(unrealdbg_native::GetRuntimeDirectory(), smokeTest);
            WriteStartupTrace(L"[trace] Shell 创建完成，即将 Launch");
            shell_->Launch();
            WriteStartupTrace(L"[trace] Shell::Launch 返回");
        }
        catch (const hresult_error& error)
        {
            const std::wstring text = std::wstring(L"[界面异常] 启动失败：") + error.message().c_str();
            WriteStartupTrace(L"[trace] 捕获 hresult_error: " + text +
                L"；hresult=0x" + std::to_wstring(static_cast<uint32_t>(error.code().value)));
            if (auto* sink = unrealdbg_native::GetWinUiLogSink())
            {
                sink->Error(text);
            }
            ::OutputDebugStringW((L"UnrealDbgNative WinUI " + text + L"\n").c_str());
            g_smokeTestExitCode = 1;
        }
        catch (...)
        {
            WriteStartupTrace(L"[trace] 捕获未知异常");
            if (auto* sink = unrealdbg_native::GetWinUiLogSink())
            {
                sink->Error(L"[界面异常] 启动失败：未知异常类型");
            }
            g_smokeTestExitCode = 1;
        }
    }

    // WinUI 自带的元数据提供器：IXamlMetadataProvider 的三个方法全部转发给它，
    // 使 XamlControlsResources 的主题资源键能够被解析。
    Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider metadataProvider_;
    std::unique_ptr<Shell> shell_;
};

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    const HRESULT initialized = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
    {
        return 1;
    }

    // 需求 6：进程启动即把工作目录归一化到真实运行目录。历史上从项目根目录
    // （或任意其它目录）启动时，相对路径会以调用者的工作目录为基准，表现为
    // "必须进到产物目录才能启动"。归一化后两种启动方式完全等价。
    {
        const std::wstring runtimeDirectory = unrealdbg_native::GetRuntimeDirectory();
        if (!runtimeDirectory.empty())
        {
            ::SetCurrentDirectoryW(runtimeDirectory.c_str());
        }
    }

    const wchar_t* commandLine = ::GetCommandLineW();
    if (commandLine != nullptr && std::wcsstr(commandLine, L"--self-test") != nullptr)
    {
        std::wstring failure;
        const bool ok = unrealdbg_native::RunCoreSelfTests(failure)
            && unrealdbg_native::RunSymbolCacheSelfTests(failure);
        if (!ok)
        {
            ::OutputDebugStringW((L"UnrealDbgNative WinUI 自检失败：" + failure + L"\n").c_str());
        }
        ::CoUninitialize();
        return ok ? 0 : 1;
    }
    if (commandLine != nullptr && std::wcsstr(commandLine, L"--symbol-cache-check") != nullptr)
    {
        std::wstring failure;
        const bool ok = unrealdbg_native::RunSymbolCacheSelfTests(failure);
        if (!ok)
        {
            ::OutputDebugStringW((L"UnrealDbgNative 符号缓存自检失败：" + failure + L"\n").c_str());
        }
        ::CoUninitialize();
        return ok ? 0 : 1;
    }

    const bool smokeTest = commandLine != nullptr && std::wcsstr(commandLine, L"--ui-smoke-test") != nullptr;

    bool bootstrapped = false;
    try
    {
        const PACKAGE_VERSION minVersion{};
        winrt::check_hresult(::MddBootstrapInitialize2(0x00010008, nullptr, minVersion,
            MddBootstrapInitializeOptions_OnNoMatch_ShowUI));
        bootstrapped = true;
    }
    catch (const hresult_error&)
    {
        bootstrapped = false;
    }

    if (!bootstrapped)
    {
        const std::wstring message = L"未检测到 Windows App Runtime 1.8 (x64)，界面无法启动。\n\n"
            L"请先安装 WindowsAppRuntimeInstall-x64.exe 后重试。";
        ::MessageBoxW(nullptr, message.c_str(), L"虚幻调试器", MB_OK | MB_ICONERROR);
        ::CoUninitialize();
        return 1;
    }

    {
        Application::Start([](auto&&)
        {
            make<WinUiApp>();
        });
    }

    ::MddBootstrapShutdown();
    ::CoUninitialize();
    return smokeTest ? g_smokeTestExitCode : 0;
}

// 旧版插件通过宿主导出的 PrintLog 回传日志，入口保持不变。
extern "C" __declspec(dllexport) void __stdcall PrintLog(TCHAR* text)
{
    unrealdbg_native::LogSink* sink = unrealdbg_native::GetWinUiLogSink();
    if (sink == nullptr)
    {
        OutputDebugStringW(L"[UnrealDbgNative] PrintLog 收到消息，但日志接收器尚未就绪\n");
        return;
    }

    if (text == nullptr)
    {
        sink->Error(L"兼容日志入口 PrintLog 收到空指针", ERROR_INVALID_PARAMETER);
        return;
    }

    // 限制扫描长度，避免损坏插件传入未终止字符串时越界读取。
    constexpr size_t kMaximumPrintLogLength = 32768;
    size_t length = 0;
    while (length < kMaximumPrintLogLength && text[length] != L'\0')
    {
        ++length;
    }
    if (length == 0)
    {
        return;
    }
    if (length >= kMaximumPrintLogLength)
    {
        sink->Error(L"兼容日志入口 PrintLog 消息超过 32767 个字符，已拒绝接收", ERROR_BUFFER_OVERFLOW);
        return;
    }

    const std::wstring message(text, length);
    const std::wstring forwarded = L"[兼容 PrintLog] " + message;
    const bool verbose = message.find(L"[DEBUG]") != std::wstring::npos
        || message.find(L"[调试]") != std::wstring::npos;
    sink->External(verbose ? unrealdbg_native::LogLevel::Debug : unrealdbg_native::LogLevel::Info, forwarded);
}