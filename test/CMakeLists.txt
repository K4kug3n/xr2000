set(TEST_EXECUTABLE_NAME "test_xr2000")

file(GLOB TEST_SOURCES "*.cpp")

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(${TEST_EXECUTABLE_NAME} ${TEST_SOURCES})
target_link_libraries(${TEST_EXECUTABLE_NAME} PUBLIC
    gtest_main
	xr2000_lib
)

target_include_directories(${TEST_EXECUTABLE_NAME} PRIVATE
	${PROJECT_SOURCE_DIR}/include
)

include(GoogleTest)
gtest_discover_tests(${TEST_EXECUTABLE_NAME})
