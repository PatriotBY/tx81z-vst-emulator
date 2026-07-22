cmake_minimum_required(VERSION 3.20)
project(tx81z_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(tx81z_cpu_core STATIC
    src/cpu_core.cpp
)
target_include_directories(tx81z_cpu_core PUBLIC include)

add_library(tx81z_system STATIC
    src/hd44780.cpp
    src/tx81z_bus.cpp
    third_party/ymfm/src/ymfm_opz.cpp
)
target_include_directories(tx81z_system PUBLIC include third_party/ymfm/src)
target_link_libraries(tx81z_system PUBLIC tx81z_cpu_core)

enable_testing()

add_executable(test_cpu tests/test_cpu.cpp)
target_link_libraries(test_cpu PRIVATE tx81z_cpu_core)
add_test(NAME cpu_core_sanity COMMAND test_cpu)

add_executable(test_system tests/test_system.cpp)
target_link_libraries(test_system PRIVATE tx81z_system)
add_test(NAME system_wiring COMMAND test_system)

add_executable(boot_rom tools/boot_rom.cpp)
target_link_libraries(boot_rom PRIVATE tx81z_system)

add_executable(render_note tools/render_note.cpp)
target_link_libraries(render_note PRIVATE tx81z_system)

add_executable(direct_opz_test tools/direct_opz_test.cpp)
target_link_libraries(direct_opz_test PRIVATE tx81z_system)
