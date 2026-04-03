# PCB Module - PCB editor and board design
set(PCB_SOURCES
    # Editor
    editor/mainwindow.cpp
    editor/mainwindow.h
    editor/pcb_api.cpp
    editor/pcb_api.h
    editor/pcb_commands.cpp
    editor/pcb_commands.h
    editor/pcb_sync_dialog.cpp
    editor/pcb_sync_dialog.h
    editor/pcb_view.cpp
    editor/pcb_view.h

    # Items
    items/pcb_item.cpp
    items/pcb_item.h
    items/component_item.cpp
    items/component_item.h
    items/trace_item.cpp
    items/trace_item.h
    items/via_item.cpp
    items/via_item.h
    items/pad_item.cpp
    items/pad_item.h
    items/copper_pour_item.cpp
    items/copper_pour_item.h
    items/ratsnest_item.h

    # Models
    models/board_model.h
    models/component_model.h
    models/trace_model.h
    models/via_model.h
    models/pad_model.h
    models/copper_pour_model.h
    models/drc_types.cpp
    models/drc_types.h
    models/length_match_types.h

    # Tools
    tools/pcb_tool.cpp
    tools/pcb_tool.h
    tools/pcb_tool_registry.cpp
    tools/pcb_tool_registry.h
    tools/pcb_tool_registry_builtin.cpp
    tools/pcb_tool_registry_builtin.h
    tools/pcb_select_tool.cpp
    tools/pcb_select_tool.h
    tools/pcb_component_tool.cpp
    tools/pcb_component_tool.h
    tools/pcb_trace_tool.cpp
    tools/pcb_trace_tool.h
    tools/pcb_via_tool.cpp
    tools/pcb_via_tool.h
    tools/pcb_pad_tool.cpp
    tools/pcb_pad_tool.h
    tools/pcb_pour_tool.cpp
    tools/pcb_pour_tool.h
    tools/pcb_rect_tool.cpp
    tools/pcb_rect_tool.h
    tools/pcb_erase_tool.cpp
    tools/pcb_erase_tool.h
    tools/pcb_measure_tool.cpp
    tools/pcb_measure_tool.h
    tools/pcb_diff_pair_tool.cpp
    tools/pcb_diff_pair_tool.h
    tools/pcb_length_tuning_tool.cpp
    tools/pcb_length_tuning_tool.h
    tools/pcb_zoom_area_tool.cpp
    tools/pcb_zoom_area_tool.h

    # Factories
    factories/pcb_item_factory.cpp
    factories/pcb_item_factory.h
    factories/pcb_item_registry.cpp
    factories/pcb_item_registry.h

    # Analysis
    analysis/drc_engine.h
    analysis/connectivity_engine.h
    analysis/signal_integrity_engine.cpp
    analysis/signal_integrity_engine.h
    analysis/pcb_ratsnest_manager.cpp
    analysis/pcb_ratsnest_manager.h
    analysis/pcb_auto_router.cpp
    analysis/pcb_auto_router.h
    analysis/length_match_manager.cpp
    analysis/length_match_manager.h
    analysis/length_measurement_engine.cpp
    analysis/length_measurement_engine.h
    analysis/serpentine_generator.cpp
    analysis/serpentine_generator.h
    analysis/pcb_diff_engine.cpp
    analysis/pcb_diff_engine.h

    # DRC
    drc/pcb_drc.cpp
    drc/pcb_drc.h
    drc/pcb_drc_panel.cpp
    drc/pcb_drc_panel.h
    drc/pcb_design_rules_editor.cpp
    drc/pcb_design_rules_editor.h

    # Layers
    layers/pcb_layer.cpp
    layers/pcb_layer.h
    layers/pcb_layer_panel.cpp
    layers/pcb_layer_panel.h

    # UI
    ui/pcb_property_editor.cpp
    ui/pcb_property_editor.h
    ui/pcb_components_widget.cpp
    ui/pcb_components_widget.h
    ui/pcb_3d_view.cpp
    ui/pcb_3d_view.h
    ui/pcb_3d_window.cpp
    ui/pcb_3d_window.h
    ui/footprint_preview_view.cpp
    ui/footprint_preview_view.h

    # Dialogs
    dialogs/board_setup_dialog.cpp
    dialogs/board_setup_dialog.h
    dialogs/gerber_export_dialog.cpp
    dialogs/gerber_export_dialog.h
    dialogs/via_stitching_dialog.cpp
    dialogs/via_stitching_dialog.h
    dialogs/netlist_import_dialog.cpp
    dialogs/netlist_import_dialog.h
    dialogs/pick_place_export_dialog.cpp
    dialogs/pick_place_export_dialog.h
    dialogs/auto_router_dialog.cpp
    dialogs/auto_router_dialog.h
    dialogs/length_matching_dialog.cpp
    dialogs/length_matching_dialog.h
    dialogs/pcb_diff_viewer.cpp
    dialogs/pcb_diff_viewer.h

    # Import
    import/netlist_importer.cpp
    import/netlist_importer.h

    # Gerber
    gerber/gerber_layer.h
    gerber/gerber_parser.cpp
    gerber/gerber_parser.h
    gerber/gerber_exporter.cpp
    gerber/gerber_exporter.h
    gerber/gerber_view.cpp
    gerber/gerber_view.h
    gerber/gerber_3d_view.cpp
    gerber/gerber_3d_view.h
    gerber/gerber_viewer_window.cpp
    gerber/gerber_viewer_window.h

    # IO
    io/pcb_file_io.cpp
    io/pcb_file_io.h

    # Manufacturing
    manufacturing/manufacturing_exporter.cpp
    manufacturing/manufacturing_exporter.h

    # MCAD
    mcad/mcad_exporter.cpp
    mcad/mcad_exporter.h

    # Plugins
    plugins/pcb_plugin.h
    plugins/pcb_plugin_manager.cpp
    plugins/pcb_plugin_manager.h
)

add_library(VioraPCBCore STATIC ${PCB_SOURCES})
set_target_properties(VioraPCBCore PROPERTIES AUTOMOC ON)
target_include_directories(VioraPCBCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/items
    ${CMAKE_CURRENT_SOURCE_DIR}/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/models
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/drc
    ${CMAKE_CURRENT_SOURCE_DIR}/layers
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/import
    ${CMAKE_CURRENT_SOURCE_DIR}/gerber
    ${CMAKE_CURRENT_SOURCE_DIR}/io
    ${CMAKE_CURRENT_SOURCE_DIR}/manufacturing
    ${CMAKE_CURRENT_SOURCE_DIR}/mcad
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins
    ${CMAKE_CURRENT_SOURCE_DIR}/factories
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
    ${CMAKE_CURRENT_SOURCE_DIR}/../python
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_link_libraries(VioraPCBCore PUBLIC
    FluxCore
    FluxFootprints
    FluxHeadless
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::PrintSupport
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::OpenGL
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
    Qt${QT_VERSION_MAJOR}::Svg
    Qt${QT_VERSION_MAJOR}::Charts
)
