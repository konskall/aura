# «Αύρα» on the device (2026-11-20, ADR 0011): microWakeWord's streaming model
# over the microphone's voice with TensorFlow Lite Micro and microWakeWord's
# own features. The model (tools/wakeword) is built in when model/avra.tflite
# exists; without it the firmware builds the same and offers no wake word.
set(aura_wake_word_model "${CMAKE_CURRENT_LIST_DIR}/model/avra.tflite")
set(aura_wake_word_embed)
if(EXISTS "${aura_wake_word_model}")
    list(APPEND aura_wake_word_embed EMBED_FILES "${aura_wake_word_model}")
endif()

idf_component_register(
    SRCS "src/esp32_wake_word.cpp"
    INCLUDE_DIRS "include"
    REQUIRES aura_shared
    PRIV_REQUIRES espressif__esp-tflite-micro esphome__esp-micro-speech-features esp_timer freertos heap log pthread
    ${aura_wake_word_embed}
)

if(aura_wake_word_embed)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE AURA_WAKE_WORD_MODEL=1)
    message(STATUS "AURA: the «Αύρα» wake word model is built in")
else()
    message(STATUS "AURA: no model/avra.tflite: the device offers no wake word")
endif()
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
