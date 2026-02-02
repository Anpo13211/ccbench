if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.10)
  project(ss2pl_standalone LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif ()

find_package(Threads REQUIRED)

add_executable(ycsb_ss2pl_standalone.exe
        flags.cc
        result.cc
        result_impl.cc
        transaction.cc
        util.cc
        ycsb_ss2pl.cc
        )

target_include_directories(ycsb_ss2pl_standalone.exe
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(ycsb_ss2pl_standalone.exe
        PRIVATE -Wall -Wextra -Werror)

target_link_libraries(ycsb_ss2pl_standalone.exe
        Threads::Threads)

if (DEFINED ADD_ANALYSIS)
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE ADD_ANALYSIS=${ADD_ANALYSIS})
else ()
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE ADD_ANALYSIS=0)
endif ()

if (DEFINED BACK_OFF)
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE BACK_OFF=${BACK_OFF})
else ()
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE BACK_OFF=1)
endif ()

if (DEFINED KEY_SIZE)
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE KEY_SIZE=${KEY_SIZE})
else ()
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE KEY_SIZE=8)
endif ()

if (DEFINED KEY_SORT)
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE KEY_SORT=${KEY_SORT})
else ()
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE KEY_SORT=0)
endif ()

if (DEFINED MASSTREE_USE)
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE MASSTREE_USE=${MASSTREE_USE})
else ()
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE MASSTREE_USE=0)
endif ()

if (DEFINED VAL_SIZE)
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE VAL_SIZE=${VAL_SIZE})
else ()
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE VAL_SIZE=4)
endif ()

if (DEFINED DLR0)
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE DLR0)
else ()
    target_compile_definitions(ycsb_ss2pl_standalone.exe PRIVATE DLR1)
endif ()
