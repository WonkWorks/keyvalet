set(keyvalet_sources
    "app_main.c"
    "audio_sequence.c"
    "security.c"
    "settings.c"
    "settings_schema.c"
    "build_identity.c"
    "connectivity_indicator.c"
    "config_contract.c"
    "control_logic.c"
    "device_controller.c"
    "hardware_io.c"
    "hardware_profile.c"
    "input_monitor.c"
    "improv_serial.c"
    "journal.c"
    "journal_format.c"
    "journal_ring.c"
    "journal_sync.c"
    "journal_sync_contract.c"
    "lock_arbiter.c"
    "network_manager.c"
    "piezo.c"
    "replay_record.c"
    "sensor_debounce.c"
    "replay_store.c"
    "replay_window.c"
    "time_sync.c"
    "tls_material.c"
    "tls_identity.c"
    "tls_usb.c"
    "unlock_contract.c"
    "web_server.c"
    "usb_provision.c"
    "wifi_store.c")

set(keyvalet_private_dependencies
    esp_app_format
    app_update
    esp_driver_gpio
    esp_driver_ledc
    esp_driver_usb_serial_jtag
    esp_event
    esp_hw_support
    esp_http_server
    esp_https_server
    esp_netif
    esp_partition
    esp_timer
    esp_wifi
    nvs_flash
    mbedtls
    json
    spi_flash
    vfs)

if(CONFIG_KEYVALET_DIAGNOSTICS)
    set(keyvalet_build_variant "diagnostic")
    list(APPEND keyvalet_sources
         "diagnostic_console.c"
         "diagnostic_solenoid.c")
else()
    set(keyvalet_build_variant "production")
endif()

idf_component_register(SRCS ${keyvalet_sources}
                       PRIV_REQUIRES ${keyvalet_private_dependencies}
                       INCLUDE_DIRS "")

set(keyvalet_build_identity_header
    "${CMAKE_CURRENT_BINARY_DIR}/keyvalet_build_identity.h")
add_custom_target(keyvalet_build_identity ALL
    COMMAND "${PYTHON}" "${PROJECT_DIR}/tools/build_identity.py"
            --project-root "${PROJECT_DIR}"
            --variant "${keyvalet_build_variant}"
            --output "${keyvalet_build_identity_header}"
    BYPRODUCTS "${keyvalet_build_identity_header}"
    COMMENT "Generating KeyValet build identity"
    VERBATIM)
add_dependencies(${COMPONENT_LIB} keyvalet_build_identity)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
