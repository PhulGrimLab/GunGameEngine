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
    src/font.cpp
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
target_compile_definitions(engine_bench PRIVATE "GGE_SPAWNER_SCENE=\"${CMAKE_CURRENT_SOURCE_DIR}/../assets/samples/spawner.ggs\"")

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
# 동작 버튼: 누른 프레임에 한 번만 전달된다(E를 2프레임 눌러도 한 번, EQ를 함께 누르면 각각 한 번, 순서는 Interact -> Cancel -> Menu).
add_test(NAME gge_player_script_actions_arrive_once_per_press
         COMMAND gge_player ${GGE_SAMPLE_SCENE} --headless --warp --frames 10 --keys E:2,-:1,Q:1,M:1,EQ:3
                 --script ${GGE_SAMPLE_SCRIPTS} --script-type LogActions)
set_tests_properties(gge_player_script_actions_arrive_once_per_press PROPERTIES
    PASS_REGULAR_EXPRESSION "action Interact[\r\n]+.script. action Cancel[\r\n]+.script. action Menu[\r\n]+.script. action Interact[\r\n]+.script. action Cancel[\r\n]+frames=10 ")
# 상호작용으로 퍼즐 장치를 움직인다: Door 영역 안(61프레임째 x=2.1)에서 E를 누르면 기둥이 올라간다.
add_test(NAME gge_player_script_interact_inside_a_trigger_moves_an_object
         COMMAND gge_player ${GGE_SAMPLE_SCENE} --headless --warp --frames 62 --keys D:61,E:1
                 --script ${GGE_SAMPLE_SCRIPTS} --script-type OpenWithInteract)
set_tests_properties(gge_player_script_interact_inside_a_trigger_moves_an_object PROPERTIES
    PASS_REGULAR_EXPRESSION "opened: Pillar y 0.5 -> 3.5")
# 같은 스크립트라도 Door 밖에서는 아무 일도 없다.
add_test(NAME gge_player_script_interact_outside_the_trigger_does_nothing
         COMMAND gge_player ${GGE_SAMPLE_SCENE} --headless --warp --frames 5 --keys E:1
                 --script ${GGE_SAMPLE_SCRIPTS} --script-type OpenWithInteract)
set_tests_properties(gge_player_script_interact_outside_the_trigger_does_nothing PROPERTIES
    PASS_REGULAR_EXPRESSION "frames=5 player=.-4.000, 0.000. yaw=0.000 room=Hall events=.Enter StartPad 1.[\r\n]"
    FAIL_REGULAR_EXPRESSION "opened")
# 열쇠로 잠긴 문(assets/samples/locked_door.ggs + LockedDoor 스크립트): 스크립트가 이름 있는 충돌체 "Door"를 끄고 켠다.
set(GGE_LOCKED_DOOR_SCENE ${CMAKE_CURRENT_SOURCE_DIR}/../assets/samples/locked_door.ggs)
# 열쇠를 줍고(KeyPad) 문 앞(DoorPad)에서 상호작용하면 문이 열려 Annex까지 걸어 들어간다.
add_test(NAME gge_player_locked_door_opens_with_the_key
         COMMAND gge_player ${GGE_LOCKED_DOOR_SCENE} --headless --warp --frames 113 --keys S:40,E:1,DW:31,E:1,D:40
                 --script ${GGE_SAMPLE_SCRIPTS} --script-type LockedDoor)
set_tests_properties(gge_player_locked_door_opens_with_the_key PROPERTIES
    PASS_REGULAR_EXPRESSION "picked up the key[\r\n]+.script. the door opens[\r\n]+frames=113 player=.2.192, -0.192. yaw=1.571 room=Annex events=.Enter KeyPad 10, Exit KeyPad 10, Enter DoorPad 11, Exit DoorPad 11.")
# 열쇠 없이 문 앞에서 상호작용하면 잠겨 있고, 계속 걸어도 문(x=-1 - 반지름 0.4)에 막힌다.
add_test(NAME gge_player_locked_door_stays_locked_without_the_key
         COMMAND gge_player ${GGE_LOCKED_DOOR_SCENE} --headless --warp --frames 72 --keys DS:31,E:1,D:40
                 --script ${GGE_SAMPLE_SCRIPTS} --script-type LockedDoor)
set_tests_properties(gge_player_locked_door_stays_locked_without_the_key PROPERTIES
    PASS_REGULAR_EXPRESSION "the door is locked[\r\n]+frames=72 player=.-1.400, 0.192. yaw=1.571 room=Hall events=.Enter DoorPad 11.")
