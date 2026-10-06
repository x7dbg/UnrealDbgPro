#include "LogPanel.h"
#include "UiComponents.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

#include <string>

using namespace winrt;
using winrt::Windows::Foundation::IInspectable;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace unrealdbg_ui
{
    // 顶部胶囊按钮的开关外观（折叠 / 跟随共用）。
    void SetChipActive(const winrt::Microsoft::UI::Xaml::Controls::Button& button,
        const winrt::Microsoft::UI::Xaml::Controls::TextBlock& label,
        const winrt::Microsoft::UI::Xaml::Controls::FontIcon& icon, const bool active);

    namespace
    {
        // 日志视图上限：保留全量记录用于重新筛选，只渲染最近的一段，避免长时间运行时卡顿。
        constexpr size_t kMaxRecords = 3000;
        constexpr uint32_t kMaxRows = 1200;

        struct LevelSkin
        {
            Color accent;
            Color background;
            const wchar_t* tag;
        };

        LevelSkin SkinOf(const unrealdbg_native::LogLevel level)
        {
            switch (level)
            {
            case unrealdbg_native::LogLevel::Debug:
                return LevelSkin{ ui::kInfo, ui::Rgb(0x11, 0x1B, 0x28), L"调试" };
            case unrealdbg_native::LogLevel::Error:
                return LevelSkin{ ui::kDanger, ui::Rgb(0x26, 0x14, 0x19), L"错误" };
            case unrealdbg_native::LogLevel::Info:
            default:
                return LevelSkin{ ui::kSuccess, ui::Rgb(0x12, 0x21, 0x1A), L"信息" };
            }
        }

        Thickness Padding4(const double left, const double top, const double right, const double bottom)
        {
            return Thickness{ left, top, right, bottom };
        }

        // 顶部的"胶囊按钮"：折叠、跟随、复制、清空共用同一种外观，靠 active 表示开关状态。
        Button MakeChip(const hstring& caption, const hstring& glyph, TextBlock& labelOut, FontIcon& iconOut)
        {
            Button button;
            StackPanel content;
            content.Orientation(Orientation::Horizontal);
            content.Spacing(4);
            content.VerticalAlignment(VerticalAlignment::Center);
            iconOut = ui::Icon(glyph, 12, ui::kTextSecondary);
            labelOut = ui::Text(caption, ui::kFontCaption - 0.5, ui::kTextSecondary, ui::kWeightMedium);
            content.Children().Append(iconOut);
            content.Children().Append(labelOut);
            button.Content(content);
            button.Padding(Padding4(10, 4, 12, 4));
            button.MinHeight(28);
            button.CornerRadius(CornerRadius{ ui::kRadiusSm, ui::kRadiusSm, ui::kRadiusSm, ui::kRadiusSm });
            button.BorderThickness(Thickness{ 1, 1, 1, 1 });
            button.VerticalAlignment(VerticalAlignment::Center);
            SetChipActive(button, labelOut, iconOut, false);
            return button;
        }

        bool TryCopyToClipboard(const std::wstring& text)
        {
            try
            {
                DataPackage package;
                package.SetText(hstring(text));
                Clipboard::SetContent(package);
                return true;
            }
            catch (const hresult_error&)
            {
                return false;
            }
        }

        // 后端拼好的日志文本拆解结果：时间（HH:MM:SS）与正文；元数据（pid/tid/会话/序号）
        // 不上正文行，完整原文始终保留在悬浮提示里。
        struct DecomposedLog
        {
            std::wstring time;
            std::wstring message;
        };

        // 识别"[来源] [时间][pid=..][tid=..][会话=..][序号=..] [级别] 正文"前缀。
        // 任何一段不认识就停在那里，正文原样保留，保证新格式之外的内容不丢字。
        DecomposedLog DecomposeLogText(const std::wstring& text)
        {
            DecomposedLog result;
            size_t cursor = 0;
            while (cursor < text.size())
            {
                while (cursor < text.size() && text[cursor] == L' ') ++cursor;
                if (cursor >= text.size() || text[cursor] != L'[') break;
                const size_t tokenStart = cursor;
                const size_t close = text.find(L']', cursor);
                if (close == std::wstring::npos) break;
                const std::wstring token = text.substr(cursor + 1, close - cursor - 1);
                bool consumed = true;
                if (token.size() >= 16 && token[4] == L'-' && token[7] == L'-')
                {
                    // 时间戳 [yyyy-MM-dd HH:MM:SS.mmm]，正文行只展示时分秒
                    result.time = token.substr(11, 8);
                }
                else if (!(token.rfind(L"pid=", 0) == 0 || token.rfind(L"tid=", 0) == 0
                    || token.rfind(L"会话=", 0) == 0 || token.rfind(L"序号=", 0) == 0
                    || token == L"信息" || token == L"调试" || token == L"错误" || token == L"警告"
                    || token == L"后端"))
                {
                    consumed = false;
                }
                if (!consumed)
                {
                    cursor = tokenStart;
                    break;
                }
                cursor = close + 1;
            }
            while (cursor < text.size() && text[cursor] == L' ') ++cursor;
            result.message = text.substr(cursor);
            if (result.message.empty())
            {
                // 整行都是标记时退回原文，避免出现空行
                result.message = text;
                result.time.clear();
            }
            return result;
        }
    }

    void SetChipActive(const Button& button, const TextBlock& label, const FontIcon& icon, const bool active)
    {
        const Color background = active ? ui::kAccentSoft : ui::Rgb(0x00, 0x00, 0x00, 0x00);
        const Color foreground = active ? ui::kAccentText : ui::kTextSecondary;
        button.Background(ui::Brush(background));
        button.BorderBrush(ui::Brush(active ? ui::kAccent : ui::kBorder));
        button.Foreground(ui::Brush(foreground));
        button.Resources().Insert(box_value(hstring(L"ButtonBackground")), box_value(ui::Brush(background)));
        button.Resources().Insert(box_value(hstring(L"ButtonBackgroundPointerOver")),
            box_value(ui::Brush(active ? ui::kAccentPressed : ui::kCardHover)));
        button.Resources().Insert(box_value(hstring(L"ButtonBackgroundPressed")),
            box_value(ui::Brush(active ? ui::kAccentPressed : ui::kSunken)));
        button.Resources().Insert(box_value(hstring(L"ButtonBorderBrush")),
            box_value(ui::Brush(active ? ui::kAccent : ui::kBorder)));
        if (label != nullptr) label.Foreground(ui::Brush(foreground));
        if (icon != nullptr) icon.Foreground(ui::Brush(foreground));
    }

    LogPanel::LogPanel()
    {
        root_ = Grid();
        RowDefinition headerRow;
        headerRow.Height(GridLength{ 1, GridUnitType::Auto });
        root_.RowDefinitions().Append(headerRow);
        RowDefinition contentRow;
        contentRow.Height(GridLength{ 1, GridUnitType::Star });
        root_.RowDefinitions().Append(contentRow);

        Border header;
        header.Height(ui::kLogHeaderHeight);
        header.Background(ui::Brush(ui::kCardInset));
        header.BorderBrush(ui::Brush(ui::kBorder));
        header.BorderThickness(Thickness{ 0, 1, 0, 0 });
        header.Padding(Padding4(10, 0, 10, 0));

        Grid bar;
        for (int i = 0; i < 9; ++i)
        {
            ColumnDefinition column;
            column.Width(i == 4 ? GridLength{ 1, GridUnitType::Star } : GridLength{ 1, GridUnitType::Auto });
            bar.ColumnDefinitions().Append(column);
        }

        foldButton_ = MakeChip(L"折叠", ui::kGlyphChevronDown, foldLabel_, foldIcon_);
        foldButton_.Click({ this, &LogPanel::OnFoldClicked });
        Grid::SetColumn(foldButton_, 0);
        bar.Children().Append(foldButton_);

        FontIcon logIcon = ui::Icon(ui::kGlyphLog, 13, ui::kTextMuted);
        logIcon.Margin(Padding4(ui::kSpaceMd, 0, 0, 0));
        logIcon.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(logIcon, 1);
        bar.Children().Append(logIcon);

        TextBlock title = ui::Text(L"运行日志", ui::kFontCaption, ui::kTextPrimary, ui::kWeightSemiBold);
        title.Margin(Padding4(6, 0, 0, 0));
        title.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(title, 2);
        bar.Children().Append(title);

        Border countPill = ui::Pill(L"0", ui::kTextSecondary, ui::kSunken, ui::kFontMicro + 0.5);
        countPill.Margin(Padding4(8, 0, 0, 0));
        countText_ = countPill.Child().try_as<TextBlock>();
        countPill.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(countPill, 3);
        bar.Children().Append(countPill);

        hintText_ = ui::Text(L"", ui::kFontCaption - 1, ui::kTextMuted);
        hintText_.Margin(Padding4(ui::kSpaceMd, 0, ui::kSpaceSm, 0));
        hintText_.TextTrimming(TextTrimming::CharacterEllipsis);
        hintText_.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(hintText_, 4);
        bar.Children().Append(hintText_);

        filterBox_ = ComboBox();
        filterBox_.Width(100);
        filterBox_.Height(28);
        filterBox_.VerticalAlignment(VerticalAlignment::Center);
        filterBox_.Items().Append(box_value(hstring(L"全部级别")));
        filterBox_.Items().Append(box_value(hstring(L"仅信息")));
        filterBox_.Items().Append(box_value(hstring(L"仅调试")));
        filterBox_.Items().Append(box_value(hstring(L"仅错误")));
        filterBox_.SelectedIndex(0);
        filterBox_.SelectionChanged({ this, &LogPanel::OnFilterChanged });
        Grid::SetColumn(filterBox_, 5);
        bar.Children().Append(filterBox_);

        followButton_ = MakeChip(L"跟随", ui::kGlyphCheck, followLabel_, followIcon_);
        followButton_.Margin(Padding4(ui::kSpaceSm, 0, 0, 0));
        followButton_.Click({ this, &LogPanel::OnFollowClicked });
        Grid::SetColumn(followButton_, 6);
        bar.Children().Append(followButton_);

        TextBlock copyLabel{ nullptr };
        FontIcon copyIcon{ nullptr };
        Button copyButton = MakeChip(L"复制", ui::kGlyphCopy, copyLabel, copyIcon);
        copyButton.Margin(Padding4(ui::kSpaceSm, 0, 0, 0));
        copyButton.Click({ this, &LogPanel::OnCopyClicked });
        Grid::SetColumn(copyButton, 7);
        bar.Children().Append(copyButton);

        TextBlock clearLabel{ nullptr };
        FontIcon clearIcon{ nullptr };
        Button clearButton = MakeChip(L"清空", ui::kGlyphClear, clearLabel, clearIcon);
        clearButton.Margin(Padding4(ui::kSpaceSm, 0, 0, 0));
        clearButton.Click({ this, &LogPanel::OnClearClicked });
        Grid::SetColumn(clearButton, 8);
        bar.Children().Append(clearButton);

        header.Child(bar);
        Grid::SetRow(header, 0);
        root_.Children().Append(header);

        list_ = ListView();
        list_.SelectionMode(ListViewSelectionMode::None);
        list_.IsItemClickEnabled(false);
        list_.Background(ui::Brush(ui::Rgb(0x00, 0x00, 0x00, 0x00)));
        list_.BorderThickness(Thickness{ 0, 0, 0, 0 });
        list_.Padding(Padding4(7, 7, 7, 7));
        list_.ItemContainerTransitions(winrt::Microsoft::UI::Xaml::Media::Animation::TransitionCollection());

        contentHost_ = ui::Surface(ui::kSunken, ui::kBorder, 0, 0);
        contentHost_.Child(list_);
        Grid::SetRow(contentHost_, 1);
        root_.Children().Append(contentHost_);

        UpdateCounters();
        UpdateFoldButton();
        UpdateFollowButton();
    }

    void LogPanel::SetFoldHandler(std::function<void(bool)> handler)
    {
        foldHandler_ = std::move(handler);
    }

    bool LogPanel::Matches(const unrealdbg_native::LogRecord& record) const noexcept
    {
        switch (filter_)
        {
        case LogFilter::Info: return record.level == unrealdbg_native::LogLevel::Info;
        case LogFilter::Debug: return record.level == unrealdbg_native::LogLevel::Debug;
        case LogFilter::Error: return record.level == unrealdbg_native::LogLevel::Error;
        case LogFilter::All:
        default: return true;
        }
    }

    void LogPanel::Append(const std::vector<unrealdbg_native::LogRecord>& records)
    {
        if (records.empty()) return;

        for (const auto& record : records)
        {
            records_.push_back(record);
            if (Matches(record)) AppendRow(record);
        }

        if (records_.size() > kMaxRecords)
        {
            records_.erase(records_.begin(), records_.begin() + static_cast<std::ptrdiff_t>(records_.size() - kMaxRecords));
        }

        TrimRows();
        UpdateCounters();
        if (autoScroll_) ScrollToEnd();
    }

    void LogPanel::AppendRow(const unrealdbg_native::LogRecord& record)
    {
        const LevelSkin skin = SkinOf(record.level);
        const DecomposedLog parts = DecomposeLogText(record.text);

        Grid row;
        ColumnDefinition stripeColumn;
        stripeColumn.Width(GridLength{ 3 });
        row.ColumnDefinitions().Append(stripeColumn);
        ColumnDefinition tagColumn;
        tagColumn.Width(GridLength{ 30 });
        row.ColumnDefinitions().Append(tagColumn);
        ColumnDefinition timeColumn;
        timeColumn.Width(GridLength{ 62 });
        row.ColumnDefinitions().Append(timeColumn);
        ColumnDefinition textColumn;
        textColumn.Width(GridLength{ 1, GridUnitType::Star });
        row.ColumnDefinitions().Append(textColumn);
        ColumnDefinition actionColumn;
        actionColumn.Width(GridLength{ 1, GridUnitType::Auto });
        row.ColumnDefinitions().Append(actionColumn);

        // 级别色条：不占正文宽度也能一眼分辨级别
        Border stripe;
        stripe.Width(3);
        stripe.Height(14);
        stripe.CornerRadius(CornerRadius{ 1.5, 1.5, 1.5, 1.5 });
        stripe.Background(ui::Brush(skin.accent));
        stripe.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(stripe, 0);
        row.Children().Append(stripe);

        TextBlock tag = ui::Text(skin.tag, ui::kFontMicro + 0.5, skin.accent, ui::kWeightSemiBold);
        tag.VerticalAlignment(VerticalAlignment::Center);
        tag.Margin(Padding4(7, 0, 0, 0));
        Grid::SetColumn(tag, 1);
        row.Children().Append(tag);

        TextBlock time = ui::Mono(hstring(parts.time), ui::kFontMono - 1, ui::kTextMuted);
        time.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(time, 2);
        row.Children().Append(time);

        // 正文按级别着色：调试日志是噪音，压暗；错误日志直接用警示色
        const Color messageColor = record.level == unrealdbg_native::LogLevel::Error ? ui::kDanger
            : record.level == unrealdbg_native::LogLevel::Debug ? ui::kTextSecondary
            : ui::kTextPrimary;
        TextBlock line = ui::Mono(hstring(parts.message), ui::kFontMono, messageColor);
        line.TextTrimming(TextTrimming::CharacterEllipsis);
        line.IsTextSelectionEnabled(true);
        line.VerticalAlignment(VerticalAlignment::Center);
        line.Margin(Padding4(ui::kSpaceSm, 0, ui::kSpaceSm, 0));
        Grid::SetColumn(line, 3);
        row.Children().Append(line);

        TextBlock copyLabel{ nullptr };
        FontIcon copyIcon{ nullptr };
        Button copyButton = MakeChip(L"", ui::kGlyphCopy, copyLabel, copyIcon);
        copyButton.Padding(Padding4(5, 1, 5, 1));
        copyButton.MinHeight(22);
        copyButton.Opacity(0.85);
        copyButton.VerticalAlignment(VerticalAlignment::Center);
        copyIcon.Visibility(Visibility::Collapsed);
        copyButton.Click([this, message = record.text](const IInspectable&, const RoutedEventArgs&)
        {
            SetHint(TryCopyToClipboard(message) ? L"已复制该条日志" : L"复制失败，请重试");
        });
        Grid::SetColumn(copyButton, 4);
        row.Children().Append(copyButton);

        Border item;
        // 错误行保留淡红底以示醒目，其余行透明；行与行之间用极细分隔线代替色块
        item.Background(ui::Brush(record.level == unrealdbg_native::LogLevel::Error
            ? ui::kDangerSoft : ui::Rgb(0x00, 0x00, 0x00, 0x00)));
        item.BorderBrush(ui::Brush(ui::Rgb(0x22, 0x2A, 0x37, 0x66)));
        item.BorderThickness(Thickness{ 0, 0, 0, 1 });
        item.CornerRadius(CornerRadius{ 0, 0, 0, 0 });
        item.Padding(Padding4(2, 3, 2, 4));
        item.Child(row);

        ListViewItem container;
        container.Content(item);
        container.Padding(Padding4(0, 0, 0, 0));
        container.MinHeight(0);
        container.Margin(Padding4(0, 0, 0, 0));
        container.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        container.Background(ui::Brush(ui::Rgb(0x00, 0x00, 0x00, 0x00)));
        container.BorderThickness(Thickness{ 0, 0, 0, 0 });
        ui::SetToolTip(container, hstring(record.text));

        list_.Items().Append(container);
    }

    void LogPanel::TrimRows()
    {
        while (list_.Items().Size() > kMaxRows) list_.Items().RemoveAt(0);
    }

    void LogPanel::RebuildRows()
    {
        list_.Items().Clear();
        for (const auto& record : records_)
        {
            if (Matches(record)) AppendRow(record);
        }
        TrimRows();
        UpdateCounters();
        ScrollToEnd();
    }

    void LogPanel::Clear()
    {
        records_.clear();
        list_.Items().Clear();
        UpdateCounters();
        SetHint(L"日志视图已清空，磁盘日志文件不受影响");
    }

    uint32_t LogPanel::VisibleCount() const
    {
        return list_.Items().Size();
    }

    void LogPanel::UpdateCounters()
    {
        if (countText_ == nullptr) return;
        const hstring visible = to_hstring(VisibleCount());
        const hstring total = to_hstring(static_cast<uint32_t>(records_.size()));
        countText_.Text(visible == total ? hstring(L"共 " + visible + L" 条") : hstring(visible + L" / " + total + L" 条"));
    }

    void LogPanel::UpdateFoldButton()
    {
        foldLabel_.Text(folded_ ? L"展开" : L"折叠");
        foldIcon_.Glyph(folded_ ? ui::kGlyphChevronUp : ui::kGlyphChevronDown);
    }

    void LogPanel::UpdateFollowButton()
    {
        SetChipActive(followButton_, followLabel_, followIcon_, autoScroll_);
        followLabel_.Text(autoScroll_ ? L"跟随" : L"暂停");
        followIcon_.Glyph(autoScroll_ ? ui::kGlyphPlay : ui::kGlyphPause);
    }

    void LogPanel::SetFolded(const bool folded)
    {
        if (folded_ == folded) return;
        folded_ = folded;
        contentHost_.Visibility(folded ? Visibility::Collapsed : Visibility::Visible);
        UpdateFoldButton();
        if (foldHandler_) foldHandler_(folded_);
    }

    void LogPanel::SetFilter(const LogFilter filter)
    {
        if (filter_ == filter) return;
        filter_ = filter;
        suppressFilterEvent_ = true;
        filterBox_.SelectedIndex(static_cast<int32_t>(filter_));
        suppressFilterEvent_ = false;
        RebuildRows();
    }

    void LogPanel::SetAutoScroll(const bool value)
    {
        if (autoScroll_ == value) return;
        autoScroll_ = value;
        UpdateFollowButton();
        if (autoScroll_) ScrollToEnd();
    }

    void LogPanel::SetHint(const hstring& text)
    {
        if (hintText_ != nullptr) hintText_.Text(text);
    }

    void LogPanel::ScrollToEnd()
    {
        if (list_.Items().Size() == 0) return;
        list_.UpdateLayout();
        list_.ScrollIntoView(list_.Items().GetAt(list_.Items().Size() - 1));
    }

    void LogPanel::OnFoldClicked(const IInspectable&, const RoutedEventArgs&)
    {
        SetFolded(!folded_);
    }

    void LogPanel::OnFollowClicked(const IInspectable&, const RoutedEventArgs&)
    {
        SetAutoScroll(!autoScroll_);
    }

    void LogPanel::OnClearClicked(const IInspectable&, const RoutedEventArgs&)
    {
        Clear();
    }

    void LogPanel::OnFilterChanged(const IInspectable&, const SelectionChangedEventArgs&)
    {
        if (suppressFilterEvent_) return;
        const int32_t index = filterBox_.SelectedIndex();
        if (index < 0) return;
        filter_ = static_cast<LogFilter>(index);
        RebuildRows();
    }

    void LogPanel::OnCopyClicked(const IInspectable&, const RoutedEventArgs&)
    {
        std::wstring text;
        for (const auto& record : records_)
        {
            if (!Matches(record)) continue;
            if (!text.empty()) text += L"\r\n";
            text += L"[" + std::wstring(SkinOf(record.level).tag) + L"] " + record.text;
        }
        if (text.empty())
        {
            SetHint(L"当前筛选下没有日志可复制");
            return;
        }
        SetHint(TryCopyToClipboard(text) ? L"已复制当前筛选下的全部日志" : L"复制失败，请重试");
    }
}
