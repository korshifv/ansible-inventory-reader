cmake_minimum_required(VERSION 3.24)

project(ansible-inventory-studio
    VERSION 0.1.0
    DESCRIPTION "Native Qt/QML editor and graph viewer for Ansible YAML inventories"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)
find_package(yaml-cpp CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(ansible-inventory-studio
    src/main.cpp
    src/inventorydocument.cpp
    src/inventorydocument.h
    src/inventorytreemodel.cpp
    src/inventorytreemodel.h
)

qt_add_qml_module(ansible-inventory-studio
    URI AnsibleInventoryStudio
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/GraphView.qml
)

target_link_libraries(ansible-inventory-studio PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    yaml-cpp::yaml-cpp
)

target_compile_options(ansible-inventory-studio PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

install(TARGETS ansible-inventory-studio
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
