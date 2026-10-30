add_library(gge SHARED
    src/animation.cpp
    src/renderer.cpp
    src/obj_loader.cpp
    src/mesh_io.cpp
    src/texture_loader.cpp
    src/collision.cpp
    src/world.cpp
    src/capi.cpp)
target_include_directories(gge PUBLIC include PRIVATE src)
target_compile_definitions(gge PRIVATE GGE_EXPORTS NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(gge PRIVATE /W4 /utf-8)
target_link_libraries(gge PRIVATE d3d11 d3dcompiler dxgi windowscodecs ole32)

add_executable(engine_tests tests/engine_tests.cpp)
target_link_libraries(engine_tests PRIVATE gge)
target_compile_options(engine_tests PRIVATE /W4 /utf-8)
target_compile_definitions(engine_tests PRIVATE _CRT_SECURE_NO_WARNINGS GGE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_test(NAME engine_tests COMMAND engine_tests)

# 수동 실행용 벤치마크(ctest에 등록하지 않는다): buildin\engine_bench.exe
add_executable(engine_bench tests/engine_bench.cpp)
target_link_libraries(engine_bench PRIVATE gge)
target_compile_options(engine_bench PRIVATE /W4 /utf-8)
