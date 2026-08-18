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
option(QVIM_WERROR "Treat qvim's own compiler warnings as errors (CI gate)" OFF)
option(BUILD_TESTING "Build the test suites" ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest Test WebSockets)
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

# Warnings-as-errors, scoped to qvim's OWN targets only — never Qt/vcpkg/msgpack.
# A global /WX breaks on Qt headers' C4702 unreachable-code warnings even with
# /external:anglebrackets /external:W0, so it can't live in the add_compile_options
# above. Instead this interface carrier applies /WX per target and is opted in by
# CI with -DQVIM_WERROR=ON. Link it PRIVATE into each first-party target.
add_library(qvim_warnings INTERFACE)
if(MSVC)
    # /WX promotes ALL warnings to errors, including C4702 (unreachable code)
    # emitted from Qt's <QtCore/qnumeric.h> during code generation. That warning
    # is NOT suppressed by /external:anglebrackets /external:W0 (a known MSVC
    # limitation: codegen-phase diagnostics ignore /external), so it would break
    # a /WX build even though it originates entirely in Qt. Disable just C4702
    # under WERROR; every other warning in qvim's own code is still an error.
    target_compile_options(qvim_warnings INTERFACE
        $<$<BOOL:${QVIM_WERROR}>:/WX;/wd4702>)
endif()

set(QVIM_LIB_SOURCES
    src/AppIcon.cpp             include/AppIcon.h
    src/ArgvParser.cpp          include/ArgvParser.h
    src/CellMetrics.cpp         include/CellMetrics.h
    src/ChatModel.cpp           include/ChatModel.h
    src/ClipboardBridge.cpp     include/ClipboardBridge.h
    src/CmdlineModel.cpp        include/CmdlineModel.h
    src/Config.cpp              include/Config.h
    src/ConfigCliReader.cpp     include/ConfigCliReader.h
    src/ConfigGGlobalReader.cpp include/ConfigGGlobalReader.h
    src/CursorBlinkState.cpp    include/CursorBlinkState.h
    src/CursorItem.cpp          include/CursorItem.h
    src/GridItem.cpp            include/GridItem.h
    src/GridModel.cpp           include/GridModel.h
    src/GridRuns.cpp            include/GridRuns.h
    src/HighlightTable.cpp      include/HighlightTable.h
    src/InputHandler.cpp        include/InputHandler.h
    src/MessagesModel.cpp       include/MessagesModel.h
    src/ModeInfo.cpp            include/ModeInfo.h
    src/MsgpackRpc.cpp          include/MsgpackRpc.h
    src/NvimConnector.cpp       include/NvimConnector.h
    src/PopupMenuModel.cpp      include/PopupMenuModel.h
    src/RectNodePool.cpp        include/RectNodePool.h
    src/ResizeCoalescer.cpp     include/ResizeCoalescer.h
    src/SessionCache.cpp        include/SessionCache.h
    src/SessionEventBuffer.cpp  include/SessionEventBuffer.h
    src/SessionMirrorServer.cpp include/SessionMirrorServer.h
    src/TablineModel.cpp        include/TablineModel.h
    src/TextNodePool.cpp        include/TextNodePool.h
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
        Qt6::WebSockets
        msgpack-cxx
)
target_link_libraries(qvim_lib PRIVATE $<$<PLATFORM_ID:Windows>:dwmapi>)
target_link_libraries(qvim_lib PRIVATE qvim_warnings)
set_target_properties(qvim_lib PROPERTIES AUTOMOC ON)

