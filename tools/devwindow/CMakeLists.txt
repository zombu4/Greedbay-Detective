# Developer window: a small tool for checking the browser session by hand
# (sign in, fetch one page, sign out). Not the app's user interface and never
# installed. It starts the browser worker built in src/.
qt_add_executable(devwindow WIN32 DevWindow.cpp DevWindow.h main.cpp)
target_link_libraries(devwindow PRIVATE appcore Qt6::Widgets)
target_compile_definitions(devwindow PRIVATE DEVWINDOW_WORKER_PATH="$<TARGET_FILE:browserworker>")
add_dependencies(devwindow browserworker)
app_enable_warnings(devwindow)
