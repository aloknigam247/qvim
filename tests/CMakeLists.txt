set(QVIM_TEST_DLL_PATH       "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/bin")
set(QVIM_TEST_QPA_PLUGIN_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/platforms")
set(QVIM_TEST_QML_IMPORT_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/Qt6/qml")
cmake_path(NATIVE_PATH QVIM_TEST_DLL_PATH        QVIM_TEST_DLL_PATH_NATIVE)
cmake_path(NATIVE_PATH QVIM_TEST_QPA_PLUGIN_DIR  QVIM_TEST_QPA_PLUGIN_DIR_NATIVE)
cmake_path(NATIVE_PATH QVIM_TEST_QML_IMPORT_DIR  QVIM_TEST_QML_IMPORT_DIR_NATIVE)

function(qvim_add_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE qvim_lib Qt6::Test Qt6::Quick)
    # tests/support/QuickRasterizer.h is included as "support/QuickRasterizer.h".
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 60
        LABELS "${QVIM_TEST_LABEL}"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QVIM_TEST_DLL_PATH_NATIVE}"
    )
endfunction()

# ctest LABELS drive the parallel CI test tiers (see .github/workflows/ci.yml).
# qvim_add_test stamps each test with the current ${QVIM_TEST_LABEL}; tests
# registered by raw add_test() below (test_qml, test_deploy_manifest) are
# labelled explicitly. Every test MUST carry exactly one tier label so the CI
# union guard (unit + integration + qml == `ctest -N`) holds.

