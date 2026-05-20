cmake_minimum_required(VERSION 4.2)
project(qvim LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(QVIM_ENABLE_ASAN "Build with MSVC AddressSanitizer" OFF)
option(BUILD_TESTING "Build the test suites" ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest Test)
find_package(msgpack-cxx CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.10)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
    if(QVIM_ENABLE_ASAN)
        add_compile_options(/fsanitize=address)
    endif()
endif()

set(QVIM_LIB_SOURCES
    src/MsgpackRpc.cpp        src/MsgpackRpc.h
    src/NvimConnector.cpp     src/NvimConnector.h
    src/HighlightTable.cpp    src/HighlightTable.h
    src/MessagesModel.cpp     src/MessagesModel.h
    src/ModeInfo.cpp          src/ModeInfo.h
    src/GridModel.cpp         src/GridModel.h
    src/GridItem.cpp          src/GridItem.h
    src/InputHandler.cpp      src/InputHandler.h
    src/ClipboardBridge.cpp   src/ClipboardBridge.h
    src/TablineModel.cpp      src/TablineModel.h
    src/PopupMenuModel.cpp    src/PopupMenuModel.h
    src/CmdlineModel.cpp      src/CmdlineModel.h
    src/RecentProjectsModel.cpp src/RecentProjectsModel.h
)

qt_add_library(qvim_lib STATIC ${QVIM_LIB_SOURCES})
target_include_directories(qvim_lib PUBLIC src)
target_link_libraries(qvim_lib
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        msgpack-cxx
)
set_target_properties(qvim_lib PROPERTIES AUTOMOC ON)

qt_add_qml_module(qvim_lib
    URI Qvim
    VERSION 1.0
    QML_FILES
        qml/Cmdline.qml
        qml/Main.qml
        qml/Messages.qml
        qml/PopupMenu.qml
        qml/Shell.qml
        qml/Tabline.qml
)

qt_add_executable(qvim src/main.cpp)
target_link_libraries(qvim PRIVATE qvim_lib qvim_libplugin)
set_target_properties(qvim PROPERTIES WIN32_EXECUTABLE TRUE)

if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    # vcpkg's Qt install splits debug/release across separate prefixes, and
    # windeployqt can't be redirected to the debug prefix. We deploy the
    # minimum runtime ourselves: Qt DLLs, the platform plugin, and the QML
    # modules our QML files actually import.
    set(QVIM_DEBUG_BIN     "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/bin")
    set(QVIM_DEBUG_PLUGINS "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins")
    set(QVIM_DEBUG_QML     "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/Qt6/qml")
    set(QVIM_OUT_DIR       "$<TARGET_FILE_DIR:qvim>")

    # qt.conf tells Qt to look in the local dir for plugins + QML modules.
    file(WRITE "${CMAKE_BINARY_DIR}/qt.conf"
"[Paths]
Plugins = .
Qml2Imports = .
")

    # Glob all Qt6 debug DLLs — chasing individual dependencies is tedious and
    # the QML modules import indirect Qt libs that CMake's link-deps tracking misses.
    file(GLOB QVIM_EXTRA_QT_DLLS "${QVIM_DEBUG_BIN}/Qt6*d.dll")

    add_custom_command(TARGET qvim POST_BUILD
        # qt.conf next to qvim.exe so plugin/QML lookup is self-contained
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/qt.conf"
            "${QVIM_OUT_DIR}/qt.conf"
        # Extra Qt DLLs needed by QML modules (CMake's link-deps tracking misses these)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${QVIM_EXTRA_QT_DLLS}
            "${QVIM_OUT_DIR}/"
        # Platform plugin (always required)
        COMMAND ${CMAKE_COMMAND} -E make_directory "${QVIM_OUT_DIR}/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QVIM_DEBUG_PLUGINS}/platforms/qwindowsd.dll"
            "${QVIM_OUT_DIR}/platforms/qwindowsd.dll"
        # Minimal platform (for tests under QT_QPA_PLATFORM=minimal)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QVIM_DEBUG_PLUGINS}/platforms/qminimald.dll"
            "${QVIM_OUT_DIR}/platforms/qminimald.dll"
        # QML modules used by qvim's QML files
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${QVIM_DEBUG_QML}/QtQuick"          "${QVIM_OUT_DIR}/QtQuick"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${QVIM_DEBUG_QML}/QtQml"            "${QVIM_OUT_DIR}/QtQml"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${QVIM_DEBUG_QML}/QtTest"           "${QVIM_OUT_DIR}/QtTest"
        COMMENT "Deploying qt.conf + platform plugin + QML modules next to qvim.exe"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Release deployment ---
# For non-Debug builds, use Qt's deployment helper to gather all runtime
# dependencies at install time. windeployqt doesn't support vcpkg's debug
# prefix layout, so we only run this for Release-style builds.
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND WIN32)
    qt_generate_deploy_app_script(
        TARGET qvim
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()

install(TARGETS qvim
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)
