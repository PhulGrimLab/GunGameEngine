add_library(gge SHARED
    src/animation.cpp
    src/renderer.cpp
    src/obj_loader.cpp
    src/mesh_io.cpp
    src/texture_loader.cpp
    src/collision.cpp
    src/polygon.cpp
    src/world.cpp
    src/scene_io.cpp
    src/scene.cpp
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

# 독립 실행 플레이어: 구운 씬(.ggs)을 창에서 실행한다. 엔진 C API만 쓴다(gge.dll이 같은 폴더에 있어야 한다).
add_executable(gge_player player/player.cpp)
target_link_libraries(gge_player PRIVATE gge user32 gdi32)
target_compile_options(gge_player PRIVATE /W4 /utf-8)
target_compile_definitions(gge_player PRIVATE _CRT_SECURE_NO_WARNINGS)

# 플레이어 검증: 입력 매핑 자체 검사, 헤드리스로 구운 샘플을 걸어 화면을 골든과 비교, 잘못된 입력 거부.
set(GGE_SAMPLE_SCENE ${CMAKE_CURRENT_SOURCE_DIR}/../assets/samples/play.ggs)
add_test(NAME gge_player_self_test COMMAND gge_player --self-test)
set_tests_properties(gge_player_self_test PROPERTIES PASS_REGULAR_EXPRESSION "self-test OK")
add_test(NAME gge_player_headless_walk
         COMMAND gge_player ${GGE_SAMPLE_SCENE} --headless --warp --frames 100 --keys D:100 --screenshot ${CMAKE_BINARY_DIR}/player_walk.ppm)
set_tests_properties(gge_player_headless_walk PROPERTIES
    PASS_REGULAR_EXPRESSION "room=Annex events=.Enter StartPad 1, Exit StartPad 1, Enter Door 7, Exit Door 7."
    FIXTURES_SETUP gge_player_walk_screenshot)
# 같은 입력으로 만든 엔진 테스트의 골든 이미지(play_scene_320x240.ppm)와 바이트까지 같아야 한다.
add_test(NAME gge_player_matches_engine_golden
         COMMAND ${CMAKE_COMMAND} -E compare_files ${CMAKE_BINARY_DIR}/player_walk.ppm ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/play_scene_320x240.ppm)
set_tests_properties(gge_player_matches_engine_golden PROPERTIES FIXTURES_REQUIRED gge_player_walk_screenshot)
add_test(NAME gge_player_rejects_missing_scene COMMAND gge_player ${CMAKE_BINARY_DIR}/no_such_scene.ggs --headless --warp)
set_tests_properties(gge_player_rejects_missing_scene PROPERTIES WILL_FAIL TRUE)
add_test(NAME gge_player_rejects_bad_keys COMMAND gge_player ${GGE_SAMPLE_SCENE} --headless --warp --keys D:oops)
set_tests_properties(gge_player_rejects_bad_keys PROPERTIES WILL_FAIL TRUE)
