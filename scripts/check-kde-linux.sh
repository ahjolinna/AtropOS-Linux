#!/bin/bash
# Read-only post-install check for the AtropOS ports of KDE Linux.
# Run it on the installed desktop. It does not change the system.
#sed -i 's/\r$//' ~/Downloads/test.sh
# Mount options are written by Agama from products/atropos.yaml. An
# install made before that change keeps its old fstab, so those two
# lines stay missing until the next install.

set -u

pass=0
miss=0
skip=0

section() {
    printf '\n%s\n' "$1"
}

ok() {
    printf '  ok    %s\n' "$1"
    pass=$((pass + 1))
}

bad() {
    printf '  miss  %s\n' "$1"
    miss=$((miss + 1))
}

skipped() {
    printf '  skip  %s\n' "$1"
    skip=$((skip + 1))
}

# Agama boots systemd-boot from the ESP and does not put that mount in fstab.
# The firmware still names the loader when the ESP is not mounted.
esp_used_by_systemd_boot() {
    local bin loader_info text
    for bin in /usr/bin/bootctl /usr/sbin/bootctl; do
        [[ -x "$bin" ]] || continue
        # is-installed opens the ESP directory. Agama leaves that unmounted,
        # and a mounted vfat often has dmask=0077, so this fails for a user.
        if "$bin" is-installed >/dev/null 2>&1; then
            return 0
        fi
        # status reads the LoaderInfo EFI variable and does not need the ESP.
        if "$bin" status 2>/dev/null | grep -q 'systemd-boot'; then
            return 0
        fi
    done
    for bin in /usr/bin/sdbootutil /usr/sbin/sdbootutil; do
        [[ -x "$bin" ]] || continue
        if PATH="/usr/sbin:/usr/bin:$PATH" "$bin" is-installed >/dev/null 2>&1; then
            return 0
        fi
    done
    for loader_info in /sys/firmware/efi/efivars/LoaderInfo-*; do
        [[ -f "$loader_info" ]] || continue
        if grep -a -q 'systemd-boot' "$loader_info" 2>/dev/null; then
            return 0
        fi
        # efivarfs: 4-byte attribute header, then UTF-16LE text.
        text=$(tail -c +5 "$loader_info" 2>/dev/null | iconv -f UTF-16LE -t UTF-8 2>/dev/null || true)
        if [[ "$text" == *systemd-boot* ]]; then
            return 0
        fi
    done
    return 1
}

# Comma-separated mount options, or a space-separated command line.
has_token() {
    local haystack=$1 needle=$2
    case ",${haystack}," in
        *",${needle},"*) return 0 ;;
    esac
    case " ${haystack} " in
        *" ${needle} "*) return 0 ;;
        *) return 1 ;;
    esac
}

sysctl_is() {
    local key=$1 want=$2 got want_norm
    got=$(/usr/sbin/sysctl -n "$key" 2>/dev/null | tr '\t' ' ' || true)
    want_norm=$(printf '%s' "$want" | tr '\t' ' ')
    if [[ "$got" == "$want_norm" ]]; then
        ok "$key = $want_norm"
    else
        bad "$key is '${got:-unset}', want $want_norm"
    fi
}

unit_enabled() {
    local unit=$1 state
    state=$(systemctl is-enabled "$unit" 2>/dev/null || true)
    if [[ "$state" == enabled ]]; then
        ok "$unit enabled"
    else
        bad "$unit is '${state:-missing}', want enabled"
    fi
}

unit_active() {
    local unit=$1 state
    state=$(systemctl is-active "$unit" 2>/dev/null || true)
    if [[ "$state" == active ]]; then
        ok "$unit active"
    else
        bad "$unit is '${state:-missing}', want active"
    fi
}

unit_disabled() {
    local unit=$1 state
    state=$(systemctl is-enabled "$unit" 2>/dev/null || true)
    case "$state" in
        disabled|masked|static)
            ok "$unit $state"
            ;;
        "")
            bad "$unit is not installed"
            ;;
        *)
            bad "$unit is '$state', want disabled"
            ;;
    esac
}

