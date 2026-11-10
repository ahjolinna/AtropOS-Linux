# AtropOS regression benchmarks

`scripts/benchmark.sh <label>` runs a fixed Phoronix Test Suite set and
adds the run to the local result file `atropos-regression`. Each run gets
the identifier `<date>-<label>`, so every run sits next to the baseline.
Each run also writes `benchmarks/<date>-<label>/system.txt` with the
kernel, command line, governor, tuned profile, SELinux mode, sysctl values,
CPU mitigations and package versions, and `results.txt` with the whole
result file as text.

Test machine: AMD Ryzen 7 5700G, NVIDIA RTX 3060 Ti, LUKS + btrfs on NVMe.

## Test set

| Test | Options | Measures |
|---|---|---|
| pts/compress-7zip | | CPU, integer |
| pts/build-linux-kernel | defconfig | CPU and file system, SELinux overhead |
| pts/x265 | Bosphorus 4K | CPU, vector |
| pts/stream | Triad | memory bandwidth |
| pts/fio | Random Read, IO_uring, direct, 4KB, 1 job, default test directory | disk latency through LUKS and btrfs |
| pts/fio | Sequential Write, IO_uring, direct, 1MB, 1 job, default test directory | disk throughput |

Do not change an entry's options. Older runs would no longer line up. Add
a new entry instead.

## The distrobox

Phoronix runs in the `atropos-bench` distrobox. It shares the host
kernel and its settings, so CPU, memory and disk results are the host's,
and the read-only root stays untouched. Phoronix compiles the tests with
the container's compiler, so the container follows Slowroll like the host.

```
distrobox create -n atropos-bench \
    -i registry.opensuse.org/opensuse/tumbleweed:latest --nvidia
distrobox enter atropos-bench
# remove the Tumbleweed repositories, then:
sudo zypper ar -f http://download.opensuse.org/slowroll/repo/oss/ slowroll-oss
sudo zypper ar -f -p 80 http://download.opensuse.org/update/slowroll/repo/oss/ slowroll-update
sudo zypper dup --allow-downgrade --allow-vendor-change
sudo zypper in phoronix-test-suite php8-cli php8-zip php8-dom php8-gd \
    php8-openssl php8-curl php8-sqlite gcc gcc-c++ make cmake autoconf \
    automake bison flex libopenssl-devel zlib-devel bc libaio-devel \
    gawk which libelf-devel 7zip nasm yasm perl diffutils findutils hostname
```

The kernel build needs awk and libelf-devel, and x265 needs 7z to unpack
its input video. The container image has none of them.

In `~/.phoronix-test-suite/user-config.xml`, batch mode has
`UploadResults` FALSE and `AnonymousUsageReporting` is FALSE. Nothing is
published unless you run `phoronix-test-suite upload-result
atropos-regression` yourself.

## Comparing with OpenBenchmarking.org

- `phoronix-test-suite ob_auto_compare.debug atropos-regression` ranks
  each result against all public results for the same test and options.
  It sends OpenBenchmarking.org only the test and option hash and the
  system type; the results stay local. Save its output as
  `openbenchmarking.txt` in the run's folder. The public samples are
  mostly servers and newer desktops, so the percentiles are not a
  comparison with the same CPU. (`make-openbenchmarking-cache` does not
  help here: it only mirrors test profiles.)
- To compare with one public result, pick its ID on openbenchmarking.org
  and run `phoronix-test-suite benchmark <ID>` in the distrobox. It runs
  the same tests with the same options and merges the results.
