#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash test/run_ears_log_tests.sh

task_test_dir=$(mktemp -d /tmp/piuu-host-tests.XXXXXX)
trap 'rm -rf "$task_test_dir"' EXIT

g++ -std=c++11 -Wall -Wextra -I test/ui/mocks -I test/services/mocks -I src test/ui/test_landscape.cpp src/ui/screens/*.cpp src/ui/widgets/*.cpp src/ui/ui_model.cpp -o "$task_test_dir/landscape"
"$task_test_dir/landscape"

g++ -std=c++11 -Wall -Wextra -I src test/eyes/test_plan.cpp src/eyes/plan.cpp src/eyes/engine/{eyes,gestures}.cpp src/network/session_client.cpp src/protocol/session_protocol.cpp -o "$task_test_dir/eyes_plan"
"$task_test_dir/eyes_plan"

g++ -std=c++11 -Wall -Wextra -I src test/eyes/test_control.cpp src/eyes/control.cpp src/eyes/engine/{eyes,gestures,command_values}.cpp src/protocol/session_protocol.cpp -o "$task_test_dir/eyes_control"
"$task_test_dir/eyes_control"
g++ -std=c++11 -Wall -Wextra -I test/eyes/mocks -I src/eyes/engine test/eyes/test_renderer.cpp src/eyes/engine/{eyes,contour,renderer}.cpp -o "$task_test_dir/eyes_renderer"
"$task_test_dir/eyes_renderer"
g++ -std=c++11 -Wall -Wextra -I test/eyes/mocks -I test/services/mocks -I src test/eyes/test_runtime.cpp src/eyes/{control,runtime,plan}.cpp src/eyes/engine/{eyes,gestures,command_values,contour,renderer}.cpp src/protocol/session_protocol.cpp -o "$task_test_dir/eyes_runtime"
"$task_test_dir/eyes_runtime"

g++ -std=c++11 -Wall -Wextra -I src test/downlink_recovery/test_recovery.cpp -o "$task_test_dir/downlink_recovery"
"$task_test_dir/downlink_recovery"

g++ -std=c++11 -Wall -Wextra -I src test/diagnostics/test_dac_trace.cpp -o "$task_test_dir/dac_trace"
"$task_test_dir/dac_trace"

g++ -std=c++11 -pthread -Wall -Wextra -I src test/diagnostics/test_audio_progress.cpp -o "$task_test_dir/audio_progress"
"$task_test_dir/audio_progress"

g++ -std=c++11 -Wall -Wextra -I src test/diagnostics/test_metrics.cpp -o "$task_test_dir/metrics"
"$task_test_dir/metrics"

g++ -std=c++11 -pthread -Wall -Wextra -I src test/diagnostics/test_media_rx.cpp src/protocol/session_protocol.cpp -o "$task_test_dir/media_rx"
"$task_test_dir/media_rx"

for diagnostic_mode in network playback off; do
    network_diagnostics=0; playback_diagnostics=0
    if [[ "$diagnostic_mode" == network ]]; then network_diagnostics=1; fi
    if [[ "$diagnostic_mode" == playback ]]; then playback_diagnostics=1; fi
    g++ -std=c++11 -pthread -Wall -Wextra -DPIUU_NETWORK_DIAGNOSTICS="$network_diagnostics" -DPIUU_PLAYBACK_DIAGNOSTICS="$playback_diagnostics" -I test/network_udp/mocks -I test/services/mocks -I src test/network_udp/test_network_udp.cpp src/network/network_udp.cpp -o "$task_test_dir/network_udp"
    "$task_test_dir/network_udp"
done

g++ -std=c++11 -Wall -Wextra -I src src/protocol/server_protocol.cpp test/protocol/test_protocol.cpp -o "$task_test_dir/protocol"
"$task_test_dir/protocol" "$task_test_dir/pcm.bin" "$task_test_dir/end.bin"
g++ -std=c++11 -I test/uplink -I src src/audio/audio_uplink.cpp src/protocol/server_protocol.cpp test/uplink/test_uplink.cpp -o "$task_test_dir/uplink"
"$task_test_dir/uplink"
for playback_fault_diagnostics in 0 1; do
g++ -std=c++11 -pthread -Wall -Wextra -DPIUU_TEST_CAPTURE_LOG -DPIUU_PLAYBACK_DIAGNOSTICS="$playback_fault_diagnostics" -I test/services/mocks -I src test/services/test_services.cpp src/app/app_services.cpp src/app/conversation.cpp src/network/server_link.cpp src/network/session_client.cpp src/protocol/session_protocol.cpp src/audio/audio_uplink.cpp src/protocol/server_protocol.cpp src/ui/ui_status.cpp src/ui/ui_model.cpp test/services/mocks/runtime.cpp -o "$task_test_dir/services"
"$task_test_dir/services"

done

g++ -std=c++11 -Wall -Wextra -I test/services/mocks -I src test/settings/test_settings.cpp src/config/settings.cpp src/storage/storage_prefs.cpp -o "$task_test_dir/settings"
"$task_test_dir/settings"

for dac_diagnostics in 0 1; do
    g++ -std=c++11 -pthread -Wall -Wextra -DPIUU_DAC_DIAGNOSTICS="$dac_diagnostics" -I test/audio_output/mocks -I test/services/mocks -I src test/audio_output/test_audio_output.cpp src/audio/audio_output.cpp src/audio/volume_control.cpp -o "$task_test_dir/audio_output"
    "$task_test_dir/audio_output"
done

g++ -std=c++11 -Wall -Wextra -I src test/playback/test_completion.cpp -o "$task_test_dir/completion"
"$task_test_dir/completion"

g++ -std=c++11 -Wall -Wextra -I src test/silence/test_silence.cpp -o "$task_test_dir/silence"
"$task_test_dir/silence"

g++ -std=c++11 -Wall -Wextra -I src test/touch/test_gesture.cpp -o "$task_test_dir/gesture"
"$task_test_dir/gesture"

g++ -std=c++11 -pthread -Wall -Wextra -DPIUU_WIFI_DIAGNOSTICS=1 -I test/services/mocks -I src test/wifi/test_wifi.cpp test/services/mocks/runtime.cpp src/network/wifi_service.cpp src/diagnostics/wifi_history.cpp -o "$task_test_dir/wifi"
"$task_test_dir/wifi"

for wifi_diagnostics in 1 0; do
    g++ -std=c++11 -pthread -Wall -Wextra -DPIUU_WIFI_DIAGNOSTICS="$wifi_diagnostics" -I test/services/mocks -I src test/wifi/test_history.cpp src/diagnostics/wifi_history.cpp -o "$task_test_dir/wifi_history"
    "$task_test_dir/wifi_history"
done

g++ -std=c++11 -pthread -Wall -Wextra -I ../C3_EARS/test/link_host -I src -I ../C3_EARS/src test/ears_protocol/test_ears_protocol.cpp src/protocol/ears_protocol.cpp ../C3_EARS/src/core_link.cpp ../C3_EARS/src/core_log_relay.cpp ../C3_EARS/src/audio_protocol.cpp ../C3_EARS/src/ears_state.cpp -o "$task_test_dir/ears_protocol"
"$task_test_dir/ears_protocol"
for conversation_diagnostics in 0 1; do
    g++ -std=c++11 -pthread -Wall -Wextra -DPIUU_DIAGNOSTICS="$conversation_diagnostics" -DPIUU_CONVERSATION_DIAGNOSTICS="$conversation_diagnostics" -I test/services/mocks -I src test/ears_session/test_ears_session.cpp test/services/mocks/runtime.cpp src/app/ears_session.cpp src/app/conversation.cpp src/audio/audio_uplink.cpp src/protocol/server_protocol.cpp -o "$task_test_dir/ears_session"
    "$task_test_dir/ears_session"
done

g++ -std=c++11 -pthread -Wall -Wextra -DPIUU_TEST_SERIAL_UART -DPIUU_EARS_UART0=1 -DPIUU_EARS_RX_PIN=3 -DPIUU_EARS_TX_PIN=1 -DPIUU_PERFORMANCE_DIAGNOSTICS=0 -DPIUU_UART_DIAGNOSTICS=0 -I test/ears_link/mocks -I test/services/mocks -I src test/ears_link/test_ears_link.cpp test/services/mocks/runtime.cpp src/input/ears_link.cpp src/protocol/ears_protocol.cpp -o "$task_test_dir/ears_uart0"
"$task_test_dir/ears_uart0"

for diagnostics_mode in perf uart off; do
    performance_diagnostics=0; uart_diagnostics=0
    if [[ "$diagnostics_mode" == perf ]]; then performance_diagnostics=1; fi
    if [[ "$diagnostics_mode" == uart ]]; then uart_diagnostics=1; fi
    g++ -std=c++11 -pthread -Wall -Wextra -DPIUU_TEST_SERIAL_UART -DPIUU_PERFORMANCE_DIAGNOSTICS="$performance_diagnostics" -DPIUU_UART_DIAGNOSTICS="$uart_diagnostics" -I test/ears_link/mocks -I test/services/mocks -I src test/ears_link/test_ears_link.cpp test/services/mocks/runtime.cpp src/input/ears_link.cpp src/protocol/ears_protocol.cpp -o "$task_test_dir/ears_link"
    "$task_test_dir/ears_link"
done

g++ -std=c++11 -Wall -Wextra -I src src/protocol/session_protocol.cpp test/session_protocol/test_session_protocol.cpp -o "$task_test_dir/session_protocol"
"$task_test_dir/session_protocol" "$task_test_dir/core-envelope.bin"
g++ -std=c++11 -Wall -Wextra -I src src/protocol/session_protocol.cpp src/network/session_client.cpp test/session_protocol/test_session_client.cpp -o "$task_test_dir/session_client"
"$task_test_dir/session_client"

if [[ $# -gt 0 ]]; then
    backend_main="$1"
    if [[ -d "$backend_main" ]]; then backend_main="$backend_main/src/main.py"; fi
    backend_root="$(dirname "$(dirname "$backend_main")")"
    python3 test/eyes/check_backend.py "$backend_root" "$task_test_dir/eyes_plan" "$task_test_dir/eye-plan.bin" "$task_test_dir/eye-ack.bin"
    g++ -std=c++11 -Wall -Wextra -I src test/downlink_recovery/test_wire.cpp src/protocol/session_protocol.cpp src/protocol/server_protocol.cpp -o "$task_test_dir/downlink_wire"
    "$task_test_dir/downlink_wire" write "$task_test_dir/downlink-nack.bin"
    python3 test/downlink_recovery/check_backend.py "$backend_root/src" "$task_test_dir/downlink-nack.bin" "$task_test_dir/downlink-end.bin"
    "$task_test_dir/downlink_wire" read "$task_test_dir/downlink-end.bin"
    python3 test/session_protocol/check_python_recovery.py "$task_test_dir/session_client" "$backend_root/src"
    python3 test/session_protocol/check_python_session.py "$backend_root/src/bridge/transport/protocols/conversation.py" "$task_test_dir/core-envelope.bin" "$task_test_dir/server-envelope.bin" "$task_test_dir/session_client"
    "$task_test_dir/session_protocol" "$task_test_dir/core-envelope.bin" "$task_test_dir/server-envelope.bin"
    python3 test/protocol/check_python_bridge.py "$backend_main" "$task_test_dir/pcm.bin" "$task_test_dir/end.bin" "$task_test_dir/opus.bin"
    "$task_test_dir/protocol" "$task_test_dir/pcm.bin" "$task_test_dir/end.bin" "$task_test_dir/opus.bin"
fi
