set(QVIM_TEST_DLL_PATH       "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/bin")
set(QVIM_TEST_QPA_PLUGIN_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/platforms")
set(QVIM_TEST_QML_IMPORT_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/Qt6/qml")
cmake_path(NATIVE_PATH QVIM_TEST_DLL_PATH        QVIM_TEST_DLL_PATH_NATIVE)
cmake_path(NATIVE_PATH QVIM_TEST_QPA_PLUGIN_DIR  QVIM_TEST_QPA_PLUGIN_DIR_NATIVE)
cmake_path(NATIVE_PATH QVIM_TEST_QML_IMPORT_DIR  QVIM_TEST_QML_IMPORT_DIR_NATIVE)

function(qvim_add_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE qvim_lib Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 60
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QVIM_TEST_DLL_PATH_NATIVE}"
    )
endfunction()

# Tier 1 — unit / model (guiless)
qvim_add_test(test_app_icon            unit/test_app_icon.cpp)
qvim_add_test(test_argv_parser         unit/test_argv_parser.cpp)
qvim_add_test(test_cell_metrics        unit/test_cell_metrics.cpp)
qvim_add_test(test_cmdline_pos         unit/test_cmdline_pos.cpp)
qvim_add_test(test_config_cli_reader   unit/test_config_cli_reader.cpp)
qvim_add_test(test_config_g_reader     unit/test_config_g_reader.cpp)
qvim_add_test(test_config_registry     unit/test_config_registry.cpp)
qvim_add_test(test_cursor_blink        unit/test_cursor_blink.cpp)
qvim_add_test(test_float_focusable     unit/test_float_focusable.cpp)
qvim_add_test(test_grid_model          unit/test_grid_model.cpp)
qvim_add_test(test_hidpi_rendering     unit/test_hidpi_rendering.cpp)
qvim_add_test(test_highlight_table     unit/test_highlight_table.cpp)
qvim_add_test(test_input_fuzz          unit/test_input_fuzz.cpp)
qvim_add_test(test_input_handler       unit/test_input_handler.cpp)
qvim_add_test(test_mouse_input         unit/test_mouse_input.cpp)
qvim_add_test(test_msgpack_rpc         unit/test_msgpack_rpc.cpp)
qvim_add_test(test_option_set_dispatch unit/test_option_set_dispatch.cpp)
qvim_add_test(test_popupmenu_select    unit/test_popupmenu_select.cpp)
qvim_add_test(test_resize_coalescer    unit/test_resize_coalescer.cpp)

# test_app_icon constructs QGuiApplication and needs the qico imageformat
# plugin from vcpkg's debug Qt to decode :/icon.ico. The qvim post-build step
# stages plugins next to qvim.exe; point QT_PLUGIN_PATH at that staging dir.
set(QVIM_TEST_IMAGEFORMATS_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins")
cmake_path(NATIVE_PATH QVIM_TEST_IMAGEFORMATS_DIR QVIM_TEST_IMAGEFORMATS_DIR_NATIVE)
set_tests_properties(test_app_icon PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${QVIM_TEST_QPA_PLUGIN_DIR_NATIVE};QT_PLUGIN_PATH=${QVIM_TEST_IMAGEFORMATS_DIR_NATIVE}"
)
# test_option_set_dispatch uses QTEST_MAIN (QGuiApplication) so it needs a
# resolvable platform plugin too.
set_tests_properties(test_option_set_dispatch PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${QVIM_TEST_QPA_PLUGIN_DIR_NATIVE}"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QVIM_TEST_DLL_PATH_NATIVE}"
)
# test_cell_metrics uses QTEST_MAIN (QGuiApplication is needed to construct
# QFont / QFontMetricsF) and therefore also needs a resolvable platform plugin.
set_tests_properties(test_cell_metrics PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${QVIM_TEST_QPA_PLUGIN_DIR_NATIVE}"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QVIM_TEST_DLL_PATH_NATIVE}"
)

# Tier 2 — integration (offscreen QPA, real nvim --embed)
qvim_add_test(test_attach_and_render             integration/test_attach_and_render.cpp)
qvim_add_test(test_click_focuses_float           integration/test_click_focuses_float.cpp)
qvim_add_test(test_click_skips_unfocusable_float integration/test_click_skips_unfocusable_float.cpp)
qvim_add_test(test_cmdline                       integration/test_cmdline.cpp)
qvim_add_test(test_config_g_globals              integration/test_config_g_globals.cpp)
qvim_add_test(test_insert_and_quit               integration/test_insert_and_quit.cpp)
qvim_add_test(test_messages                      integration/test_messages.cpp)
qvim_add_test(test_multigrid                     integration/test_multigrid.cpp)
qvim_add_test(test_open_file_arg                 integration/test_open_file_arg.cpp)
qvim_add_test(test_pixel_snapshot                integration/test_pixel_snapshot.cpp)
qvim_add_test(test_popupmenu                     integration/test_popupmenu.cpp)
qvim_add_test(test_resize                        integration/test_resize.cpp)
qvim_add_test(test_resize_smoothness             integration/test_resize_smoothness.cpp)
qvim_add_test(test_runtime_set_options           integration/test_runtime_set_options.cpp)
qvim_add_test(test_tabline                       integration/test_tabline.cpp)
qvim_add_test(test_title_updates                 integration/test_title_updates.cpp)
qvim_add_test(test_titlebar_color                integration/test_titlebar_color.cpp)
qvim_add_test(test_visual_on_drag                integration/test_visual_on_drag.cpp)
target_link_libraries(test_visual_on_drag PRIVATE qvim_libplugin Qt6::Quick)

# test_runtime_set_options loads Main.qml under one of its cases.
target_link_libraries(test_runtime_set_options PRIVATE qvim_libplugin Qt6::Quick)

# User-POV smoke harness loads Main.qml in a real QQuickWindow and drives it
# via QTest::keyClick — exercises the full focus chain, not just the connector.
# Needs qvim_libplugin (registers the Qvim QML module) + Qt6::Quick (QQuickWindow).
qvim_add_test(test_user_smoke        integration/test_user_smoke.cpp)
target_link_libraries(test_user_smoke PRIVATE qvim_libplugin Qt6::Quick)

# Float-click tests also load Main.qml in a real QQuickWindow and inject mouse
# events through Qt's hit-test, so they need the same QML registration link.
target_link_libraries(test_click_focuses_float           PRIVATE qvim_libplugin Qt6::Quick)
target_link_libraries(test_click_skips_unfocusable_float PRIVATE qvim_libplugin Qt6::Quick)

foreach(t test_attach_and_render test_click_focuses_float test_click_skips_unfocusable_float test_cmdline test_config_g_globals test_insert_and_quit test_messages test_multigrid test_open_file_arg test_pixel_snapshot test_popupmenu test_resize test_resize_smoothness test_runtime_set_options test_tabline test_title_updates test_titlebar_color test_user_smoke test_visual_on_drag)
    set_tests_properties(${t} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${QVIM_TEST_QPA_PLUGIN_DIR_NATIVE};QML_IMPORT_PATH=${QVIM_TEST_QML_IMPORT_DIR_NATIVE};QML2_IMPORT_PATH=${QVIM_TEST_QML_IMPORT_DIR_NATIVE};QT_SCALE_FACTOR=1;QT_AUTO_SCREEN_SCALE_FACTOR=0"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QVIM_TEST_DLL_PATH_NATIVE}"
    )
endforeach()


# Pixel-snapshot test needs to find tests/golden/ relative to the source tree.
set_tests_properties(test_pixel_snapshot PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Tier 3 — QML tests (qmltest)
qt_add_executable(test_qml qml/main_qmltest.cpp)
target_link_libraries(test_qml PRIVATE qvim_lib qvim_libplugin Qt6::QuickTest Qt6::Qml)
add_test(NAME test_qml
    COMMAND test_qml -input ${CMAKE_CURRENT_SOURCE_DIR}/qml
)
set_tests_properties(test_qml PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${QVIM_TEST_QPA_PLUGIN_DIR_NATIVE};QML_IMPORT_PATH=${QVIM_TEST_QML_IMPORT_DIR_NATIVE};QML2_IMPORT_PATH=${QVIM_TEST_QML_IMPORT_DIR_NATIVE}"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QVIM_TEST_DLL_PATH_NATIVE}"
    TIMEOUT 30
)
