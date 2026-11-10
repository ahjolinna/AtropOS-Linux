#!/bin/bash
# AtropOS regression benchmark. Runs a fixed Phoronix Test Suite set in the
# atropos-bench distrobox and adds the run to one result file, so every run
# sits next to the baseline.
#
#   scripts/benchmark.sh <label>        e.g. baseline, plasma-6.9, iommu-off
#
# Run it on the host with the desktop idle. It takes about an hour.
# Results stay local: batch mode has UploadResults=FALSE. To publish one
# on OpenBenchmarking.org, run "phoronix-test-suite upload-result
# atropos-regression" in the distrobox yourself.
#
# The distrobox (Slowroll userspace, NVIDIA passed through):
#   distrobox create -n atropos-bench \
#       -i registry.opensuse.org/opensuse/tumbleweed:latest --nvidia
#   then switch its repositories to Slowroll and install
#   phoronix-test-suite, as described in benchmarks/README.md.
set -euo pipefail

label=${1:?usage: $0 <label>}
box=atropos-bench
result=atropos-regression
root=$(cd "$(dirname "$0")/.." && pwd)
stamp=$(date +%Y-%m-%d)
identifier="${stamp}-${label}"
out=$root/benchmarks/$identifier
mkdir -p "$out"

# The host state that can change a result. The container's rpm database
# is not the host's, so this runs here, before entering the distrobox.
{
    echo "identifier: $identifier"
    echo "date: $(date -Is)"
    echo "kernel: $(uname -r)"
    echo "cmdline: $(cat /proc/cmdline)"
    echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)"
    echo "epp: $(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference 2>/dev/null)"
    # A BIOS change shows here: without the cpb flag the firmware has turned
    # off Core Performance Boost (a manual all-core frequency does that), and
    # boost stays 0 whatever tuned asks for.
    echo "boost: $(cat /sys/devices/system/cpu/cpufreq/boost 2>/dev/null) cpb-flag: $(grep -m1 -qw cpb /proc/cpuinfo && echo yes || echo no)"
    echo "cppc-nominal-mhz: $(cat /sys/devices/system/cpu/cpu0/acpi_cppc/nominal_freq 2>/dev/null)"
    echo "selinux-enforcing: $(cat /sys/fs/selinux/enforce 2>/dev/null || echo unknown)"
    echo "tuned-profile: $(cat /etc/tuned/active_profile 2>/dev/null || echo unknown)"
    for k in vm.swappiness vm.dirty_ratio vm.watermark_scale_factor \
             kernel.split_lock_mitigate net.ipv4.tcp_congestion_control; do
        echo "sysctl $k: $(cat "/proc/sys/${k//.//}" 2>/dev/null)"
    done
    echo "mitigations:"
    grep -H . /sys/devices/system/cpu/vulnerabilities/* 2>/dev/null | sed 's|.*/||; s/^/  /'
    echo "packages:"
    rpm -q --qf '  %{NAME}-%{VERSION}-%{RELEASE}\n' \
        kernel-longterm kernel-default atropos-performance patterns-atropos-base \
        AtropOS-release plasma6-workspace kwin6 mesa nvidia-video-G07 \
        glibc systemd 2>/dev/null | grep -v 'not installed' || true
    echo "container:"
    distrobox enter "$box" -- bash -c \
        'echo "  $(. /etc/os-release; echo "$NAME $VERSION_ID")"; echo "  $(gcc --version | head -1)"; echo "  $(phoronix-test-suite version 2>/dev/null | tail -1)"'
} > "$out/system.txt"

# Each entry: test, then its options. The options are fixed so every run
# measures the same thing. Every option needs a preset: Phoronix 10.8.4
# crashes in batch mode on an option without one (pts_test_run_options.php
# uses $option_names before it is set). Do not change them; add a new entry instead,
# or older runs no longer line up.
tests=(
    "pts/compress-7zip|"
    "pts/build-linux-kernel|build-linux-kernel.build=defconfig"
    "pts/x265|x265.video=Bosphorus 4K"
    "pts/stream|stream.run-type=Triad"
    "pts/fio|fio.type=Random Read;fio.engine=IO_uring;fio.direct=Yes;fio.size=4KB;fio.cpu-threads=1;fio.auto-disk-mount-points=Default Test Directory"
    "pts/fio|fio.type=Sequential Write;fio.engine=IO_uring;fio.direct=Yes;fio.size=1MB;fio.cpu-threads=1;fio.auto-disk-mount-points=Default Test Directory"
)

for entry in "${tests[@]}"; do
    test=${entry%%|*}
    preset=${entry#*|}
    echo "== $test ${preset:+($preset)}"
    distrobox enter "$box" -- env \
        TEST_RESULTS_NAME="$result" \
        TEST_RESULTS_IDENTIFIER="$identifier" \
        TEST_RESULTS_DESCRIPTION="AtropOS regression benchmark" \
        PRESET_OPTIONS="$preset" \
        phoronix-test-suite batch-benchmark "$test" < /dev/null
done

# A text copy of the whole result file, for the repository history.
distrobox enter "$box" -- phoronix-test-suite result-file-to-text "$result" \
    2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' > "$out/results.txt" || true
echo "Saved $out"
