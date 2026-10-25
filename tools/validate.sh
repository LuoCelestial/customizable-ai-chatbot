#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir
    local gc_sections="-Wl,--gc-sections"
    if [[ "$(uname -s)" == "Darwin" ]]; then gc_sections="-Wl,-dead_strip"; fi

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_assistant_state.c main/assistant_state.c \
        -o "${test_dir}/test_assistant_state"
    "${test_dir}/test_assistant_state"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_assistant_wifi_policy.c main/assistant_wifi_policy.c \
        -o "${test_dir}/test_assistant_wifi_policy"
    "${test_dir}/test_assistant_wifi_policy"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/audio_stubs -Imain \
        tests/test_assistant_settings.c main/assistant_settings.c main/assistant_profile.c main/assistant_memory.c \
        -o "${test_dir}/test_assistant_settings"
    "${test_dir}/test_assistant_settings"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_assistant_memory.c main/assistant_memory.c \
        -o "${test_dir}/test_assistant_memory"
    "${test_dir}/test_assistant_memory"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${gc_sections}" \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_voice_gateway.py'
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_assistant_font.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_assistant_wifi_preset.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local crypto_libs=(-lm)
    if [[ "$(uname -s)" != "Darwin" ]]; then crypto_libs=(-lm -lcrypto); fi
    local validation_build_dir
    if [[ -f "${repo_root}/.local/assistant-wifi.json" ]]; then umask 077; fi

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror \
        -Itests/assistant_client_stubs -Imain -I"${IDF_PATH}/components/json/cJSON" \
        tests/test_assistant_client.c main/assistant_client.c main/assistant_memory.c main/assistant_profile.c main/assistant_tencent.c \
        "${IDF_PATH}/components/json/cJSON/cJSON.c" "${crypto_libs[@]}" \
        -o "${validation_build_dir}/test_assistant_client"
    "${validation_build_dir}/test_assistant_client"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror \
        -Itests/assistant_client_stubs -Imain -I"${IDF_PATH}/components/json/cJSON" \
        tests/test_assistant_storage.c main/assistant_storage.c main/assistant_settings.c main/assistant_profile.c main/assistant_memory.c \
        "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm \
        -o "${validation_build_dir}/test_assistant_storage"
    "${validation_build_dir}/test_assistant_storage"

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    local firmware_mode=0644
    if [[ -f "${repo_root}/.local/assistant-wifi.json" ]]; then firmware_mode=0600; fi
    install -m "${firmware_mode}" \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