file_has() {
    local path=$1 text=$2 label=$3
    if [[ ! -f "$path" ]]; then
        bad "$label ($path missing)"
        return
    fi
    if grep -F -q -- "$text" "$path"; then
        ok "$label"
    else
        bad "$label (file present, '$text' missing)"
    fi
}

# Last uncommented assignment wins. Skip paths that are not files:
# gawk exits before printing if any argument is missing, so one absent
# drop-in directory used to hide a value that was already read.
effective_value() {
    local key=$1
    shift
    local files=() path
    for path in "$@"; do
        if [[ "$path" == - ]]; then
            files+=("-")
            continue
        fi
        [[ -f "$path" ]] || continue
        files+=("$path")
    done
    if ((${#files[@]} == 0)); then
        return 0
    fi
    awk -v key="$key" '
        $0 ~ /^[[:space:]]*#/ { next }
        $0 ~ ("^[[:space:]]*" key "[[:space:]]*=") {
            value = $0
            sub(/^[^=]*=[[:space:]]*/, "", value)
            gsub(/\r/, "", value)
        }
        END { printf "%s", value }
    ' "${files[@]}"
}

# systemd stores ManagedOOMMemoryPressureLimit as a fraction of 2^32.
# 60% is 2576980377. systemctl show prints that number, not "60%".
pressure_is() {
    local slice=$1 percent=$2 got raw
    got=$(systemctl show "$slice" -p ManagedOOMMemoryPressureLimit --value 2>/dev/null || true)
    raw=$(( (percent * 4294967296) / 100 ))
    if [[ "$got" == "${percent}%" || "$got" == "$raw" || "$got" == "$((raw + 1))" ]]; then
        ok "$slice memory pressure limit is ${percent}%"
    else
        bad "$slice memory pressure limit is '${got:-unset}', want ${percent}%"
    fi
}

# Kalpa's findmnt line, plus the two options Agama now writes.
# relatime, seclabel, ssd, and space_cache=v2 show up without being set
# in fstab. ro, the subvolumes, and x-initrd.mount are the real layout.
expect_mount() {
    local path=$1 fstype=$2
    shift 2
    local actual opts token
    actual=$(findmnt -n -o FSTYPE "$path" 2>/dev/null || true)
    opts=$(findmnt -n -o OPTIONS "$path" 2>/dev/null || true)
    if [[ "$actual" != "$fstype" ]]; then
        bad "$path is '${actual:-not mounted}', want $fstype"
        return
    fi
    ok "$path is $fstype"
    for token in "$@"; do
        if has_token "$opts" "$token"; then
            ok "$path $token"
        else
            bad "$path is missing $token (${opts:-no options})"
        fi
    done
}

section "btrfs mount"
# seclabel is added by SELinux. An install with SELinux off does not show it.
common=(relatime space_cache=v2)
if [[ -r /sys/fs/selinux/enforce ]]; then
    common+=(seclabel)
else
    skipped "seclabel (SELinux is off)"
fi
expect_mount / btrfs ro "${common[@]}" compress=zstd:-4 lazytime
root_subvol=$(findmnt -n -o FSROOT / 2>/dev/null || true)
case "$root_subvol" in
    /.snapshots/*/snapshot|/@/.snapshots/*/snapshot)
        ok "/ is the current snapshot ($root_subvol)"
        ;;
    *)
        bad "/ subvolume is '${root_subvol:-unset}', want @/.snapshots/N/snapshot"
        ;;
esac
root_opts=$(findmnt -n -o OPTIONS / 2>/dev/null || true)
if has_token "$root_opts" ssd; then
    ok "/ ssd (the kernel added this)"
else
    root_src=$(findmnt -n -o SOURCE / 2>/dev/null || true)
    root_disk=$(lsblk -no PKNAME "$root_src" 2>/dev/null | awk 'NF { print; exit }')
    if [[ -n "$root_disk" && ! -e "/sys/block/${root_disk}/queue/rotational" ]]; then
        root_disk=$(lsblk -no PKNAME "/dev/${root_disk}" 2>/dev/null | awk 'NF { print; exit }')
    fi
    if [[ -r "/sys/block/${root_disk}/queue/rotational" ]] && [[ $(cat "/sys/block/${root_disk}/queue/rotational") == 0 ]]; then
        bad "/ is on an SSD and ssd is not in the mount options"
    else
        skipped "/ ssd (not an SSD, or the disk could not be identified)"
    fi
fi
expect_mount /etc btrfs rw "${common[@]}"
# Old YaST wrote x-initrd.mount on /var and /root. Agama does not.
# Both subvolumes are still mounted read-write without that flag.
expect_mount /root btrfs rw "${common[@]}"
expect_mount /var btrfs rw "${common[@]}"
for early_path in /root /var; do
    early_opts=$(findmnt -n -o OPTIONS "$early_path" 2>/dev/null || true)
    if has_token "$early_opts" x-initrd.mount; then
        ok "$early_path x-initrd.mount"
    else
        skipped "$early_path x-initrd.mount (Agama does not set this)"
    fi
done
expect_mount /.snapshots btrfs rw "${common[@]}"
expect_mount /home btrfs rw "${common[@]}"
expect_mount /opt btrfs rw "${common[@]}"
expect_mount /srv btrfs rw "${common[@]}"
expect_mount /usr/local btrfs rw "${common[@]}"
if findmnt -n /boot/efi >/dev/null 2>&1; then
    expect_mount /boot/efi vfat rw relatime
elif findmnt -n /efi >/dev/null 2>&1; then
    expect_mount /efi vfat rw relatime
elif findmnt -s -n /boot/efi >/dev/null 2>&1 || findmnt -s -n /efi >/dev/null 2>&1; then
    ok "ESP is in fstab and not mounted right now"
elif esp_used_by_systemd_boot; then
    ok "ESP is used by systemd-boot and is not kept in fstab"
else
    bad "ESP is not mounted and not in fstab"
fi
if findmnt -n /boot/grub2/x86_64-efi >/dev/null 2>&1; then
    expect_mount /boot/writable btrfs rw "${common[@]}"
    expect_mount /boot/grub2/x86_64-efi btrfs rw
    expect_mount /boot/grub2/i386-pc btrfs rw
else
    skipped "grub2 subvolumes (systemd-boot does not mount them)"
    if findmnt -n /boot/writable >/dev/null 2>&1; then
        expect_mount /boot/writable btrfs rw "${common[@]}"
    else
        skipped "/boot/writable (systemd-boot keeps boot files on the ESP)"
    fi
fi

section "boot command line"
cmdline=$(cat /proc/cmdline 2>/dev/null || true)
for token in \
    quiet \
    splash=silent \
    plymouth.ignore-serial-consoles \
    loglevel=3 \
    vt.global_cursor_default=0 \
    nowatchdog \
    systemd.show_status=auto \
    rd.udev.log_level=1 \
    psi=1 \
    security=selinux \
    selinux=1
do
    if has_token "$cmdline" "$token"; then
        ok "$token"
    else
        bad "$token is not in the running command line"
    fi
done
for token in nohz_full= rcu_nocbs= zswap.enabled=0 systemd.hostname=kde-linux amdgpu.dcdebugmask=; do
    case " $cmdline " in
        *" $token"*)
            bad "rejected option is on the command line: $token"
            ;;
        *)
            ok "command line does not set $token"
            ;;
    esac
