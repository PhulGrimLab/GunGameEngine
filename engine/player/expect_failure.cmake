# 테스트 헬퍼: 프로그램을 실행해 "0이 아닌 종료 코드"이면서 출력(표준 출력+오류)이 정규식에 맞는지 확인한다.
# CTest의 WILL_FAIL은 종료 코드만 뒤집고, PASS_REGULAR_EXPRESSION은 종료 코드를 무시하므로 둘을 함께 검사할 수 없다.
# 사용: cmake -DEXE=<프로그램> "-DARGS=a|b|c" "-DEXPECT_REGEX=<정규식>" -P expect_failure.cmake   (ARGS는 '|'로 구분)
if(NOT EXE OR NOT EXPECT_REGEX)
    message(FATAL_ERROR "usage: -DEXE=... -DARGS=a|b -DEXPECT_REGEX=... -P expect_failure.cmake")
endif()
string(REPLACE "|" ";" args "${ARGS}")
execute_process(COMMAND "${EXE}" ${args}
                RESULT_VARIABLE exit_code OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(all "${out}${err}")
if(exit_code STREQUAL "0")
    message(FATAL_ERROR "expected a non-zero exit code but the program succeeded:\n${all}")
endif()
if(NOT all MATCHES "${EXPECT_REGEX}")
    message(FATAL_ERROR "exit code ${exit_code}, but the output does not match '${EXPECT_REGEX}':\n${all}")
endif()
message(STATUS "failed as expected (exit code ${exit_code})")
