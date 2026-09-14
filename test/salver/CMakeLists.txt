add_executable(SalverUtilTest
  SalverUtilTest.cpp
  ${REPO_ROOT}/src/salver/SalverUtil.cpp
)

target_include_directories(SalverUtilTest PRIVATE
  ${REPO_ROOT}/src/salver
)

target_link_libraries(SalverUtilTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SalverUtilTest)
