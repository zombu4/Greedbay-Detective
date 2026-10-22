# The shared seller list's server (DESIGN 3.7): a separate program the list
# owner runs on their own server (HTTPS). Not part of the app's packages.

qt_add_library(sellerserver STATIC
    HttpServer.cpp HttpServer.h
    KeyFiles.cpp KeyFiles.h
    ServerStore.cpp ServerStore.h
    Service.cpp Service.h)
target_link_libraries(sellerserver PUBLIC appsellerlist Qt6::Core Qt6::Network Qt6::Sql)
target_include_directories(sellerserver PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
app_enable_warnings(sellerserver)

qt_add_executable(seller-list-server main.cpp)
set_target_properties(seller-list-server PROPERTIES MACOSX_BUNDLE OFF WIN32_EXECUTABLE OFF)
target_link_libraries(seller-list-server PRIVATE sellerserver)
app_copy_runtime_dlls(seller-list-server)
app_enable_warnings(seller-list-server)
