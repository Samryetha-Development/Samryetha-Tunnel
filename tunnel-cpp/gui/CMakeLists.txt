add_executable(tunnel-gui
    main.cpp
    Theme.hpp
    AppState.hpp
    AppState.cpp
    MainWindow.hpp
    MainWindow.cpp
    DashboardPage.hpp
    DashboardPage.cpp
    LogsPage.hpp
    LogsPage.cpp
    StatsPage.hpp
    StatsPage.cpp
    SettingsPage.hpp
    SettingsPage.cpp
    TokensPage.hpp
    TokensPage.cpp
    UsagePage.hpp
    UsagePage.cpp
    AdminPage.hpp
    AdminPage.cpp
    VisitorAuthDialog.hpp
    VisitorAuthDialog.cpp
    TunnelEditorDialog.hpp
    TunnelEditorDialog.cpp
)

target_link_libraries(tunnel-gui PRIVATE tunnel_core Qt6::Widgets Qt6::Charts)

# macOS 出 .app，Windows 出无控制台窗口的 GUI 程序
set_target_properties(tunnel-gui PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "Samryetha Tunnel"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.samryetha.tunnel"
    WIN32_EXECUTABLE ON
)
