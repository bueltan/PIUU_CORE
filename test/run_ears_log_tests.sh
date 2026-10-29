#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_test_dir=$(mktemp -d /tmp/piuu-ears-log.XXXXXX)
trap 'rm -rf "$task_test_dir"' EXIT
cmp src/diagnostics/core_log_wire.h ../C3_EARS/src/core_log_wire.h
g++ -std=c++11 -pthread -Wall -Wextra -DPIUU_EARS_LOG=1 -I test/ears_log/mocks -I src test/ears_log/test_ears_log.cpp src/hardware/ears_log.cpp -o "$task_test_dir/core"
"$task_test_dir/core" "$task_test_dir/frames.bin"
g++ -std=c++11 -pthread -Wall -Wextra -I ../C3_EARS/test/link_host -I ../C3_EARS/src ../C3_EARS/test/log_relay/test_relay.cpp ../C3_EARS/src/core_link.cpp ../C3_EARS/src/core_log_relay.cpp ../C3_EARS/src/audio_protocol.cpp ../C3_EARS/src/ears_state.cpp -o "$task_test_dir/ears"
"$task_test_dir/ears" "$task_test_dir/frames.bin"
