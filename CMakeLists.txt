cmake_minimum_required(VERSION 3.24)

project(ansible-inventory-studio
    VERSION 0.2.2
    DESCRIPTION "Native Qt/QML editor and graph viewer for Ansible YAML inventories"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2)
find_package(yaml-cpp CONFIG REQUIRED)

if(TARGET yaml-cpp::yaml-cpp)
    set(YAML_CPP_TARGET yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
    set(YAML_CPP_TARGET yaml-cpp)
else()
    message(FATAL_ERROR "yaml-cpp was found, but no supported CMake target was exported")
endif()

qt_standard_project_setup()

qt_add_executable(ansible-inventory-studio
    src/main.cpp
    src/inventorydocument_io.cpp
    src/inventorydocument_edit.cpp
    src/inventorydocument_graph.cpp
    src/inventorydocument_exclusions.cpp
    src/inventorydocument_ping_filtered.cpp
    src/inventorydocument.h
    src/inventorytreemodel.cpp
    src/inventorytreemodel.h
)

qt_add_qml_module(ansible-inventory-studio
    URI AnsibleInventoryStudio
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/GraphView.qml
)

target_link_libraries(ansible-inventory-studio PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    ${YAML_CPP_TARGET}
)

target_compile_options(ansible-inventory-studio PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

install(TARGETS ansible-inventory-studio
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# Package metadata shared by all release builders.
set(CPACK_PACKAGE_NAME "ansible-inventory-studio")
set(CPACK_PACKAGE_VENDOR "Korshi")
set(CPACK_PACKAGE_CONTACT "Korshi")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt editor and graph viewer for Ansible YAML inventories")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/korshifv/ansible-inventory-reader")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Korshi")
set(CPACK_DEBIAN_PACKAGE_SECTION "admin")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-dialogs, qml6-module-qtquick-layouts, qml6-module-qtquick-templates, qml6-module-qtquick-window, qml6-module-qtqml-workerscript"
)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-only")
set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative >= 6.4, yaml-cpp")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

include(CPack)
