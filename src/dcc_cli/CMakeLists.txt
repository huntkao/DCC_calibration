add_executable(dcc_cal main.cpp)
target_link_libraries(dcc_cal PRIVATE dcc_app dcc_sim)
target_compile_options(dcc_cal PRIVATE ${DCC_WARNINGS})
