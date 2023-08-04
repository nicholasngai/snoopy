#!/bin/sh

set -eux

cd "$(dirname "$0")/.."

BENCHMARK_DIR=benchmarks
SUBSCRIPTION=7fd7e4ed-48d3-4cab-8df3-436e7c7cfed1
GROUP=enclave_group

if [ -n "${AZ+x}" ]; then
    export AZDCAP_DEBUG_LOG_LEVEL=0
    AZ=true
else
    if uname -r | grep -q azure; then
        fold -s <<EOF
It looks like you're running on Azure. If you want to automatically deallocate VMs, you should re-run this script as

    AZ=true $0

Hit Enter to continue without automatic deallocation or Ctrl-C to exit.
EOF
        read _
    fi
    AZ=false
fi

mkdir -p "$BENCHMARK_DIR"

kill_snoopy() {
    first=$1
    last=$2
    i=$first
    pids=
    while [ "$i" -le "$last" ]; do
        ssh enclave$i "{ killall -KILL load_balancer_host; killall suboram_host; } >/dev/null 2>/dev/null" &
        pids=${pids:+$pids }$!
        i=$(( i + 1 ))
    done
    wait $pids || :
}

cleanup() {
    kill_snoopy 0 32

    vm_ids=
    i=0
    while [ "$i" -le 32 ]; do
        vm_ids="${vm_ids:+$vm_ids }/subscriptions/$SUBSCRIPTION/resourceGroups/$GROUP/providers/Microsoft.Compute/virtualMachines/enclave$i"
        i=$(( i + 1 ))
    done
    az vm deallocate --no-wait --ids $vm_ids

    kill -KILL $snoopy_pids
}
trap cleanup INT QUIT TERM EXIT

last_e=
for e in 32 16 8 4 2 1; do
    if "$AZ"; then
        if [ -n "$last_e" ]; then
            vm_ids=
            i=$(( e + 1 ))
            while [ "$i" -le "$last_e" ]; do
                vm_ids="${vm_ids:+$vm_ids }/subscriptions/$SUBSCRIPTION/resourceGroups/$GROUP/providers/Microsoft.Compute/virtualMachines/enclave$i"
                i=$(( i + 1 ))
            done
            az vm deallocate --no-wait --ids $vm_ids
        fi
    fi

    for latency in 125 250 500 1000 2000 4000; do
        filename=$BENCHMARK_DIR/enclaves$e-size16777216-latency$latency.txt
        if [ -f "$filename" ]; then
            continue
        fi

        # Set latency in configs.
        sed -Ei "s/(\"max_latency_ms\"): [0-9]+/\\1: $latency/" config/distributed-sgx-sort/*/lb.config
        ./scripts/sync.sh 0 "$e"

        snoopy_pids=

        # Spawn suborams.
        i=0
        while [ "$i" -lt "$e" ]; do
            ssh "enclave$(( i + 1 ))" "cd snoopy/build && ./suboram/host/suboram_host ./suboram/enc/suboram_enc.signed ../config/distributed-sgx-sort/$e/suboram$i.config" &
            snoopy_pids=${snoopy_pids:+$snoopy_pids }$!
            i=$(( i + 1 ))
        done

        # Spawn load balancer.
        ssh enclave0 "cd snoopy/build && ./load_balancer/host/load_balancer_host ./load_balancer/enc/load_balancer_enc.signed ../config/distributed-sgx-sort/$e/lb.config" | tee "$filename" &
        snoopy_pids="$snoopy_pids $!"

        # Wait 10 seconds.
        sleep 10

        # Spawn client.
        ./build/client/client "config/distributed-sgx-sort/$e/client.config"

        # Clean up.
        kill_snoopy 0 "$e"
    done

    last_e=$e
done
