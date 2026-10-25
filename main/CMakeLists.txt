idf_component_register(
    SRCS "main.c"
         "assistant_client.c"
         "assistant_tencent.c"
         "assistant_memory.c"
         "assistant_profile.c"
         "assistant_settings.c"
         "assistant_wifi_policy.c"
         "assistant_setup.c"
         "assistant_storage.c"
         "assistant_state.c"
         "assistant_wallpaper.c"
         "assistant_tts.c"
         "assistant_tts_text.c"
         "assistant_weather.c"
         "assistant_ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_netif esp_partition esp_timer esp_wifi nvs_flash
             esp_http_client esp_http_server mbedtls json lwip
    EMBED_TXTFILES "setup.html" "assistant_persona.txt"
                   "assistant_search_prompt.txt"
                   "assistant_persona_companion.txt" "assistant_persona_expert.txt"
)
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/passport_noto_sans_sc_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/passport_kaomoji_16.c"
)

# Optional private, untracked factory Wi-Fi preset. Never print its contents.
set(assistant_wifi_preset "${CMAKE_CURRENT_LIST_DIR}/assistant_wifi_default.json")
file(GLOB assistant_local_wifi CONFIGURE_DEPENDS "${PROJECT_DIR}/.local/assistant-wifi.json")
if(assistant_local_wifi)
    set(assistant_wifi_preset "${PROJECT_DIR}/.local/assistant-wifi.json")
    idf_build_get_property(assistant_python PYTHON)
    execute_process(COMMAND "${assistant_python}" "${PROJECT_DIR}/tools/configure_assistant_wifi.py" --check
        RESULT_VARIABLE assistant_preset_result OUTPUT_QUIET ERROR_QUIET)
    if(NOT assistant_preset_result EQUAL 0)
        message(FATAL_ERROR "Private Wi-Fi preset is invalid; credentials are not logged")
    endif()
    message(STATUS "Private Wi-Fi preset enabled; generated firmware contains credentials")
endif()
configure_file("${assistant_wifi_preset}" "${CMAKE_CURRENT_BINARY_DIR}/assistant_wifi_preset.json" COPYONLY)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_BINARY_DIR}/assistant_wifi_preset.json" TEXT)
