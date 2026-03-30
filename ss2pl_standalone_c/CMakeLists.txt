if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.10)
  project(ss2pl_standalone_c LANGUAGES C)
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
endif ()

find_package(Threads REQUIRED)
include(CTest)

add_executable(ycsb_ss2pl_standalone_c
        main.c
        transaction.c
        util.c
        result.c
        zipf.c
        )

target_include_directories(ycsb_ss2pl_standalone_c
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(ycsb_ss2pl_standalone_c
        PRIVATE -Wall -Wextra -Werror)

target_link_libraries(ycsb_ss2pl_standalone_c
        Threads::Threads
        m)

if (DEFINED VAL_SIZE)
    target_compile_definitions(ycsb_ss2pl_standalone_c PRIVATE VAL_SIZE=${VAL_SIZE})
else ()
    target_compile_definitions(ycsb_ss2pl_standalone_c PRIVATE VAL_SIZE=4)
endif ()

if (DEFINED DLR0)
    target_compile_definitions(ycsb_ss2pl_standalone_c PRIVATE DLR0)
else ()
    target_compile_definitions(ycsb_ss2pl_standalone_c PRIVATE DLR1)
endif ()

if (BUILD_TESTING)
    add_executable(ss2pl_regression
            regression.c
            transaction.c
            )

    target_include_directories(ss2pl_regression
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    target_compile_options(ss2pl_regression
            PRIVATE -Wall -Wextra -Werror)

    target_link_libraries(ss2pl_regression
            Threads::Threads)

    if (DEFINED VAL_SIZE)
        target_compile_definitions(ss2pl_regression PRIVATE VAL_SIZE=${VAL_SIZE})
    else ()
        target_compile_definitions(ss2pl_regression PRIVATE VAL_SIZE=4)
    endif ()

    if (DEFINED DLR0)
        target_compile_definitions(ss2pl_regression PRIVATE DLR0)
    else ()
        target_compile_definitions(ss2pl_regression PRIVATE DLR1)
    endif ()

    add_test(NAME ss2pl_regression COMMAND ss2pl_regression)
endif ()
