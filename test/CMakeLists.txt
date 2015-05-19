find_package(Qt5 "${Qt5_VERSION}" REQUIRED Test)

function(add_simple_test cpp_name)
    get_filename_component(test_name "${cpp_name}" NAME_WE)
    add_executable("${test_name}" "${cpp_name}")
    target_link_libraries("${test_name}" libqmlcompmgr Qt5::Test)
    add_test("${test_name}" "${test_name}")
endfunction()

add_simple_test(tst_compositor.cpp)
