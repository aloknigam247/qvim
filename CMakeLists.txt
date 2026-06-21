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
    # Treat angle-bracket includes as external (system) headers and suppress
    # their warnings. Keeps /W4 strict for our own code while letting Qt's
    # qnumeric.h / qvariant.h / qjsengine.h C4702 unreachable-code warnings
    # through without polluting our build.
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8
                        /external:anglebrackets /external:W0)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
    if(QVIM_ENABLE_ASAN)
        add_compile_options(/fsanitize=address)
    endif()
endif()

set(QVIM_LIB_SOURCES
    src/AppIcon.cpp             include/AppIcon.h
    src/ArgvParser.cpp          include/ArgvParser.h
    src/CellMetrics.cpp         include/CellMetrics.h
    src/ClipboardBridge.cpp     include/ClipboardBridge.h
    src/CmdlineModel.cpp        include/CmdlineModel.h
    src/Config.cpp              include/Config.h
    src/ConfigCliReader.cpp     include/ConfigCliReader.h
    src/ConfigGGlobalReader.cpp include/ConfigGGlobalReader.h
    src/CursorBlinkState.cpp    include/CursorBlinkState.h
    src/CursorItem.cpp          include/CursorItem.h
    src/GridItem.cpp            include/GridItem.h
    src/GridModel.cpp           include/GridModel.h
    src/HighlightTable.cpp      include/HighlightTable.h
    src/InputHandler.cpp        include/InputHandler.h
    src/MessagesModel.cpp       include/MessagesModel.h
    src/ModeInfo.cpp            include/ModeInfo.h
    src/MsgpackRpc.cpp          include/MsgpackRpc.h
    src/NvimConnector.cpp       include/NvimConnector.h
    src/PopupMenuModel.cpp      include/PopupMenuModel.h
    src/ResizeCoalescer.cpp     include/ResizeCoalescer.h
    src/SessionCache.cpp        include/SessionCache.h
    src/TablineModel.cpp        include/TablineModel.h
    src/WindowChrome.cpp        include/WindowChrome.h
)

qt_add_library(qvim_lib STATIC ${QVIM_LIB_SOURCES} resources/qvim.qrc)
target_include_directories(qvim_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(qvim_lib
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        msgpack-cxx
)
target_link_libraries(qvim_lib PRIVATE $<$<PLATFORM_ID:Windows>:dwmapi>)
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

if(WIN32)
    qt_add_executable(qvim src/main.cpp resources/qvim.rc)
else()
    qt_add_executable(qvim src/main.cpp)
endif()
target_link_libraries(qvim PRIVATE qvim_lib qvim_libplugin)
set_target_properties(qvim PROPERTIES WIN32_EXECUTABLE TRUE)

if(WIN32)
    # vcpkg's Qt install splits debug/release across separate prefixes, and
    # windeployqt can't be redirected to the debug prefix. We deploy the
    # minimum runtime ourselves: Qt DLLs, the platform plugin, and the QML
    # modules our QML files actually import.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(QVIM_VCPKG_PREFIX "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug")
        set(QVIM_QT_DLL_SUFFIX "d")
    else()
        set(QVIM_VCPKG_PREFIX "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
        set(QVIM_QT_DLL_SUFFIX "")
    endif()
    set(QVIM_QT_BIN     "${QVIM_VCPKG_PREFIX}/bin")
    set(QVIM_QT_PLUGINS "${QVIM_VCPKG_PREFIX}/Qt6/plugins")
    set(QVIM_QT_QML     "${QVIM_VCPKG_PREFIX}/Qt6/qml")
    set(QVIM_OUT_DIR    "$<TARGET_FILE_DIR:qvim>")

    # qt.conf tells Qt to look in the local dir for plugins + QML modules.
    file(WRITE "${CMAKE_BINARY_DIR}/qt.conf"
"[Paths]
Plugins = .
Qml2Imports = .
")

    # Glob all Qt6 DLLs — chasing individual dependencies is tedious and
    # the QML modules import indirect Qt libs that CMake's link-deps tracking misses.
    file(GLOB QVIM_EXTRA_QT_DLLS "${QVIM_QT_BIN}/Qt6*${QVIM_QT_DLL_SUFFIX}.dll")

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
            "${QVIM_QT_PLUGINS}/platforms/qwindows${QVIM_QT_DLL_SUFFIX}.dll"
            "${QVIM_OUT_DIR}/platforms/qwindows${QVIM_QT_DLL_SUFFIX}.dll"
        # Minimal platform (for tests under QT_QPA_PLATFORM=minimal)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QVIM_QT_PLUGINS}/platforms/qminimal${QVIM_QT_DLL_SUFFIX}.dll"
            "${QVIM_OUT_DIR}/platforms/qminimal${QVIM_QT_DLL_SUFFIX}.dll"
        # ICO image format plugin (needed to load :/icon.ico for window icon)
        COMMAND ${CMAKE_COMMAND} -E make_directory "${QVIM_OUT_DIR}/imageformats"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QVIM_QT_PLUGINS}/imageformats/qico${QVIM_QT_DLL_SUFFIX}.dll"
            "${QVIM_OUT_DIR}/imageformats/qico${QVIM_QT_DLL_SUFFIX}.dll"
        # QML modules used by qvim's QML files
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${QVIM_QT_QML}/QtQuick"          "${QVIM_OUT_DIR}/QtQuick"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${QVIM_QT_QML}/QtQml"            "${QVIM_OUT_DIR}/QtQml"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${QVIM_QT_QML}/QtTest"           "${QVIM_OUT_DIR}/QtTest"
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
