cmake_minimum_required(VERSION 3.16)
project(PokemonRed)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: UTF-8 콘솔 출력 + Unicode
# -municode 제거: main() 엔트리포인트 사용 (wmain 불필요)
add_compile_definitions(UNICODE _UNICODE)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(PokemonRed ${SOURCES})

# winmm: BGM (MCI), winpthread
target_link_libraries(PokemonRed winmm)

# 실행파일 출력 위치
set_target_properties(PokemonRed PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)
