cmake_minimum_required(VERSION 3.15)
project(viscillateApp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find WIL and unofficial-webview2 packages
find_package(wil CONFIG REQUIRED)
find_package(unofficial-webview2 CONFIG REQUIRED)

add_executable(viscillateApp main.cpp)

# Link targets
target_link_libraries(viscillateApp PRIVATE
    WIL::WIL
    unofficial::webview2::webview2
)

# copy the 'UI' directory to the output folder after building
add_custom_command(TARGET viscillateApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/UI
    $<TARGET_FILE_DIR:viscillateApp>/UI
)

# Uncomment for production:
#set_target_properties(viscillateApp PROPERTIES WIN32_EXECUTABLE TRUE)