set_tests_properties(gge_player_locked_door_stays_locked_without_the_key PROPERTIES FAIL_REGULAR_EXPRESSION "the door opens")
# 대조군: 스크립트가 없어도 문이 막는다(막는 것은 충돌체이고, 여는 것은 스크립트다).
add_test(NAME gge_player_locked_door_blocks_without_any_script
         COMMAND gge_player ${GGE_LOCKED_DOOR_SCENE} --headless --warp --frames 80 --keys DS:31,D:49)
set_tests_properties(gge_player_locked_door_blocks_without_any_script PROPERTIES
    PASS_REGULAR_EXPRESSION "frames=80 player=.-1.400, 0.192. yaw=1.571 room=Hall events=.Enter DoorPad 11.")
# 플레이어 걷기/대기 자동 전환(assets/samples/walk_animation.ggs + PlayerWalkAnimation 스크립트): 스크립트가 PlayerSpeed로 walk/idle 클립을 고른다.
# 결과 한 줄에 Hero 노드의 클립 재생 상태를 붙여(--report-animation) 로그 문구가 아니라 실제 엔진 상태를 확인한다.
set(GGE_WALK_SCENE ${CMAKE_CURRENT_SOURCE_DIR}/../assets/samples/walk_animation.ggs)
function(gge_player_walk_test name frames keys regex)
    add_test(NAME ${name}
             COMMAND gge_player ${GGE_WALK_SCENE} --headless --warp --frames ${frames} --keys ${keys} --report-animation Hero
                     --script ${GGE_SAMPLE_SCRIPTS} --script-type PlayerWalkAnimation)
    set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
endfunction()
# 걷기 20프레임(0.67초): 페이드(0.15초)가 끝난 walk
gge_player_walk_test(gge_player_walk_animation_switches_to_walk 20 D:20
    "animation.Hero.=walk playing=1 fading=0 loop=1 finished=0 time=0.633")
# 5프레임(0.13초)이면 아직 idle -> walk 크로스페이드 중
gge_player_walk_test(gge_player_walk_animation_crossfades 5 D:5 "animation.Hero.=walk playing=1 fading=1")
# 걷다가 멈추면(입력 없음) idle로 돌아온다
gge_player_walk_test(gge_player_walk_animation_returns_to_idle_when_stopped 50 D:20,-:30 "animation.Hero.=idle playing=1 fading=0")
# 벽을 정면으로 밀면 입력은 있어도 실제 이동이 0이라 idle(PlayerSpeed는 입력이 아니라 실제 이동)
gge_player_walk_test(gge_player_walk_animation_is_idle_when_blocked_by_a_wall 120 D:120
    "player=.5.600, 0.000. .*animation.Hero.=idle playing=1 fading=0")
# 오브젝트 생성/삭제(assets/samples/spawner.ggs + Spawner 스크립트): 템플릿 Crate(+Lid)를 복제/삭제한다. --report-objects가 실제 엔진 상태
# (동적 노드 수, 렌더러 오브젝트 수)를 한 줄에 붙인다. 구운 오브젝트는 Floor, Crate, Lid, Hero 4개.
set(GGE_SPAWNER_SCENE ${CMAKE_CURRENT_SOURCE_DIR}/../assets/samples/spawner.ggs)
function(gge_player_spawner_test name frames keys regex)
    add_test(NAME ${name}
             COMMAND gge_player ${GGE_SPAWNER_SCENE} --headless --warp --frames ${frames} --keys ${keys} --report-objects
                     --script ${GGE_SAMPLE_SCRIPTS} --script-type Spawner)
    set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
endfunction()
# E 세 번: 복제 3개(노드 2개씩) = 동적 6, 렌더러 오브젝트 4 + 6. 플레이어가 움직이는 만큼 위치가 다르다(복제는 만든 순간 플레이어 위치).
gge_player_spawner_test(gge_player_spawner_creates_instances 11 E:1,-:2,D:3,E:1,D:3,E:1
    "spawned Crate1 at 0.00, 0.00[\r\n]+.script. spawned Crate2 at 0.30, 0.00[\r\n]+.script. spawned Crate3 at 0.60, 0.00[\r\n]+.*dynamic=6 objects=10")
# E 두 번 뒤 Q: 가장 최근 복제(Crate2)만 지워진다 -> 동적 2, 오브젝트 6
gge_player_spawner_test(gge_player_spawner_destroys_the_latest_instance 8 E:1,-:1,E:1,-:1,Q:1,-:1
    "destroyed Crate2[\r\n]+.*dynamic=2 objects=6")