done
case " $cmdline " in
    *" lsm="*)
        case " $cmdline " in
            *"lsm="*"selinux"*)
                ok "lsm= still includes selinux"
                ;;
            *)
                bad "lsm= on the command line does not include selinux"
                ;;
        esac
        ;;
    *)
        ok "command line does not replace lsm="
        ;;
esac
if [[ -f /etc/selinux/config ]] \
    && grep -q '^SELINUX=enforcing$' /etc/selinux/config \
    && grep -q '^SELINUXTYPE=targeted$' /etc/selinux/config; then
    ok "SELinux config is enforcing targeted"
else
    bad "SELinux config is not enforcing targeted"
fi

section "zram and sysctl"
if [[ -d /sys/block/zram0 ]]; then
    algo=$(awk '{
        for (i = 1; i <= NF; i++) if ($i ~ /^\[/) { gsub(/[\[\]]/, "", $i); print $i; exit }
    }' /sys/block/zram0/comp_algorithm)
    if [[ "$algo" == zstd ]]; then
        ok "zram0 compression is zstd"
    else
        bad "zram0 compression is '${algo:-unset}', want zstd"
    fi
    disksize=$(cat /sys/block/zram0/disksize)
    memtotal=$(awk '/^MemTotal:/ { print $2 * 1024 }' /proc/meminfo)
    # zram-generator "ram" matches MemTotal. Allow a small rounding gap.
    low=$((memtotal - 1048576))
    high=$((memtotal + 1048576))
    if (( disksize >= low && disksize <= high )); then
        ok "zram0 size matches RAM"
    else
        bad "zram0 size is $disksize bytes, RAM is $memtotal bytes"
    fi
else
    bad "zram0 is not present"
fi
sysctl_is vm.swappiness 180
sysctl_is vm.page-cluster 0
sysctl_is vm.watermark_boost_factor 0
sysctl_is vm.watermark_scale_factor 125
sysctl_is kernel.split_lock_mitigate 0
sysctl_is net.ipv4.tcp_congestion_control bbr
sysctl_is net.core.default_qdisc fq
sysctl_is fs.inotify.max_user_instances 8192
sysctl_is fs.inotify.max_user_watches 524288
sysctl_is kernel.panic 30
sysctl_is kernel.printk "3 3 3 3"
sysctl_is kernel.nmi_watchdog 0
sysctl_is net.ipv4.tcp_rfc1337 1
sysctl_is net.ipv4.conf.all.rp_filter 2
sysctl_is net.ipv4.conf.all.send_redirects 0
sysctl_is net.ipv4.conf.all.accept_redirects 0
sysctl_is net.ipv6.conf.all.accept_redirects 0
sysctl_is net.ipv6.conf.all.use_tempaddr 2
sysctl_is net.ipv4.conf.all.log_martians 1
sysctl_is kernel.unprivileged_bpf_disabled 1
sysctl_is kernel.kptr_restrict 2
sysctl_is dev.tty.ldisc_autoload 0
sysctl_is kernel.kexec_load_disabled 1

section "systemd"
start_timeout=$(systemctl show -p DefaultTimeoutStartUSec --value 2>/dev/null || true)
stop_timeout=$(systemctl show -p DefaultTimeoutStopUSec --value 2>/dev/null || true)
case "$start_timeout" in
    16s|16.000000s|16000000) ok "DefaultTimeoutStartSec is 16s" ;;
    *) bad "DefaultTimeoutStartSec is '${start_timeout:-unset}', want 16s" ;;
