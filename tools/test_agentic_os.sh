#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
PYTHON=${PYTHON:-python3}
LIBS=${ARDUINO_LIBRARIES:-$HOME/Documents/Arduino/libraries}
CXX=${CXX:-/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++}
NFS_LIB=$("$PYTHON" "$ROOT/tools/prepare_os_dependencies.py")
SDK=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export CPLUS_INCLUDE_PATH="$NFS_LIB/src:$SDK/usr/include/c++/v1${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
mkdir -p build/previews
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$LIBS/ArduinoJson/src" tests/os_core_test.cpp -o build/os-core-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$LIBS/ArduinoJson/src" -I"$ROOT/firmware/AgenticOS/src" tests/os_interaction_test.cpp -o build/os-interaction-test
build/os-interaction-test
build/os-core-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$LIBS/ArduinoJson/src" -I"$ROOT/firmware/AgenticOS/src" tests/os_retention_test.cpp -o build/os-retention-test
build/os-retention-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$ROOT/firmware/AgenticOS/src" tests/os_voice_test.cpp -o build/os-voice-test
build/os-voice-test
"$CXX" -isysroot "$SDK" -isystem "$SDK/usr/include/c++/v1" -std=c++17 -I"$ROOT/firmware/AgenticOS/src" -I"$LIBS/ArduinoJson/src" tests/os_sd_clock_test.cpp -o build/os-sd-clock-test
build/os-sd-clock-test
"$CXX" -isysroot "$SDK" -isystem "$SDK/usr/include/c++/v1" -std=c++17 -I"$ROOT/firmware/AgenticOS/src" -I"$LIBS/ArduinoJson/src" tests/os_sd_link_test.cpp -o build/os-sd-link-test
build/os-sd-link-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$ROOT/firmware/AgenticOS/src" -I"$LIBS/ArduinoJson/src" tests/os_contract_test.cpp -o build/os-contract-test
build/os-contract-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$ROOT/tests/os-stubs" -I"$ROOT/firmware/AgenticOS/src" -I"$LIBS/ArduinoJson/src" -I"$LIBS/SdFat/src" tests/os_sd_provider_test.cpp "$NFS_LIB/src/Names.cpp" "$LIBS/SdFat/src/common/upcase.cpp" -o build/os-sd-provider-test
build/os-sd-provider-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$ROOT/firmware/AgenticOS/src" -I"$LIBS/ArduinoJson/src" tests/os_nfs_test.cpp "$NFS_LIB/src/ESP32NFS.cpp" "$NFS_LIB/src/ExportMap.cpp" "$NFS_LIB/src/Digest.cpp" "$NFS_LIB/src/Memory.cpp" "$NFS_LIB/src/Names.cpp" -o build/os-nfs-test
"$CXX" -isysroot "$SDK" -std=c++17 -pthread -I"$ROOT/firmware/AgenticOS/src" -I"$LIBS/ArduinoJson/src" tests/os_nfs_socket_fixture.cpp "$NFS_LIB/src/ESP32NFS.cpp" "$NFS_LIB/src/ExportMap.cpp" "$NFS_LIB/src/Digest.cpp" "$NFS_LIB/src/Memory.cpp" "$NFS_LIB/src/Names.cpp" -o build/os-nfs-socket
"$PYTHON" -m unittest discover -s tests -p 'test_os_nfs_socket.py' -v
build/os-nfs-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$ROOT/firmware/AgenticOS/src" -I"$LIBS/ArduinoJson/src" tests/os_nfs_scaling_test.cpp "$NFS_LIB/src/Memory.cpp" "$NFS_LIB/src/Names.cpp" -o build/os-nfs-scaling-test
build/os-nfs-scaling-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$ROOT/firmware/AgenticOS/src" -I"$LIBS/ArduinoJson/src" tests/os_network_test.cpp -o build/os-network-test
build/os-network-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$ROOT/firmware/AgenticOS/src" -I"$LIBS/ArduinoJson/src" tests/os_http_lifecycle_test.cpp -o build/os-http-lifecycle-test
build/os-http-lifecycle-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$ROOT/firmware/AgenticOS/src" -I"$LIBS/ArduinoJson/src" tests/os_wifi_test.cpp -o build/os-wifi-test
build/os-wifi-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$ROOT/firmware/AgenticOS/src" -I"$LIBS/ArduinoJson/src" tests/os_ble_config_test.cpp -o build/os-ble-config-test
build/os-ble-config-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$ROOT/tests/radio-stubs" -I"$ROOT/firmware/AgenticOS/src" tests/os_radio_mux_test.cpp firmware/AgenticOS/src/radio/RadioMux.cpp -o build/os-radio-mux-test
build/os-radio-mux-test
"$PYTHON" -m unittest discover -s tests -p 'test_os_client.py' -v
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$LIBS/ArduinoJson/src" tests/os_runtime_test.cpp -o build/os-runtime-test
build/os-runtime-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$LIBS/ArduinoJson/src" tests/os_power_test.cpp -o build/os-power-test
build/os-power-test
"$CXX" -isysroot "$SDK" -std=c++17 -fsanitize=address,undefined -I"$LIBS/ArduinoJson/src" tests/os_conditions_test.cpp -o build/os-conditions-test
build/os-conditions-test
"$PYTHON" -m unittest discover -s tests -p 'test_os_assets.py' -v
cmake -S tests/os-ui -B build/os-ui -G Ninja -DCMAKE_C_COMPILER="${CXX%++}" -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_OSX_SYSROOT="$SDK" -DCMAKE_BUILD_TYPE=Debug
cmake --build build/os-ui -j 4
build/os-ui/notes_ui_test
build/os-ui/notes_lua_test
build/os-ui/notes_app_test
build/os-ui/notes_failure_test
"$PYTHON" -m unittest discover -s tests -p 'test_board_packages.py' -v


ESP32_NFS_LIBRARY="$NFS_LIB" ARDUINO_LIBRARIES="$LIBS" cmake -S tests/os-fat -B build/os-fat -G Ninja -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_OSX_SYSROOT="$SDK"
cmake --build build/os-fat -j 4
build/os-fat/notes_fat_test
