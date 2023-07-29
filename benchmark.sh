#!/bin/sh

set -eux

( cd build && make -j )
./scripts/sync.sh 0 32

for e in 32 16 8 4 2 1; do
    # Spawn suborams.
    i=0
    while [ "$i" -lt "$e" ]; do
        ssh "enclave$(( i + 1 ))" "cd snoopy/build && ./suboram/host/suboram_host ./suboram/enc/suboram_enc.signed ../config/distributed-sgx-sort/$e/suboram$i.config" &
    done
done
