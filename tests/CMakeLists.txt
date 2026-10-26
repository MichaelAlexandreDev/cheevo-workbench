add_executable(cw_tests
    test_main.cpp
    test_rcheevos.cpp)
target_link_libraries(cw_tests PRIVATE cw_devtools doctest cw_warnings)

add_test(NAME cw_tests COMMAND cw_tests)
