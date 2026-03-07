#pragma once

#include <Windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/command_line.h"
#include "app/runtime_profile.h"
#include "app/site_predictor.h"
#include "browser/browser_controller.h"
#include "cef/velox_client.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"
#include "profiling/metrics_recorder.h"
#include "settings/app_settings.h"

namespace velox::browser {

class BrowserWindow : public cef::BrowserEventDelegate {
 public:
  enum class PanelMode {
    kNone,
    kSettings,
    kHistory,
    kDownloads,
    kBookmarks,
  };

  BrowserWindow(HINSTANCE instance,
                settings::AppSettings settings,
                app::CommandLineOptions command_line,
                app::RuntimeProfile runtime_profile,
                profiling::MetricsRecorder* metrics,
                app::SitePredictor* site_predictor);

  bool Create();
  void Show(int command_show);
  bool CreateBrowserShell();
  HWND hwnd() const;

  void OnBrowserCreated(int tab_id, CefRefPtr<CefBrowser> browser) override;
  void OnBrowserClosed(int tab_id) override;
  void OnAddressChanged(int tab_id, const std::wstring& url) override;
  void OnTitleChanged(int tab_id, const std::wstring& title) override;
  void OnLoadingStateChange(int tab_id, bool is_loading, bool can_go_back, bool can_go_forward) override;
  void OnLoadError(int tab_id, const std::wstring& failed_url, const std::wstring& error_text) override;
  void OnStatusMessage(int tab_id, const std::wstring& status) override;
  void OnLoadProgress(int tab_id, double progress) override;
  void OnRendererMetric(int tab_id, const std::string& name, double value) override;
  void OnOpenUrlInNewTab(const std::wstring& url, bool activate) override;
  std::wstring GetDownloadTargetPath(int tab_id, const std::wstring& suggested_name) override;
  void OnDownloadCreated(int tab_id, int download_id, const std::wstring& file_name, const std::wstring& full_path) override;
  void OnDownloadUpdated(int tab_id,
                         int download_id,
                         int percent_complete,
                         bool is_complete,
                         bool is_canceled,
                         const std::wstring& status_text) override;
  void OnBrowserCommand(cef::BrowserCommand command) override;

 private:
  struct TabGroup {
    int id = 0;
    std::wstring label;
    COLORREF accent = RGB(0, 0, 0);
  };

  struct TabState {
    int id = 0;
    int group_id = 0;
    CefRefPtr<cef::VeloxClient> client;
    CefRefPtr<CefBrowser> browser;
    std::wstring title = L"New tab";
    std::wstring url = L"about:blank";
    std::wstring status_text;
    std::wstring load_error;
    bool is_loading = false;
    bool can_go_back = false;
    bool can_go_forward = false;
    bool close_requested = false;
    int progress_percent = 0;
  };

  struct TabVisual {
    int tab_id = 0;
    RECT tab_rect{};
    RECT close_rect{};
  };

  struct GroupVisual {
    int group_id = 0;
    RECT rect{};
  };

  struct SettingsOptionVisual {
    std::wstring key;
    RECT rect{};
  };

  struct ActionVisual {
    PanelMode mode = PanelMode::kNone;
    std::wstring label;
    RECT rect{};
  };

  struct HistoryEntry {
    std::wstring title;
    std::wstring url;
    std::wstring host;
    std::wstring visited_at;
  };

  struct HistoryVisual {
    size_t index = 0;
    RECT rect{};
  };

  struct DownloadEntry {
    int id = 0;
    std::wstring file_name;
    std::wstring full_path;
    std::wstring status_text;
    int percent_complete = 0;
    bool is_complete = false;
    bool is_canceled = false;
  };

  struct DownloadVisual {
    int id = 0;
    RECT rect{};
  };

  struct BookmarkEntry {
    std::wstring title;
    std::wstring url;
    std::wstring host;
    std::wstring saved_at;
  };

  struct BookmarkVisual {
    size_t index = 0;
    RECT rect{};
  };

  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

  void CreateControls();
  void LayoutChildren();
  void ResizeBrowserHosts();
  void NavigateFromAddressBar();
  void FocusAddressBar();
  void UpdateNavigationButtons();
  void CreateThemeResources();
  void DestroyThemeResources();
  void ApplyControlFonts() const;
  void SetAddressBarText(const std::wstring& text);
  void SetStatusText(const std::wstring& text);
  void UpdateProgressBar(int progress_percent);
  std::wstring BuildDefaultStatusText() const;
  std::wstring BuildPrivacyBadgeText() const;
  std::wstring BuildProfileBadgeText() const;
  LRESULT HandleDrawItem(const DRAWITEMSTRUCT* draw_item);

  void PostAddressChangedMessage(int tab_id, const std::wstring& address);
  void PostTitleChangedMessage(int tab_id, const std::wstring& title);
  void PostLoadingStateMessage(int tab_id, bool is_loading, bool can_go_back, bool can_go_forward);
  void PostLoadErrorMessage(int tab_id, const std::wstring& failed_url, const std::wstring& error_text);
  void PostStatusMessage(int tab_id, const std::wstring& status);
  void PostLoadProgressMessage(int tab_id, int progress_percent);
  void PostRendererMetricMessage(int tab_id, const std::wstring& status);
  void PostOpenTabMessage(const std::wstring& url, bool activate);