esac
case "$stop_timeout" in
    8s|8.000000s|8000000) ok "DefaultTimeoutStopSec is 8s" ;;
    *) bad "DefaultTimeoutStopSec is '${stop_timeout:-unset}', want 8s" ;;
esac
if journal_blob=$(systemd-analyze cat-config systemd/journald.conf 2>/dev/null); then
    journal_max=$(printf '%s\n' "$journal_blob" | effective_value SystemMaxUse -)
else
    shopt -s nullglob
    journal_files=(
        /usr/lib/systemd/journald.conf
        /usr/lib/systemd/journald.conf.d/*.conf
        /etc/systemd/journald.conf
        /etc/systemd/journald.conf.d/*.conf
        /run/systemd/journald.conf.d/*.conf
    )
    shopt -u nullglob
    journal_max=$(effective_value SystemMaxUse "${journal_files[@]}")
fi
if [[ "$journal_max" == 500M ]]; then
    ok "journal SystemMaxUse=500M"
else
    bad "journal SystemMaxUse is '${journal_max:-unset}', want 500M"
fi
# kernel-longterm builds with CONFIG_PSI_DEFAULT_DISABLED. oomd's unit
# requires /proc/pressure/memory, which appears only after psi=1.
oomd_state=$(systemctl is-active systemd-oomd.service 2>/dev/null || true)
if [[ "$oomd_state" == active ]]; then
    ok "systemd-oomd.service active"
elif [[ ! -e /proc/pressure/memory ]]; then
    bad "systemd-oomd.service is inactive because /proc/pressure/memory is missing (needs psi=1)"
else
    bad "systemd-oomd.service is '${oomd_state:-missing}', want active"
fi
unit_enabled systemd-oomd.service
pressure_is system.slice 60
pressure_is user.slice 40
# zram-generator's setup unit has no [Install] section. is-enabled
# reports static. The zram0 checks above are what show it is in use.
zram_state=$(systemctl is-enabled systemd-zram-setup@zram0.service 2>/dev/null || true)
case "$zram_state" in
    static|enabled)
        ok "systemd-zram-setup@zram0.service is $zram_state"
        ;;
    *)
        bad "systemd-zram-setup@zram0.service is '${zram_state:-missing}', want static or enabled"
        ;;
esac
unit_enabled dbus-broker.service
# patterns-atropos requires tuned-ppd and conflicts with
# power-profiles-daemon. An older install can still have the other daemon.
# tuned-ppd Requires tuned.service. A /usr wants link starts the unit
# even when is-enabled still says disabled.
for tuned_unit in tuned.service tuned-ppd.service; do
    tuned_state=$(systemctl is-enabled "$tuned_unit" 2>/dev/null || true)
    case "$tuned_state" in
        enabled|static)
            ok "$tuned_unit is $tuned_state"
            ;;
        *)
            if [[ -L /usr/lib/systemd/system/multi-user.target.wants/$tuned_unit || -L /usr/lib/systemd/system/graphical.target.wants/$tuned_unit ]]; then
                ok "$tuned_unit is pulled in by its vendor wants link"
            else
                bad "$tuned_unit is '${tuned_state:-missing}', want enabled"
            fi
            ;;
    esac
done
unit_enabled atropos-powertop.service
unit_disabled transactional-update.timer
# transactional-update reads the vendor file, then every file in
# /usr/etc/transactional-update.conf.d, then /etc/transactional-update.conf.
shopt -s nullglob
reboot_files=(
    /usr/etc/transactional-update.conf
    /usr/etc/transactional-update.conf.d/*
    /etc/transactional-update.conf
)
shopt -u nullglob
reboot_method=$(effective_value REBOOT_METHOD "${reboot_files[@]}")
if [[ "$reboot_method" == notify ]]; then
    ok "REBOOT_METHOD=notify"
else
    bad "REBOOT_METHOD is '${reboot_method:-unset}', want notify"
fi
if grep -q GenuineIntel /proc/cpuinfo 2>/dev/null; then
    unit_active thermald.service
else
    skipped "thermald active (no Intel CPU)"
    unit_enabled thermald.service
fi
unit_enabled atropos-iw-set-regdomain.path
unit_enabled atropos-configure-firefox.path

kill_user=$(busctl get-property org.freedesktop.login1 /org/freedesktop/login1 \
    org.freedesktop.login1.Manager KillUserProcesses 2>/dev/null || true)
if [[ "$kill_user" == "b true" ]]; then
    ok "KillUserProcesses"
else
    bad "KillUserProcesses is '${kill_user:-unset}'"
fi
# systemd 261 exposes EnableWallMessages. Older logind used WallMessages.
wall=$(busctl get-property org.freedesktop.login1 /org/freedesktop/login1 \
    org.freedesktop.login1.Manager EnableWallMessages 2>/dev/null || true)
if [[ -z "$wall" ]]; then
    wall=$(busctl get-property org.freedesktop.login1 /org/freedesktop/login1 \
        org.freedesktop.login1.Manager WallMessages 2>/dev/null || true)
fi
if [[ "$wall" == "b false" ]]; then
    ok "shutdown wall messages off"
else
    bad "wall messages are '${wall:-unset}', want off"
fi

section "desktop drop-ins"
if [[ -d /sys/module/ntsync ]]; then
    ok "ntsync loaded"
else
    bad "ntsync is not loaded"
fi
file_has /usr/lib/udev/rules.d/60-ioschedulers.rules 'scheduler}="bfq"' "BFQ scheduler rule"
file_has /usr/lib/udev/rules.d/60-usb-dirty-pages-udev.rules 'atropos-usb-dirty-pages' "USB writeback cap"
file_has /usr/lib/modprobe.d/20-audio-pm.conf 'power_save' "HDA power save"
file_has /usr/lib/modprobe.d/98-atropos-watchdog.conf 'blacklist iTCO_wdt' "watchdog module blacklist"
file_has /etc/security/limits.d/00-atropos.conf 'memlock' "PAM memlock"
# plasma6-branding-openSUSE owns /etc/xdg/baloofilerc. The exclude line
# lives in /usr/etc/xdg/baloofilerc, which KConfig merges in. An older
# package wrote the same path as the branding file and lost.
baloo_found=0
for baloo_file in /usr/etc/xdg/baloofilerc /etc/xdg/baloofilerc; do
    if [[ -f "$baloo_file" ]] && grep -F -q -- '$HOME/.snapshots/' "$baloo_file"; then
        ok "Baloo skips ~/.snapshots ($baloo_file)"
        baloo_found=1
        break
    fi
done
if ((baloo_found == 0)); then
    if [[ -f /etc/xdg/baloofilerc.rpmnew ]] && grep -F -q -- '$HOME/.snapshots/' /etc/xdg/baloofilerc.rpmnew; then
        bad "Baloo skips ~/.snapshots (only in /etc/xdg/baloofilerc.rpmnew)"
    else
        bad "Baloo skips ~/.snapshots (no exclude line; branding owns /etc/xdg/baloofilerc)"
    fi
fi
file_has /etc/xdg/QtProject/qtlogging.ini '*.debug=false' "quieter Qt logging"
file_has /usr/lib/environment.d/50-atropos-workarounds.conf 'QT_DISABLE_HW_TEXTURES_CONVERSION=1' "AMD texture workaround"
file_has /usr/lib/environment.d/60-atropos-ssh-askpass.conf 'SSH_ASKPASS=/usr/bin/ksshaskpass' "ksshaskpass"
file_has /usr/lib/environment.d/60-atropos-ssh-auth-sock.conf 'ssh-agent.socket' "ssh-agent socket"
file_has /etc/geoclue/conf.d/00-atropos.conf 'api.beacondb.net' "Geoclue BeaconDB"
file_has /usr/share/atropos/applications/org.qt.qdbusviewer6.desktop 'NoDisplay=true' "qdbusviewer hidden"
file_has /usr/lib/environment.d/99-atropos-applications.conf '/usr/share/atropos' "application override directory"
if [[ $(systemctl is-enabled plasmalogin.service 2>/dev/null || true) == enabled ]]; then
    ok "plasmalogin enabled"
else
    bad "plasmalogin is not enabled"
fi
sddm_state=$(systemctl is-enabled sddm.service 2>/dev/null || true)
case "$sddm_state" in
    disabled|masked|not-found|"")
        ok "sddm not enabled"
        ;;
    *)
        bad "sddm is '$sddm_state'"
        ;;
esac

section "nvidia"
if [[ -d /sys/module/nvidia ]] || rpm -q atropos-nvidia >/dev/null 2>&1; then
    file_has /usr/lib/modprobe.d/00-nvidia.conf 'NVreg_InitializeSystemMemoryAllocations=0' "NVIDIA memory allocation"
    file_has /usr/lib/udev/rules.d/80-nvidia-power-management.rules 'nvidia' "NVIDIA runtime PM rule"
else
    skipped "NVIDIA package and module are absent"
fi

printf '\n%d ok, %d missing, %d skipped\n' "$pass" "$miss" "$skip"
if (( miss > 0 )); then
    exit 1
fi
exit 0
