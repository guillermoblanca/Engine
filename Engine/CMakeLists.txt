add_library(GB STATIC
    src/GB/Application.cpp
    src/GB/Log.cpp)
add_library(GB::GB ALIAS GB)

target_include_directories(GB PUBLIC src)
target_link_libraries(GB PUBLIC spdlog::spdlog)

if(MSVC)
    target_compile_options(GB PRIVATE /W4 /permissive-)
    if(GB_WARNINGS_AS_ERRORS)
        target_compile_options(GB PRIVATE /WX)
    endif()
else()
    target_compile_options(GB PRIVATE -Wall -Wextra -Wpedantic)
    if(GB_WARNINGS_AS_ERRORS)
        target_compile_options(GB PRIVATE -Werror)
    endif()
endif()