# Tier 1 — unit / model (guiless)
set(QVIM_TEST_LABEL unit)
qvim_add_test(test_app_icon            unit/test_app_icon.cpp)
qvim_add_test(test_argv_parser         unit/test_argv_parser.cpp)
qvim_add_test(test_cell_metrics        unit/test_cell_metrics.cpp)
qvim_add_test(test_cmdline_pos         unit/test_cmdline_pos.cpp)
qvim_add_test(test_config_cli_reader   unit/test_config_cli_reader.cpp)
qvim_add_test(test_config_g_reader     unit/test_config_g_reader.cpp)
qvim_add_test(test_config_registry     unit/test_config_registry.cpp)
qvim_add_test(test_copilot_bridge      unit/test_copilot_bridge.cpp)
qvim_add_test(test_cursor_blink        unit/test_cursor_blink.cpp)
qvim_add_test(test_cursor_item         unit/test_cursor_item.cpp)
qvim_add_test(test_float_focusable     unit/test_float_focusable.cpp)
qvim_add_test(test_grid_model          unit/test_grid_model.cpp)
qvim_add_test(test_grid_runs           unit/test_grid_runs.cpp)
qvim_add_test(test_hidpi_rendering     unit/test_hidpi_rendering.cpp)
qvim_add_test(test_highlight_table     unit/test_highlight_table.cpp)
qvim_add_test(test_input_fuzz          unit/test_input_fuzz.cpp)
qvim_add_test(test_input_handler       unit/test_input_handler.cpp)
qvim_add_test(test_mdns_advertise      unit/test_mdns_advertise.cpp)
qvim_add_test(test_mouse_input         unit/test_mouse_input.cpp)
qvim_add_test(test_msgpack_rpc         unit/test_msgpack_rpc.cpp)
qvim_add_test(test_option_set_dispatch unit/test_option_set_dispatch.cpp)
qvim_add_test(test_popupmenu_select    unit/test_popupmenu_select.cpp)
qvim_add_test(test_resize_coalescer    unit/test_resize_coalescer.cpp)
qvim_add_test(test_rounded_highlight   unit/test_rounded_highlight.cpp)
qvim_add_test(test_scroll_perf_micro   unit/test_scroll_perf_micro.cpp)
qvim_add_test(test_session_cache       unit/test_session_cache.cpp)
qvim_add_test(test_session_event_buffer unit/test_session_event_buffer.cpp)
qvim_add_test(test_text_node_pool      unit/test_text_node_pool.cpp)

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
set(QVIM_TEST_LABEL integration)
qvim_add_test(test_attach_and_render             integration/test_attach_and_render.cpp)
qvim_add_test(test_boot_no_resize_jump           integration/test_boot_no_resize_jump.cpp)
qvim_add_test(test_boot_perf                     integration/test_boot_perf.cpp)
qvim_add_test(test_chat_panel                    integration/test_chat_panel.cpp)
qvim_add_test(test_click_focuses_float           integration/test_click_focuses_float.cpp)
qvim_add_test(test_click_skips_unfocusable_float integration/test_click_skips_unfocusable_float.cpp)
qvim_add_test(test_cmdline                       integration/test_cmdline.cpp)
qvim_add_test(test_config_g_globals              integration/test_config_g_globals.cpp)
qvim_add_test(test_ctrl_shift_mapping            integration/test_ctrl_shift_mapping.cpp)
qvim_add_test(test_insert_and_quit               integration/test_insert_and_quit.cpp)
qvim_add_test(test_messages                      integration/test_messages.cpp)
qvim_add_test(test_multigrid                     integration/test_multigrid.cpp)
qvim_add_test(test_open_file_arg                 integration/test_open_file_arg.cpp)
qvim_add_test(test_pixel_snapshot                integration/test_pixel_snapshot.cpp)
qvim_add_test(test_popupmenu                     integration/test_popupmenu.cpp)
qvim_add_test(test_resize                        integration/test_resize.cpp)
qvim_add_test(test_resize_smoothness             integration/test_resize_smoothness.cpp)
qvim_add_test(test_runtime_set_options           integration/test_runtime_set_options.cpp)
qvim_add_test(test_scroll_perf                   integration/test_scroll_perf.cpp)
target_link_libraries(test_scroll_perf PRIVATE qvim_libplugin Qt6::Quick)
qvim_add_test(test_session_mirror                integration/test_session_mirror.cpp)
qvim_add_test(test_tabline                       integration/test_tabline.cpp)
qvim_add_test(test_title_updates                 integration/test_title_updates.cpp)
qvim_add_test(test_titlebar_color                integration/test_titlebar_color.cpp)
qvim_add_test(test_visual_on_drag                integration/test_visual_on_drag.cpp)
target_link_libraries(test_visual_on_drag PRIVATE qvim_libplugin Qt6::Quick)
qvim_add_test(test_wheel_scroll                  integration/test_wheel_scroll.cpp)
target_link_libraries(test_wheel_scroll PRIVATE qvim_libplugin Qt6::Quick)
qvim_add_test(test_window_snap                   integration/test_window_snap.cpp)
target_link_libraries(test_window_snap PRIVATE qvim_libplugin Qt6::Quick)

# test_runtime_set_options loads Main.qml under one of its cases.
target_link_libraries(test_runtime_set_options PRIVATE qvim_libplugin Qt6::Quick)

# User-POV smoke harness loads Main.qml in a real QQuickWindow and drives it
# via QTest::keyClick — exercises the full focus chain, not just the connector.
# Needs qvim_libplugin (registers the Qvim QML module) + Qt6::Quick (QQuickWindow).
qvim_add_test(test_user_smoke        integration/test_user_smoke.cpp)
target_link_libraries(test_user_smoke PRIVATE qvim_libplugin Qt6::Quick)

# Chat panel end-to-end also loads Main.qml and drives the focus chain via
# mouse/key clicks — same QML registration link requirement.
target_link_libraries(test_chat_panel PRIVATE qvim_libplugin Qt6::Quick)

# Float-click tests also load Main.qml in a real QQuickWindow and inject mouse
# events through Qt's hit-test, so they need the same QML registration link.
target_link_libraries(test_click_focuses_float           PRIVATE qvim_libplugin Qt6::Quick)
target_link_libraries(test_click_skips_unfocusable_float PRIVATE qvim_libplugin Qt6::Quick)

target_link_libraries(test_boot_no_resize_jump PRIVATE qvim_libplugin Qt6::Quick)
target_link_libraries(test_boot_perf           PRIVATE qvim_libplugin Qt6::Quick)

