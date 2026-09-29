#!/usr/bin/env bash
# Build and run the score keeper's test: host/hiscore/run.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
cc -O1 -g -Wall -fsanitize=address,undefined -Ihost/hiscore -Icomponents/medalboot/include \
   -o /tmp/pelletino_test_hiscore host/hiscore/test_hiscore.c components/medalboot/src/hiscore.c
exec /tmp/pelletino_test_hiscore