qt_add_qml_module(qvim_lib
    URI Qvim
    VERSION 1.0
    QML_FILES
        qml/ChatPanel.qml
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
target_link_libraries(qvim PRIVATE qvim_warnings)
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

    # Explicit list of the Qt6 runtime DLLs qvim actually loads (measured from
    # Process.Modules on a real launch). This replaces a blanket
    # `file(GLOB Qt6*.dll)` that copied EVERY Qt6 module — Qt6Widgets,
    # Qt6PrintSupport, Qt6ShaderTools, every QuickControls2 style — none of which
    # qvim loads. Those unused files still cost cold-start time: Qt's plugin/QML
    # discovery stats+reads them and Windows Defender scans each on first sight.
    # VCPKG_APPLOCAL_DEPS continues to supply the non-Qt transitive deps
    # (freetype/harfbuzz/icu/pcre2/zlib/brotli/...), which windeployqt does NOT.
    #
    # GUARDED BY tests/check_deploy.ps1 (ctest: test_deploy_manifest). If you add a
    # Qt module or a QML import and forget to deploy it here, that test fails with a
    # FIX hint pointing back at this block — you never edit the test, only this list.
    set(QVIM_QT_RUNTIME_DLLS
        Qt6Core
        Qt6Gui
        Qt6Network
        Qt6Qml
        Qt6QmlMeta
        Qt6QmlModels
        Qt6QmlWorkerScript
        Qt6Quick
        Qt6WebSockets
    )
    set(QVIM_QT_DLL_PATHS "")
    foreach(_qtdll ${QVIM_QT_RUNTIME_DLLS})
        list(APPEND QVIM_QT_DLL_PATHS
            "${QVIM_QT_BIN}/${_qtdll}${QVIM_QT_DLL_SUFFIX}.dll")
    endforeach()

    add_custom_command(TARGET qvim POST_BUILD
        # qt.conf next to qvim.exe so plugin/QML lookup is self-contained
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/qt.conf"
            "${QVIM_OUT_DIR}/qt.conf"
        # Only the Qt6 modules qvim loads (not the whole Qt6*.dll set)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${QVIM_QT_DLL_PATHS}
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
        # QML modules — ONLY what qvim imports (QtQuick + QtQuick.Window) plus the
        # QtQml core they depend on. Previously the whole QtQuick tree (~70MB with
        # Controls/Dialogs/Particles/Effects/Shapes/NativeStyle + PDBs) was copied
        # wholesale — the dominant cold-start payload source.
        COMMAND ${CMAKE_COMMAND} -E make_directory "${QVIM_OUT_DIR}/QtQuick"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QVIM_QT_QML}/QtQuick/qmldir"
            "${QVIM_QT_QML}/QtQuick/plugins.qmltypes"
            "${QVIM_QT_QML}/QtQuick/qtquick2plugin${QVIM_QT_DLL_SUFFIX}.dll"
            "${QVIM_OUT_DIR}/QtQuick/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${QVIM_QT_QML}/QtQuick/Window"   "${QVIM_OUT_DIR}/QtQuick/Window"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${QVIM_OUT_DIR}/QtQml"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QVIM_QT_QML}/QtQml/qmldir"
            "${QVIM_QT_QML}/QtQml/plugins.qmltypes"
            "${QVIM_QT_QML}/QtQml/qmlplugin${QVIM_QT_DLL_SUFFIX}.dll"
            "${QVIM_OUT_DIR}/QtQml/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${QVIM_QT_QML}/QtQml/Models"        "${QVIM_OUT_DIR}/QtQml/Models"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${QVIM_QT_QML}/QtQml/WorkerScript"  "${QVIM_OUT_DIR}/QtQml/WorkerScript"
        # QtTest QML module — tier-3 qmltest needs it; harmless for the app.
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${QVIM_QT_QML}/QtTest"           "${QVIM_OUT_DIR}/QtTest"
        COMMENT "Deploying trimmed Qt runtime (loaded DLLs + imported QML modules) next to qvim.exe"
        VERBATIM
    )

    # QtQuick.Window's qmldir declares a REQUIRED (non-optional) `plugin
    # quickwindowplugin`, but Qt's debug build ships the DLL suffixed
    # (quickwindowplugind.dll) while the config-agnostic qmldir keeps the base
    # name. tests/check_deploy.ps1 resolves that declared name literally, so a
    # debug deploy looks for quickwindowplugin.dll and fails. Deploy an unsuffixed
    # copy of the debug plugin so the declared name resolves — it is a valid debug
    # binary, and Qt still prefers the suffixed variant at runtime. Release already
    # ships quickwindowplugin.dll, so this is debug-only (QVIM_QT_DLL_SUFFIX set).
    if(QVIM_QT_DLL_SUFFIX)
        add_custom_command(TARGET qvim POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QVIM_QT_QML}/QtQuick/Window/quickwindowplugin${QVIM_QT_DLL_SUFFIX}.dll"
                "${QVIM_OUT_DIR}/QtQuick/Window/quickwindowplugin.dll"
            COMMENT "Deploying unsuffixed QtQuick.Window plugin alias for the debug deploy-manifest check"
            VERBATIM
        )
    endif()
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
