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
qvim_add_test(test_cmdline_pos      unit/test_cmdline_pos.cpp)
qvim_add_test(test_grid_model       unit/test_grid_model.cpp)
qvim_add_test(test_hidpi_rendering  unit/test_hidpi_rendering.cpp)
qvim_add_test(test_highlight_table  unit/test_highlight_table.cpp)
qvim_add_test(test_input_fuzz       unit/test_input_fuzz.cpp)
qvim_add_test(test_input_handler    unit/test_input_handler.cpp)
qvim_add_test(test_msgpack_rpc      unit/test_msgpack_rpc.cpp)
qvim_add_test(test_popupmenu_select unit/test_popupmenu_select.cpp)

# Tier 2 — integration (offscreen QPA, real nvim --embed)
qvim_add_test(test_attach_and_render integration/test_attach_and_render.cpp)
qvim_add_test(test_cmdline           integration/test_cmdline.cpp)
qvim_add_test(test_insert_and_quit   integration/test_insert_and_quit.cpp)
qvim_add_test(test_messages          integration/test_messages.cpp)
qvim_add_test(test_multigrid         integration/test_multigrid.cpp)
qvim_add_test(test_pixel_snapshot    integration/test_pixel_snapshot.cpp)
qvim_add_test(test_popupmenu         integration/test_popupmenu.cpp)
qvim_add_test(test_resize            integration/test_resize.cpp)
qvim_add_test(test_tabline           integration/test_tabline.cpp)

# User-POV smoke harness loads Main.qml in a real QQuickWindow and drives it
# via QTest::keyClick — exercises the full focus chain, not just the connector.
# Needs qvim_libplugin (registers the Qvim QML module) + Qt6::Quick (QQuickWindow).
qvim_add_test(test_user_smoke        integration/test_user_smoke.cpp)
target_link_libraries(test_user_smoke PRIVATE qvim_libplugin Qt6::Quick)

foreach(t test_attach_and_render test_cmdline test_insert_and_quit test_messages test_multigrid test_pixel_snapshot test_popupmenu test_resize test_tabline test_user_smoke)
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
