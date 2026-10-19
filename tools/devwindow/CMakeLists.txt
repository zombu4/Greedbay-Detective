# Developer window: a small tool for checking the browser session by hand
# (sign in, fetch one page, sign out, a rough lookup). Not the app's user interface and never
# installed. It drives the user's installed browser (src/browser/).
qt_add_executable(devwindow WIN32 DevWindow.cpp DevWindow.h main.cpp)
target_link_libraries(devwindow PRIVATE appbrowser applookup Qt6::Widgets)
app_enable_warnings(devwindow)
