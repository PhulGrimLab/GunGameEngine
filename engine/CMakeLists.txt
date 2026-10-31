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
# --script를 줄 때만 hostfxr로 .NET을 올린다(player/script_host.cpp). 관리 코드는 tools/ 빌드(build.cmd)가 build/bin/script_host에 모은다.
add_executable(gge_player player/player.cpp player/script_host.cpp)
target_link_libraries(gge_player PRIVATE gge user32 gdi32 advapi32)
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

# 스크립팅(.NET 호스팅): 실제 플레이어가 hostfxr로 .NET을 올려 샘플 스크립트(tools/GunGame.SampleScripts)를 실행한다.
# 관리 코드는 tools/ 빌드(build.cmd의 dotnet build)가 build/bin/script_host, build/bin/scripts에 모으므로 그 빌드 뒤에 돌려야 한다.
set(GGE_SAMPLE_SCRIPTS ${CMAKE_BINARY_DIR}/bin/scripts/GunGame.SampleScripts.dll)
set(GGE_PLAYER_WALK --headless --warp --frames 100 --keys D:100)
# 실패해야 하는 실행: 0이 아닌 종료 코드이면서 출력이 정규식에 맞아야 한다(player/expect_failure.cmake).
function(gge_player_expect_failure name regex)
    string(REPLACE ";" "|" joined "${ARGN}")
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:gge_player> "-DARGS=${joined}" "-DEXPECT_REGEX=${regex}"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/player/expect_failure.cmake)
endfunction()

# 시작할 때 Door를 끈 스크립트: 끝까지 걸어도 Door 이벤트가 없다.
add_test(NAME gge_player_script_changes_the_game
         COMMAND gge_player ${GGE_SAMPLE_SCENE} ${GGE_PLAYER_WALK} --script ${GGE_SAMPLE_SCRIPTS} --script-type DoorLock)
set_tests_properties(gge_player_script_changes_the_game PROPERTIES
    PASS_REGULAR_EXPRESSION "room=Annex events=.Enter StartPad 1, Exit StartPad 1.[\r\n]")
# Door에 들어서면 Hall로 텔레포트: 같은 걸음이 방 이동과 이벤트(Exit가 먼저)까지 바꾼다.
add_test(NAME gge_player_script_teleports_on_an_event
         COMMAND gge_player ${GGE_SAMPLE_SCENE} ${GGE_PLAYER_WALK} --script ${GGE_SAMPLE_SCRIPTS} --script-type ReturnToHall)
set_tests_properties(gge_player_script_teleports_on_an_event PROPERTIES
    PASS_REGULAR_EXPRESSION "player=.-0.100, 0.000. yaw=1.571 room=Hall events=.Enter StartPad 1, Exit StartPad 1, Enter Door 7, Exit Door 7, Enter StartPad 1, Exit StartPad 1.[\r\n]")
# OnUpdate: 30번째 업데이트(1초 뒤)에 Annex로 텔레포트. 키 입력 없이 40프레임.
add_test(NAME gge_player_script_update_runs_every_frame
         COMMAND gge_player ${GGE_SAMPLE_SCENE} --headless --warp --frames 40 --script ${GGE_SAMPLE_SCRIPTS} --script-type TeleportAfterOneSecond)
set_tests_properties(gge_player_script_update_runs_every_frame PROPERTIES
    PASS_REGULAR_EXPRESSION "player=.4.000, 0.000. yaw=0.000 room=Annex events=.Enter StartPad 1, Exit StartPad 1, Enter Door 7.[\r\n]")
# 스크립트 호스트 폴더에 gge.dll 사본이 있어도 .NET이 그걸 따로 불러 엔진 상태가 갈라지면 안 된다(플레이어가 올린 것을 그대로 쓴다).
# 갈라지면 스크립트가 보는 씬이 달라 위 텔레포트/트리거 결과가 나오지 않거나 프로세스가 죽는다.
add_test(NAME gge_player_script_decoy_host_setup
         COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/bin/script_host ${CMAKE_BINARY_DIR}/script_host_decoy)
add_test(NAME gge_player_script_decoy_host_setup_gge
         COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/bin/gge.dll ${CMAKE_BINARY_DIR}/script_host_decoy/gge.dll)
set_tests_properties(gge_player_script_decoy_host_setup PROPERTIES FIXTURES_SETUP gge_decoy_host)
set_tests_properties(gge_player_script_decoy_host_setup_gge PROPERTIES FIXTURES_SETUP gge_decoy_host DEPENDS gge_player_script_decoy_host_setup)
add_test(NAME gge_player_script_shares_the_players_gge_dll
         COMMAND gge_player ${GGE_SAMPLE_SCENE} ${GGE_PLAYER_WALK} --script ${GGE_SAMPLE_SCRIPTS} --script-type ReturnToHall
                 --script-host-dir ${CMAKE_BINARY_DIR}/script_host_decoy)
set_tests_properties(gge_player_script_shares_the_players_gge_dll PROPERTIES
    FIXTURES_REQUIRED gge_decoy_host
    PASS_REGULAR_EXPRESSION "player=.-0.100, 0.000. yaw=1.571 room=Hall events=.Enter StartPad 1, Exit StartPad 1, Enter Door 7, Exit Door 7, Enter StartPad 1, Exit StartPad 1.[\r\n]")
gge_player_expect_failure(gge_player_script_exception_stops_the_player
    "script GunGame.SampleScripts.ExplodeAtDoor failed in OnTrigger: InvalidOperationException: the door script exploded"
    ${GGE_SAMPLE_SCENE} ${GGE_PLAYER_WALK} --script ${GGE_SAMPLE_SCRIPTS} --script-type ExplodeAtDoor)
gge_player_expect_failure(gge_player_script_runaway_events_are_stopped
    "more than 1000 trigger events in one frame"
    ${GGE_SAMPLE_SCENE} ${GGE_PLAYER_WALK} --script ${GGE_SAMPLE_SCRIPTS} --script-type PingPong)
gge_player_expect_failure(gge_player_script_rejects_unknown_type
    "no script type 'Nope' in GunGame.SampleScripts"
    ${GGE_SAMPLE_SCENE} ${GGE_PLAYER_WALK} --script ${GGE_SAMPLE_SCRIPTS} --script-type Nope)
gge_player_expect_failure(gge_player_script_rejects_broken_constructor
    "cannot create script GunGame.SampleScripts.BrokenConstructor: cannot be constructed"
    ${GGE_SAMPLE_SCENE} ${GGE_PLAYER_WALK} --script ${GGE_SAMPLE_SCRIPTS} --script-type BrokenConstructor)
gge_player_expect_failure(gge_player_script_rejects_missing_assembly
    "script assembly not found"
    ${GGE_SAMPLE_SCENE} ${GGE_PLAYER_WALK} --script ${CMAKE_BINARY_DIR}/no_such_script.dll)
gge_player_expect_failure(gge_player_script_rejects_missing_host
    "script host not found"
    ${GGE_SAMPLE_SCENE} ${GGE_PLAYER_WALK} --script ${GGE_SAMPLE_SCRIPTS} --script-host-dir ${CMAKE_BINARY_DIR}/no_such_host)
gge_player_expect_failure(gge_player_script_type_needs_script
    "--script-type and --script-host-dir need --script"
    ${GGE_SAMPLE_SCENE} ${GGE_PLAYER_WALK} --script-type DoorLock)
