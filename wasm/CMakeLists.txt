cmake_minimum_required(VERSION 3.15)
project(TestToneDSP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(testtone_dsp
    src/wasm_exports.cpp
)

target_include_directories(testtone_dsp PRIVATE src)

if(EMSCRIPTEN)
    set_target_properties(testtone_dsp PROPERTIES
        SUFFIX ".wasm"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
    )

    # WASM から JS 側に公開する関数。リーディングアンダースコアは emscripten の慣習。
    set(EXPORTED_FUNCTIONS
        # 初期化 / 解放
        "_dsp_init" "_dsp_destroy"
        # ヒープ alloc（AudioWorklet 側のスクラッチ確保用）
        "_dsp_alloc_buffer" "_dsp_free_buffer" "_malloc" "_free"
        # パラメータ
        "_dsp_set_type" "_dsp_set_frequency_hz" "_dsp_set_level_dbfs"
        "_dsp_set_on" "_dsp_set_channel_enabled"
        # 処理
        "_dsp_process_block" "_dsp_reset"
    )
    list(JOIN EXPORTED_FUNCTIONS "," EXPORTED_FUNCTIONS_STR)

    target_link_options(testtone_dsp PRIVATE
        "-sSTANDALONE_WASM=1"
        "-sEXPORTED_FUNCTIONS=[${EXPORTED_FUNCTIONS_STR}]"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=2097152"
        "-sSTACK_SIZE=65536"
        "-sWASM=1"
        "--no-entry"
        "-O3"
    )
endif()