foreach(t test_attach_and_render test_boot_no_resize_jump test_boot_perf test_chat_panel test_click_focuses_float test_click_skips_unfocusable_float test_cmdline test_config_g_globals test_ctrl_shift_mapping test_insert_and_quit test_messages test_multigrid test_open_file_arg test_popupmenu test_resize test_resize_smoothness test_runtime_set_options test_scroll_perf test_session_mirror test_tabline test_title_updates test_titlebar_color test_user_smoke test_visual_on_drag test_wheel_scroll test_window_snap)
    set_tests_properties(${t} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${QVIM_TEST_QPA_PLUGIN_DIR_NATIVE};QML_IMPORT_PATH=${QVIM_TEST_QML_IMPORT_DIR_NATIVE};QML2_IMPORT_PATH=${QVIM_TEST_QML_IMPORT_DIR_NATIVE};QT_SCALE_FACTOR=1;QT_AUTO_SCREEN_SCALE_FACTOR=0;QVIM_SESSION_MIRROR_ADDRESS=127.0.0.1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QVIM_TEST_DLL_PATH_NATIVE}"
    )
endforeach()


# Perf tests assert hard wall-clock ceilings (test_boot_perf: boot < 3000ms;
# test_scroll_perf_micro: scroll median < 500us; test_scroll_perf: 100 j-presses
# < 3000ms). Under `ctest --parallel` they must not share the CPU with other
# tests or the timing goes noisy and they flake. RUN_SERIAL makes ctest schedule
# each one alone even inside a parallel run — they still execute and still catch
# regressions, just never concurrently. QVIM_SKIP_PERF=1 remains the escape hatch
# for genuinely slow machines.
set_tests_properties(test_boot_perf test_scroll_perf test_scroll_perf_micro PROPERTIES
    RUN_SERIAL TRUE
)


# Pixel-snapshot test needs to find tests/golden/ relative to the source tree.
# It is deliberately NOT in the minimal-QPA list above: it drives no nvim, and
# the minimal platform has an empty font database and cannot grab a window, so
# under it the "snapshot" would be a blank image that no font regression could
# ever disturb.
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
    LABELS "qml"
    TIMEOUT 30
)

# Pixel-probe tests address the rendered image in logical coordinates and, for
# test_pixel_snapshot, compare it against a golden stored at 1:1. Disable Qt's
# high-DPI scaling so one logical pixel is one device pixel regardless of the
# host's display scale; without this the golden is not portable between
# machines. QuickRasterizer::isUnscaled() makes a missing setting a hard fail.
set_tests_properties(
    test_hidpi_rendering
    test_pixel_snapshot
    test_rounded_highlight
    test_text_node_pool
    PROPERTIES ENVIRONMENT "QT_ENABLE_HIGHDPI_SCALING=0;QT_SCALE_FACTOR=1;QT_AUTO_SCREEN_SCALE_FACTOR=0"
)

# Deploy-integrity check — deterministic (no launch). Guards the EXPLICIT, hand-picked Qt
# deploy list in the top-level CMakeLists.txt against two independent, derived expectations:
#   (A) every Qt6*.dll imported by qvim.exe or a deployed plugin is itself deployed, and
#   (B) every `import Qt...` in qml/ has its module directory + backing plugin deployed.
# Every other test loads Qt from the full vcpkg tree on PATH and so cannot catch a deploy
# omission; this one reads the deployed binaries + qml source directly, so it is not flaky.
# It inspects the real product exe, so it depends on the qvim target (and its POST_BUILD
# deploy) being built.
add_test(NAME test_deploy_manifest
    COMMAND pwsh -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/check_deploy.ps1"
        -DeployDir "$<TARGET_FILE_DIR:qvim>"
        -QmlSourceDir "${CMAKE_SOURCE_DIR}/qml"
)
set_tests_properties(test_deploy_manifest PROPERTIES TIMEOUT 60 LABELS "unit")
