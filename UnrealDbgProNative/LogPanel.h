#pragma once

// 底部日志面板：折叠、按级别筛选、跟随滚动、单行复制与整体复制。
//
// 面板自身只负责"怎么显示日志"，不关心日志从哪里来；调用方通过 Append()
// 把 WinUiController 取到的记录推进来即可。

#include "Core.h"
#include "UiTheme.h"

#include <functional>
#include <vector>

namespace unrealdbg_ui
{
    enum class LogFilter
    {
        All,
        Info,
        Debug,
        Error,
    };

    class LogPanel final
    {
    public:
        LogPanel();

        [[nodiscard]] winrt::Microsoft::UI::Xaml::UIElement Root() const noexcept { return root_; }
        [[nodiscard]] double HeaderHeight() const noexcept { return ui::kLogHeaderHeight; }

        void SetFoldHandler(std::function<void(bool)> handler);

        void Append(const std::vector<unrealdbg_native::LogRecord>& records);
        void Clear();

        [[nodiscard]] bool IsFolded() const noexcept { return folded_; }
        void SetFolded(const bool folded);

        void SetFilter(const LogFilter filter);
        [[nodiscard]] LogFilter Filter() const noexcept { return filter_; }

        [[nodiscard]] uint32_t VisibleCount() const;
        [[nodiscard]] uint32_t TotalCount() const noexcept { return static_cast<uint32_t>(records_.size()); }

        void SetAutoScroll(const bool value);
        [[nodiscard]] bool AutoScroll() const noexcept { return autoScroll_; }
        void SetHint(const winrt::hstring& text);
        void ScrollToEnd();

    private:
        void OnFoldClicked(const winrt::Windows::Foundation::IInspectable& sender,
            const winrt::Microsoft::UI::Xaml::RoutedEventArgs& args);
        void OnFilterChanged(const winrt::Windows::Foundation::IInspectable& sender,
            const winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs& args);
        void OnFollowClicked(const winrt::Windows::Foundation::IInspectable& sender,
            const winrt::Microsoft::UI::Xaml::RoutedEventArgs& args);
        void OnCopyClicked(const winrt::Windows::Foundation::IInspectable& sender,
            const winrt::Microsoft::UI::Xaml::RoutedEventArgs& args);
        void OnClearClicked(const winrt::Windows::Foundation::IInspectable& sender,
            const winrt::Microsoft::UI::Xaml::RoutedEventArgs& args);

        void AppendRow(const unrealdbg_native::LogRecord& record);
        void RebuildRows();
        void TrimRows();
        void UpdateCounters();
        void UpdateFollowButton();
        void UpdateFoldButton();
        [[nodiscard]] bool Matches(const unrealdbg_native::LogRecord& record) const noexcept;

        winrt::Microsoft::UI::Xaml::Controls::Grid root_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Border contentHost_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ListView list_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button foldButton_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock foldLabel_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::FontIcon foldIcon_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button followButton_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock followLabel_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::FontIcon followIcon_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ComboBox filterBox_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock countText_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock hintText_{ nullptr };

        std::vector<unrealdbg_native::LogRecord> records_;
        std::function<void(bool)> foldHandler_;
        LogFilter filter_{ LogFilter::All };
        bool folded_{ false };
        bool autoScroll_{ true };
        bool suppressFilterEvent_{ false };
    };
}