# 아무것도 안 만들었을 때 Q: 지울 것이 없고 동적 0, 오브젝트 4
gge_player_spawner_test(gge_player_spawner_destroys_nothing_when_nothing_was_spawned 3 Q:1,-:2
    "nothing to destroy[\r\n]+.*dynamic=0 objects=4")
gge_player_expect_failure(gge_player_spawner_flood_is_stopped_by_the_frame_limit
    "too many spawns in one frame .max 64."
    ${GGE_SPAWNER_SCENE} --headless --warp --frames 3 --keys E:1 --script ${GGE_SAMPLE_SCRIPTS} --script-type SpawnFlood)
# 화면 위 글자/사각형(IUiApi): 직접 그린 5x7 시험용 폰트(assets/samples/pixel5x7.bdf) + HudDemo 스크립트. --report-ui가 화면 항목 수를 붙인다.
set(GGE_TEST_FONT ${CMAKE_CURRENT_SOURCE_DIR}/../assets/samples/pixel5x7.bdf)
function(gge_player_hud_test name frames keys regex)
    add_test(NAME ${name}
             COMMAND gge_player ${GGE_SPAWNER_SCENE} --headless --warp --frames ${frames} --keys ${keys} --report-ui --font ${GGE_TEST_FONT}
                     --script ${GGE_SAMPLE_SCRIPTS} --script-type HudDemo)
    set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
endfunction()
# 시작할 때 글자 크기 "HELLO" = 폭 30(6x5) 높이 9 한 줄, 항목은 배경 + 글자 둘
gge_player_hud_test(gge_player_hud_shows_text_and_rect 2 -:2 "measured 30x9x1[\r\n]+.*ui=2")
# 상호작용: 같은 이름으로 다시 ShowText하면 제자리 갱신이라 항목 수는 그대로
gge_player_hud_test(gge_player_hud_updates_text_in_place 3 E:1,-:2 "title changed[\r\n]+.*ui=2")
# 취소: 글자 항목이 지워지고(true) 이미 지운 뒤에는 false, 배경은 남는다
gge_player_hud_test(gge_player_hud_hides_text 3 Q:1,-:2 "title hidden: True, again: False[\r\n]+.*ui=1")
# 화면 결과: 헤드리스 화면(160x120)이 골든과 바이트까지 같다(스크립트 -> 화면 항목 -> 플레이어 화면 전체 경로)
add_test(NAME gge_player_hud_screenshot
         COMMAND gge_player ${GGE_SPAWNER_SCENE} --headless --warp --size 160x120 --frames 2 --keys E:1,-:1 --font ${GGE_TEST_FONT}
                 --script ${GGE_SAMPLE_SCRIPTS} --script-type HudDemo --screenshot ${CMAKE_BINARY_DIR}/player_hud.ppm)
set_tests_properties(gge_player_hud_screenshot PROPERTIES FIXTURES_SETUP gge_player_hud_screenshot)
add_test(NAME gge_player_hud_matches_golden
         COMMAND ${CMAKE_COMMAND} -E compare_files ${CMAKE_BINARY_DIR}/player_hud.ppm ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/player_hud_160x120.ppm)
set_tests_properties(gge_player_hud_matches_golden PROPERTIES FIXTURES_REQUIRED gge_player_hud_screenshot)
# 폰트 없이 글자 API를 쓰면 사유와 함께 멈춘다. --font만 주거나 잘못된 폰트 파일도 사유와 함께 거부한다.
gge_player_expect_failure(gge_player_hud_text_needs_a_font
    "no font: start the player with --font file.bdf"
    ${GGE_SPAWNER_SCENE} --headless --warp --frames 1 --script ${GGE_SAMPLE_SCRIPTS} --script-type HudDemo)
gge_player_expect_failure(gge_player_font_needs_a_script
    "--font needs --script"
    ${GGE_SPAWNER_SCENE} --headless --warp --frames 1 --font ${GGE_TEST_FONT})
gge_player_expect_failure(gge_player_font_missing_file_is_rejected
    "cannot load the font: cannot open font file"
    ${GGE_SPAWNER_SCENE} --headless --warp --frames 1 --font ${CMAKE_BINARY_DIR}/no_such_font.bdf --script ${GGE_SAMPLE_SCRIPTS} --script-type HudDemo)
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
