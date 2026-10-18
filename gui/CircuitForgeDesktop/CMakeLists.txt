cmake_minimum_required(VERSION 3.21)
project(CircuitForgeDesktop VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

# Compile only parsing/validation from the engine with the GUI compiler.
# CUDA execution remains in the separately built CLI process.
add_library(DesktopUi
    App/Theme.cpp
    App/MainWindow.cpp
    Widgets/FloatingPanel.cpp
    Widgets/BenchmarkChart.cpp
    Models/CircuitModel.cpp
    Canvas/CircuitView.cpp
    Integration/EngineRunner.cpp
    ../../src/NetlistParser.cpp
    ../../src/CircuitCompiler.cpp
)
target_include_directories(DesktopUi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(DesktopUi PUBLIC Qt6::Widgets)
add_executable(CircuitForgeDesktop main.cpp)
# The old Designer main.h/main.ui are retained as historical starter files.
set_source_files_properties(main.cpp PROPERTIES SKIP_AUTOMOC ON)
target_link_libraries(CircuitForgeDesktop PRIVATE DesktopUi)
if(WIN32)
    set_target_properties(CircuitForgeDesktop PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(DesktopUi PRIVATE dwmapi)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(DesktopTests Tests/DesktopTests.cpp)
    target_link_libraries(DesktopTests PRIVATE DesktopUi Qt6::Test)
    target_compile_definitions(DesktopTests PRIVATE
        CIRCUITFORGE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
    )
    add_test(NAME DesktopWorkflows COMMAND DesktopTests -o DesktopTests.txt,txt)
    if(WIN32)
        set_tests_properties(DesktopWorkflows PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 120)
    else()
        set_tests_properties(DesktopWorkflows PROPERTIES TIMEOUT 120)
    endif()
endif()
if(MSVC)
    target_compile_options(DesktopUi PUBLIC /utf-8)
endif()
