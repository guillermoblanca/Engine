add_executable(GBTests smoke.cpp)
target_link_libraries(GBTests PRIVATE GB::GB)
add_test(NAME smoke COMMAND GBTests)

add_executable(GBGLTests gl_render.cpp)
target_link_libraries(GBGLTests PRIVATE GB::GB)
add_test(NAME gl_render COMMAND GBGLTests)
set_tests_properties(gl_render PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBSceneTests gl_scene.cpp)
target_link_libraries(GBSceneTests PRIVATE GB::GB)
add_test(NAME gl_scene COMMAND GBSceneTests)
set_tests_properties(gl_scene PROPERTIES SKIP_RETURN_CODE 77)
