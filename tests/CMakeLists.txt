add_executable(GBTests smoke.cpp)
target_link_libraries(GBTests PRIVATE GB::GB)
add_test(NAME smoke COMMAND GBTests)
