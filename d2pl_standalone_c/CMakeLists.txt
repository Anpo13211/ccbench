if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.10)
  project(d2pl_standalone_c LANGUAGES C)
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
endif ()

find_package(Threads REQUIRED)
include(CTest)

add_executable(ycsb_d2pl_standalone_c
        main.c
        transaction.c
        util.c
        result.c
        zipf.c
        )

target_include_directories(ycsb_d2pl_standalone_c
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(ycsb_d2pl_standalone_c
        PRIVATE -Wall -Wextra -Werror)

target_link_libraries(ycsb_d2pl_standalone_c
        Threads::Threads
        m)

if (DEFINED VAL_SIZE)
    target_compile_definitions(ycsb_d2pl_standalone_c PRIVATE VAL_SIZE=${VAL_SIZE})
else ()
    target_compile_definitions(ycsb_d2pl_standalone_c PRIVATE VAL_SIZE=4)
endif ()

if (BUILD_TESTING)
    add_executable(d2pl_regression
            regression.c
            transaction.c
            )

    target_include_directories(d2pl_regression
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    target_compile_options(d2pl_regression
            PRIVATE -Wall -Wextra -Werror)

    target_link_libraries(d2pl_regression
            Threads::Threads)

    if (DEFINED VAL_SIZE)
        target_compile_definitions(d2pl_regression PRIVATE VAL_SIZE=${VAL_SIZE})
    else ()
        target_compile_definitions(d2pl_regression PRIVATE VAL_SIZE=4)
    endif ()

    add_test(NAME d2pl_regression COMMAND d2pl_regression)
endif ()
