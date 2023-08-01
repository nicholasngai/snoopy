#!/bin/sh

set -eux

cd "$(dirname "$0")/.."

BENCHMARK_DIR=benchmarks

a=bitonic
s=16777216

for t in 1 2 4 8; do
    f=$BENCHMARK_DIR/$a-sgx2-$s-128-threads$t.txt
    if [ -f "$f" ]; then
        continue
    fi

    sed -Ei "s/(\"num_blocks\"): [0-9]+/\\1: $s/;s/(\"threads\"): [0-9]+/\\1: $t/" config/distributed-sgx-sort/sort.config
    rsync -aiv --progress ./ enclave0:"$PWD/"

    ssh enclave0 "$(cat <<EOF
cd "$(echo "$PWD")/build"
./suboram/host/suboram_host ./suboram/enc/suboram_enc.signed ../config/distributed-sgx-sort/sort.config
EOF
    )" | tee "$f"
done

az vm deallocate -g enclave_group -n enclave0 --no-wait