  void HandleBrowserCreatedMessage(int tab_id, CefRefPtr<CefBrowser> browser);
  void HandleBrowserClosedMessage(int tab_id);
  void HandleAddressChangedMessage(int tab_id, std::wstring address);
  void HandleTitleChangedMessage(int tab_id, std::wstring title);
  void HandleLoadingStateMessage(int tab_id, bool is_loading, bool can_go_back, bool can_go_forward);
  void HandleLoadErrorMessage(int tab_id, std::wstring failed_url, std::wstring error_text);
  void HandleStatusMessage(int tab_id, std::wstring status);
  void HandleLoadProgressMessage(int tab_id, int progress_percent);
  void HandleRendererMetricMessage(int tab_id, std::wstring status);
  void HandleBrowserCommand(cef::BrowserCommand command);
  void HandleOpenTabMessage(std::wstring url, bool activate);

  void InitializeTabGroups();
  TabState* FindTab(int tab_id);
  const TabState* FindTab(int tab_id) const;
  int FindTabIndex(int tab_id) const;
  TabGroup* FindGroup(int group_id);
  const TabGroup* FindGroup(int group_id) const;
  const TabState* active_tab() const;
  TabState* active_tab();
  bool CreateTab(const std::wstring& target_url, bool activate, int preferred_group_id);
  void ActivateTab(int tab_id);
  void ActivateTabByIndex(int index);
  void SwitchTabRelative(int delta);
  void CloseTab(int tab_id);
  void CloseActiveTabOrWindow();
  void CloseAllTabsAndWindow();
  void RemoveClosedTab(int tab_id);
  void RefreshActiveTabChrome();
  void UpdateControllerFromActiveTab();
  void DrawCustomChrome(HDC device_context, const RECT& client_rect);
  void DrawGroupStrip(HDC device_context);
  void DrawTabStrip(HDC device_context);
  void DrawActionStrip(HDC device_context);
  void DrawLibraryPanel(HDC device_context);
  void DrawSettingsPanel(HDC device_context);
  void RebuildChromeRects(const RECT& client_rect);
  void OnChromeClick(POINT point);
  void OnChromeDragMove(POINT point);
  void FinishChromeInteraction(POINT point);
  void RecordHistoryVisit(const TabState& tab);
  void LoadHistoryFromDisk();
  void SaveHistoryToDisk() const;
  void LoadBookmarksFromDisk();
  void SaveBookmarksToDisk() const;
  void OpenPanel(PanelMode mode);
  void TogglePanel(PanelMode mode);
  bool IsPanelOpen() const;
  void ActivateHistoryEntry(size_t index);
  void ActivateBookmarkEntry(size_t index);
  void ToggleCurrentPageBookmark();
  bool IsBookmarked(const std::wstring& url) const;
  DownloadEntry* FindDownload(int download_id);
  const DownloadEntry* FindDownload(int download_id) const;
  std::filesystem::path DownloadsDir() const;
  int ActiveGroupId() const;
  void AssignActiveTabToGroup(int group_id);
  void ToggleSettingsPanel();
  void ApplySearchProvider(const std::wstring& provider_name, const std::wstring& query_template);
  void UpdateAddressCueText() const;
  void ReorderDraggedTab(int target_index);
  std::wstring TabTitleForDisplay(const TabState& tab) const;

  HINSTANCE instance_ = nullptr;
  settings::AppSettings settings_;
  app::CommandLineOptions command_line_;
  app::RuntimeProfile runtime_profile_;
  profiling::MetricsRecorder* metrics_ = nullptr;
  app::SitePredictor* site_predictor_ = nullptr;

  HWND hwnd_ = nullptr;
  HWND brand_label_ = nullptr;
  HWND back_button_ = nullptr;
  HWND forward_button_ = nullptr;
  HWND reload_button_ = nullptr;
  HWND stop_button_ = nullptr;
  HWND home_button_ = nullptr;
  HWND address_bar_ = nullptr;
  HWND profile_badge_ = nullptr;
  HWND privacy_badge_ = nullptr;
  HWND status_label_ = nullptr;
  HWND progress_bar_ = nullptr;

  HFONT ui_font_ = nullptr;
  HFONT ui_font_bold_ = nullptr;
  HFONT title_font_ = nullptr;
  HFONT tab_font_ = nullptr;
  HBRUSH toolbar_brush_ = nullptr;
  HBRUSH window_brush_ = nullptr;
  HBRUSH address_brush_ = nullptr;

  browser::BrowserController controller_;
  CefRefPtr<CefRequestContext> request_context_;

  std::vector<TabGroup> tab_groups_;
  std::vector<TabState> tabs_;
  int active_tab_id_ = 0;
  int next_tab_id_ = 1;
  std::vector<TabVisual> tab_visuals_;
  std::vector<GroupVisual> group_visuals_;
  std::vector<ActionVisual> action_visuals_;
  RECT new_tab_button_rect_{};
  RECT tabs_strip_rect_{};
  RECT groups_strip_rect_{};
  RECT address_shell_rect_{};
  RECT settings_panel_rect_{};
  std::vector<SettingsOptionVisual> settings_options_;
  std::vector<HistoryEntry> history_entries_;
  std::vector<HistoryVisual> history_visuals_;
  std::vector<DownloadEntry> download_entries_;
  std::vector<DownloadVisual> download_visuals_;
  std::vector<BookmarkEntry> bookmark_entries_;
  std::vector<BookmarkVisual> bookmark_visuals_;

  std::wstring status_text_;
  std::wstring current_host_;
  int load_progress_percent_ = 0;
  bool close_requested_ = false;
  bool close_all_requested_ = false;
  bool quit_after_load_posted_ = false;
  bool first_navigation_requested_ = false;
  bool saw_loading_activity_ = false;
  bool settings_panel_open_ = false;
  PanelMode panel_mode_ = PanelMode::kNone;
  bool drag_tracking_ = false;
  bool drag_reordering_ = false;
  int drag_tab_id_ = 0;
  int drag_target_index_ = -1;
  POINT drag_start_point_{};
};

}  // namespace velox::browser
