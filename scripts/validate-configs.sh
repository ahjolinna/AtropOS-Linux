#!/bin/bash
# Check the AtropOS source tree before those files go into a snapshot.
# Run it from anywhere. It does not install or change the system.
#
# The installed-system check is /usr/sbin/atropos-check. The personal
# checklist remains scripts/check-kde-linux.sh. This one looks at the
# files in the git tree: the Agama product, systemd units, KDE and
# systemd drop-ins, and the PackageKit parser test.

set -u

root=$(cd "$(dirname "$0")/.." && pwd)
pass=0
miss=0
skip=0

section() { printf '\n%s\n' "$1"; }
ok() { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  miss  %s\n' "$1"; miss=$((miss + 1)); }
skipped() { printf '  skip  %s\n' "$1"; skip=$((skip + 1)); }

section "Agama product"
if ruby -ryaml -e '
  data = YAML.load_file(ARGV[0])
  template = data.dig("storage", "volume_templates", 0)
  abort "no volume template" if template.nil?
  options = template["mount_options"]
  abort "mount_options is #{options.inspect}" unless options == ["compress=zstd:-4", "lazytime"]
  btrfs = template["btrfs"] || {}
  abort "read_only is not true" unless btrfs["read_only"] == true
  abort "default subvolume is #{btrfs["default_subvolume"].inspect}" unless btrfs["default_subvolume"] == "@"
  patterns = data.dig("software", "user_patterns") || []
  debug = patterns.find { |item| item.is_a?(Hash) && item["name"] == "atropos_debug" }
  abort "atropos_debug is not an unchecked user pattern" unless debug && debug["selected"] == false
' "$root/products/atropos.yaml"
then
    ok "atropos.yaml parses, and compress=zstd:-4 plus lazytime are the only fstab options"
    ok "atropos_debug is an unchecked installer pattern"
else
    bad "atropos.yaml failed the product check"
fi
meta=$root/release/com.github.atropos.metainfo.xml
if [[ -f $meta ]] && grep -q '<id>com.github.atropos</id>' "$meta" && grep -q 'version="20261020"' "$meta"; then
    ok "operating-system metainfo id is com.github.atropos"
else
    bad "operating-system metainfo id is not com.github.atropos at version 20261020"
fi
if command -v appstreamcli >/dev/null; then
    if appstreamcli validate --no-net "$meta" > /tmp/atropos-metainfo.txt 2>&1; then
        ok "com.github.atropos.metainfo.xml validates"
    else
        bad "com.github.atropos.metainfo.xml failed appstreamcli validate"
    fi
else
    skipped "appstreamcli is not installed"
fi

section "JSON"
json_ok=0
while IFS= read -r json; do
    if python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$json"; then
        ok "${json#"$root"/}"
    else
        bad "${json#"$root"/} is not valid JSON"
    fi
    json_ok=1
done < <(find "$root" -name '*.json' -not -path '*/previews/*')
if ((json_ok == 0)); then
    skipped "no JSON files"
fi

section "systemd units"
if ! command -v systemd-analyze >/dev/null; then
    skipped "systemd-analyze is not installed"
else
    while IFS= read -r unit; do
        base=$(basename "$unit")
        report=$(systemd-analyze verify "$unit" 2>&1 || true)
        ours=$(printf '%s\n' "$report" | grep -F "$base:" || true)
        syntax=$(printf '%s\n' "$ours" | grep -E 'Failed to parse|Assignment outside|Unknown section|Invalid |Bad ' || true)
        if [[ -n "$syntax" ]]; then
            bad "$base $(printf '%s' "$syntax" | tr '\n' ' ')"
        else
            ok "$base parses"
        fi
        if printf '%s\n' "$ours" | grep -q 'not executable'; then
            skipped "$base commands are not installed on this machine"
        fi
    done < <(find "$root" -type f \( -name '*.service' -o -name '*.path' -o -name '*.timer' \))
fi

section "udev rules"
if ! command -v udevadm >/dev/null || ! udevadm verify --help >/dev/null 2>&1; then
    skipped "udevadm verify is not available"
else
    mapfile -t rules < <(find "$root" -type f -name '*.rules')
    if ((${#rules[@]} == 0)); then
        skipped "no udev rules"
    elif udevadm verify "${rules[@]}" >/tmp/atropos-udev-verify.txt 2>&1; then
        ok "${#rules[@]} udev rules"
    else
        bad "udev rules failed: $(tr '\n' ' ' < /tmp/atropos-udev-verify.txt)"
    fi
fi

section "desktop files"
if ! command -v desktop-file-validate >/dev/null; then
    skipped "desktop-file-validate is not installed"
else
    while IFS= read -r desktop; do
        if desktop-file-validate "$desktop"; then
            ok "${desktop#"$root"/}"
        else
            bad "${desktop#"$root"/} failed desktop-file-validate"
        fi
    done < <(find "$root" -type f -name '*.desktop')
fi

# Key files. Comments and blank lines are allowed. A key line needs '='.
check_ini() {
    local file=$1 line no=0
    local rel=${file#"$root"/}
    if [[ ! -f "$file" ]]; then
        bad "$rel is missing"
        return
    fi
    while IFS= read -r line || [[ -n "$line" ]]; do
        no=$((no + 1))
        case "$line" in
            ''|'#'*|';'*|'['*']') continue ;;
            *=*) continue ;;
            *)
                bad "$rel:$no is not a section or a key"
                return
                ;;
        esac
    done < "$file"
    ok "$rel"
}

section "KDE and systemd drop-ins"
while IFS= read -r ini; do
    check_ini "$ini"
done <<EOF
$root/performance/baloofilerc
$root/performance/kdeglobals
$root/performance/plasmarc
$root/performance/kwinrc
$root/performance/kcminputrc
$root/performance/ksplashrc
$root/performance/gtk-3.0-settings.ini
$root/performance/gtk-4.0-settings.ini
$root/performance/qtlogging.ini
$root/performance/00-atropos-geoclue.conf
$root/performance/00-atropos-journald.conf
$root/performance/00-atropos-oomd.conf
$root/performance/00-atropos-system.conf
$root/performance/00-atropos-user.conf
$root/performance/00-atropos-system-oom.conf
$root/performance/00-atropos-user-oom.conf
$root/performance/00-atropos-zram.conf
$root/performance/00-atropos-killuserprocesses.conf
$root/performance/00-atropos-nowall.conf
$root/packagekit/discoverrc
$root/packagekit/atropos-packagekit.conf
$root/packagekit/atropos-offline-update.conf
$root/plymouth/plymouthd.conf
EOF

if grep -q '^LookAndFeelPackage=org.atropos.desktop$' "$root/performance/kdeglobals" \
    && grep -q '^DefaultDarkLookAndFeel=org.atropos.desktop$' "$root/performance/kdeglobals" \
    && grep -q '^DefaultLightLookAndFeel=org.kde.breeze.desktop$' "$root/performance/kdeglobals" \
    && grep -q '^ColorScheme=AetherDark$' "$root/performance/kdeglobals" \
    && grep -q '^Theme=kora$' "$root/performance/kdeglobals" \
    && grep -q '^font=Source Sans 3,' "$root/performance/kdeglobals" \
    && grep -q '^fixed=Source Code Pro,' "$root/performance/kdeglobals"; then
    ok "skel kdeglobals selects AtropOS as the dark theme, Kora, and Source fonts"
else
    bad "skel kdeglobals is missing the AtropOS dark theme, Kora, or Source fonts"
fi
if grep -q '^Theme=kora$' "$root/aether/plasma6-theme-aether-dark-0.0.3a/look-and-feel/org.atropos.aether.desktop/contents/defaults"; then
    ok "global theme defaults select Kora"
else
    bad "global theme defaults do not select Kora"
fi
splash=$root/splash/plasma6-lookandfeel-atropos-0.5
if grep -q '^Theme=org.atropos.desktop$' "$root/performance/ksplashrc" \
    && grep -q '^Theme=org.atropos.desktop$' "$root/aether/plasma6-theme-aether-dark-0.0.3a/look-and-feel/org.atropos.aether.desktop/contents/defaults" \
    && grep -q '^LayoutName=coverswitch$' "$root/performance/kwinrc" \
    && grep -q '^LayoutName=coverswitch$' "$root/aether/plasma6-theme-aether-dark-0.0.3a/look-and-feel/org.atropos.aether.desktop/contents/defaults" \
    && grep -q '^Requires:[[:space:]]*plasma6-lookandfeel-atropos$' "$root/patterns/patterns-atropos.spec" \
    && grep -q '^Requires:[[:space:]]*plasma6-lookandfeel-atropos$' "$root/aether/plasma6-theme-aether-dark.spec" \
    && grep -q '^Requires:[[:space:]]*kdeplasma6-addons$' "$root/patterns/patterns-atropos.spec" \
    && python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["KPlugin"]["Id"]=="org.atropos.desktop"; assert d["KPlugin"]["Name"]=="AtropOS"; assert d["KPlugin"]["Version"]=="0.5"' \
        "$splash/metadata.json" \
    && [[ -s $splash/contents/splash/images/background.png ]] \
    && grep -q '^Image=atropos-02$' "$splash/contents/defaults" \
    && grep -q '^ColorScheme=AetherDark$' "$splash/contents/defaults" \
    && [[ -s $splash/contents/previews/preview.png ]] \
    && grep -q 'file:///usr/share/wallpapers/atropos-02/contents/images/1376x768.jpg' \
        "$splash/contents/splash/Splash.qml" \
    && grep -q '^Requires:[[:space:]]*atropos-wallpapers$' \
        "$root/splash/plasma6-lookandfeel-atropos.spec"; then
    ok "splash is AtropOS and the task switcher is Cover Switch"
else
    bad "splash theme, splash background, Cover Switch, or AtropOS package metadata is missing"
fi
lnf_defaults=$root/aether/plasma6-theme-aether-dark-0.0.3a/look-and-feel/org.atropos.aether.desktop/contents/defaults
wp=$root/wallpapers/atropos-wallpapers-0.1.0
if grep -q '^Image=atropos-02$' "$lnf_defaults" \
    && [[ -f $wp/atropos-02/contents/images/1376x768.jpg ]] \
    && [[ -f $wp/wallpaper-animated.webm ]] \
    && grep -q 'file:///usr/share/atropos/wallpaper-animated.webm' "$wp/org.atropos.video/contents/ui/main.qml" \
    && grep -q '^Requires:[[:space:]]*atropos-wallpapers$' "$root/patterns/patterns-atropos.spec" \
    && grep -q '^Requires:[[:space:]]*atropos-wallpapers$' "$root/aether/plasma6-theme-aether-dark.spec" \
    && grep -q '^Requires:[[:space:]]*qt6-multimedia$' "$root/wallpapers/atropos-wallpapers.spec" \
    && grep -q '^Requires:[[:space:]]*qt6-multimedia-imports$' "$root/wallpapers/atropos-wallpapers.spec"; then
    ok "global theme wallpaper is atropos-02 and the video plugin is packaged"
else
    bad "global theme wallpaper, video plugin, or wallpaper dependency is missing"
fi
if python3 -c '
import json, sys
root = sys.argv[1]
for n in range(1, 8):
    pkg = f"atropos-{n:02d}"
    data = json.load(open(f"{root}/{pkg}/metadata.json"))
    assert data["KPlugin"]["Id"] == pkg, pkg
plugin = json.load(open(f"{root}/org.atropos.video/metadata.json"))
assert plugin["KPackageStructure"] == "Plasma/Wallpaper"
assert plugin["KPlugin"]["Id"] == "org.atropos.video"
' "$wp"; then
    ok "wallpaper package ids are atropos-01 through atropos-07 and org.atropos.video"
else
    bad "wallpaper package metadata ids do not match"
fi
aether_tar=$root/aether/plasma6-theme-aether-dark-0.0.3a.tar.xz
wp_tar=$root/wallpapers/atropos-wallpapers-0.1.0.tar.xz
splash_tar=$root/splash/plasma6-lookandfeel-atropos-0.5.tar.xz
aether_defaults_in_tar=
if [[ -f $aether_tar ]]; then
    aether_defaults_in_tar=$(tar -xJOf "$aether_tar" \
        plasma6-theme-aether-dark-0.0.3a/look-and-feel/org.atropos.aether.desktop/contents/defaults)
fi
if printf '%s\n' "$aether_defaults_in_tar" | grep -q '^Image=atropos-02$' \
    && printf '%s\n' "$aether_defaults_in_tar" | grep -q '^Theme=org.atropos.desktop$' \
    && printf '%s\n' "$aether_defaults_in_tar" | grep -q '^LayoutName=coverswitch$' \
    && [[ -f $wp_tar ]] && tar -tJf "$wp_tar" | grep -q 'atropos-02/contents/images/1376x768.jpg' \
    && tar -tJf "$wp_tar" | grep -q 'atropos-05/contents/images/1584x672.jpg' \
    && tar -tJf "$wp_tar" | grep -q 'atropos-06/contents/images/1376x768.jpg' \
    && tar -tJf "$wp_tar" | grep -q 'atropos-07/contents/images/1376x768.jpg' \
    && ! tar -tJf "$wp_tar" | grep -q 'atropos-08/' \
    && tar -xJOf "$wp_tar" atropos-wallpapers-0.1.0/org.atropos.video/contents/ui/main.qml \
        | grep -q 'file:///usr/share/atropos/wallpaper-animated.webm' \
    && [[ -f $splash_tar ]] && tar -xJOf "$splash_tar" \
        plasma6-lookandfeel-atropos-0.5/metadata.json \
        | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["KPlugin"]["Id"]=="org.atropos.desktop"' \
    && tar -xJOf "$splash_tar" \
        plasma6-lookandfeel-atropos-0.5/contents/splash/images/background.png \
        | cmp -s - "$splash/contents/splash/images/background.png" \
    && tar -xJOf "$splash_tar" \
        plasma6-lookandfeel-atropos-0.5/contents/defaults \
        | grep -q '^Image=atropos-02$' \
    && tar -xJOf "$splash_tar" \
        plasma6-lookandfeel-atropos-0.5/contents/splash/Splash.qml \
        | grep -q 'file:///usr/share/wallpapers/atropos-02/contents/images/1376x768.jpg' \
    && tar -xJOf "$aether_tar" \
        plasma6-theme-aether-dark-0.0.3a/widgets/lineedit.svg \
        | grep -q '#1b1e20' \
    && ! tar -xJOf "$aether_tar" \
        plasma6-theme-aether-dark-0.0.3a/widgets/lineedit.svg \
        | grep -q '#f5f4f4' \
    && tar -xJOf "$aether_tar" \
        plasma6-theme-aether-dark-0.0.3a/widgets/tasks.svg \
        | grep -q 'atropos-task-hints'; then
    ok "aether, wallpaper, and splash tarballs match the unpacked trees"
else
    bad "aether, wallpaper, or splash tarball is missing or stale"
fi
if grep -q '^name=aetherdark$' "$root/performance/plasmarc"; then
    ok "skel plasmarc selects aetherdark"
else
    bad "skel plasmarc does not select aetherdark"
fi
if python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["KPlugin"]["Id"]=="aetherdark"' \
    "$root/aether/plasma6-theme-aether-dark-0.0.3a/metadata.json"; then
    ok "aether metadata.json id is aetherdark"
else
    bad "aether metadata.json is missing or has the wrong id"
fi
if grep -q '^Name=AetherDark$' "$root/aether/plasma6-theme-aether-dark-0.0.3a/colors" \
    && grep -q '^Requires:[[:space:]]*plasma6-theme-aether-dark$' "$root/patterns/patterns-atropos.spec" \
    && grep -q '^Requires:[[:space:]]*kora-icon-theme$' "$root/patterns/patterns-atropos.spec" \
    && grep -q '^Requires:[[:space:]]*papirus-icon-theme$' "$root/patterns/patterns-atropos.spec"; then
    ok "pattern requires Aether Dark, Kora, and Papirus"
else
    bad "pattern does not require Aether Dark, Kora, and Papirus, or the color scheme id changed"
fi
apps='krfb krdc khelpcenter distrobox podman ark'
missing=
for name in $apps; do
    if ! grep -q "^Requires:[[:space:]]*${name}$" "$root/patterns/patterns-atropos.spec"; then
        missing="$missing $name"
    fi
done
flatpak=$root/performance/flatpak-preinstall.txt
for id in org.kde.koko io.github.mpc_qt.mpc-qt org.kde.kcalc org.kde.kolourpaint io.github.slgobinath.SafeEyes; do
    if ! grep -qx "$id" "$flatpak"; then
        missing="$missing $id"
    fi
done
if grep -qx 'org.kde.gwenview' "$flatpak" || grep -qx 'org.kde.haruna' "$flatpak" \
    || grep -qx 'org.kde.ark' "$flatpak"; then
    missing="$missing gwenview-or-haruna-or-ark"
fi
if [[ -z $missing ]]; then
    ok "pattern requires Ark and the desktop tools, and the Flatpak list matches"
else
    bad "desktop packages or Flatpak ids are wrong:${missing}"
fi
bootloader=$root/performance/atropos-bootloader
boot_body=$(grep -v '^[[:space:]]*#' "$bootloader" || true)
if [[ -x $bootloader ]] \
    && bash -n "$bootloader" \
    && printf '%s\n' "$boot_body" | grep -q 'set-timeout 7' \
    && printf '%s\n' "$boot_body" | grep -q 'set-timeout 0' \
    && printf '%s\n' "$boot_body" | grep -q 'bootctl' \
    && ! printf '%s\n' "$boot_body" | grep -q 'sdbootutil' \
    && ! printf '%s\n' "$boot_body" | grep -q 'menu-disabled' \
    && grep -q '^ExecStart=/usr/libexec/atropos-bootloader show$' \
        "$root/performance/atropos-bootloader-show.service" \
    && grep -q '^ExecStop=/usr/libexec/atropos-bootloader hide$' \
        "$root/performance/atropos-bootloader-hide.service" \
    && grep -q '^enable atropos-bootloader-show.service$' \
        "$root/performance/80-atropos.preset" \
    && grep -q '^enable atropos-bootloader-hide.service$' \
        "$root/performance/80-atropos.preset" \
    && grep -q '%{_libexecdir}/atropos-bootloader$' \
        "$root/performance/atropos-performance.spec" \
    && grep -q '%{_unitdir}/atropos-bootloader-show.service$' \
        "$root/performance/atropos-performance.spec" \
    && grep -q '%{_unitdir}/atropos-bootloader-hide.service$' \
        "$root/performance/atropos-performance.spec"; then
    ok "systemd-boot menu uses bootctl timeout 7 and 0"
else
    bad "bootloader script, units, or package file list is wrong"
fi
tools='kvantum-qt6 kvantum-qt5 kvantum-manager kvantum-themes sensors libdisplay-info-tools clinfo inxi conky android-udev-rules android-tools ufw hunspell kup-backup unrar lrzip orca htop git yubikey-manager flatpak-builder mcfly plocate'
missing=
for name in $tools; do
    if ! grep -q "^Requires:[[:space:]]*${name}$" "$root/patterns/patterns-atropos.spec"; then
        missing="$missing $name"
    fi
done
if [[ -z $missing ]] && grep -q 'mcfly init bash' "$root/patterns/bash.bashrc.local" \
    && grep -q '^enable plocate-updatedb.timer$' "$root/patterns/82-atropos-tools.preset"; then
    ok "pattern requires Kvantum and the extra tools"
else
    bad "pattern is missing tools:${missing:- none}, or the mcfly hook or plocate timer is absent"
fi
hw='fprintd fprintd-pam pam_u2f libfido2-1 libfido2-udev pcsc-ccid pcsc-acsccid'
missing=
for name in $hw; do
    if ! grep -q "^Requires:[[:space:]]*${name}$" "$root/patterns/patterns-atropos.spec"; then
        missing="$missing $name"
    fi
done
if [[ -z $missing ]]; then
    ok "pattern requires fingerprint, FIDO, and smartcard support"
else
    bad "pattern is missing hardware support:${missing}"
fi

section "boot command line"
cmdline_file=$root/performance/00-atropos-cmdline.conf
cmdline_line=$(grep -E '^[^#]' "$cmdline_file" | tr '\n' ' ')
for token in quiet splash=silent plymouth.ignore-serial-consoles loglevel=3 vt.global_cursor_default=0 nowatchdog systemd.show_status=auto rd.udev.log_level=1 psi=1 security=selinux selinux=1; do
    case " $cmdline_line " in
        *" $token "*) ok "cmdline has $token" ;;
        *) bad "cmdline is missing $token" ;;
    esac
done
for token in nohz_full= rcu_nocbs= zswap.enabled=0 systemd.hostname= amdgpu.dcdebugmask= lsm= selinux=0 enforcing=0; do
    case " $cmdline_line " in
        *" $token"*) bad "cmdline contains rejected $token" ;;
        *) ok "cmdline does not set $token" ;;
    esac
done
if grep -q '^Requires:[[:space:]]*selinux-autorelabel$' "$root/patterns/patterns-atropos.spec"; then
    ok "pattern requires selinux-autorelabel"
else
    bad "pattern does not require selinux-autorelabel"
fi
if grep -q '^Requires:[[:space:]]*selinux-policy-targeted-gaming$' "$root/patterns/patterns-atropos.spec"; then
    ok "pattern requires selinux-policy-targeted-gaming"
else
    bad "pattern does not require selinux-policy-targeted-gaming"
fi

section "presets"
while IFS= read -r preset; do
    rel=${preset#"$root"/}
    bad_line=0
    while IFS= read -r line || [[ -n "$line" ]]; do
        case "$line" in
            ''|'#'*) continue ;;
            enable\ *|disable\ *) continue ;;
            *)
                bad "$rel has '$line'"
                bad_line=1
                break
                ;;
        esac
    done < "$preset"
    if ((bad_line == 0)); then
        ok "$rel"
    fi
done < <(find "$root" -type f -name '*.preset')

section "shell scripts"
while IFS= read -r script; do
    if bash -n "$script"; then
        ok "${script#"$root"/}"
    else
        bad "${script#"$root"/} failed bash -n"
    fi
done < <(find "$root" -type f -name '*.sh')
if [[ -f "$root/check/atropos-check" ]]; then
    if bash -n "$root/check/atropos-check"; then
        ok "check/atropos-check"
    else
        bad "check/atropos-check failed bash -n"
    fi
fi

section "open issues"
if grep -q 'default.target.wants/atychiad.service' "$root/atychia/atychia.spec" \
    && grep -q 'ln -s ../atychiad.service' "$root/atychia/atychia.spec" \
    && grep -q '^Requires:[[:space:]]*kf6-kirigami$' "$root/atychia/atychia.spec" \
    && grep -q '^Requires:[[:space:]]*kf6-kirigami-imports$' "$root/atychia/atychia.spec" \
    && grep -q '^Requires:[[:space:]]*kirigami-addons6$' "$root/atychia/atychia.spec" \
    && ! grep -q 'qt6qml(org.kde.kirigami)' "$root/atychia/atychia.spec"; then
    ok "atychiad.service is enabled by unit name"
else
    bad "atychia spec does not enable atychiad.service by unit name"
fi
if grep -q 'Disk passphrase' "$root/plymouth/theme/atropos.script" \
    && grep -q 'line = "_"' "$root/plymouth/theme/atropos.script"; then
    ok "plymouth draws a passphrase when the prompt is empty"
else
    bad "plymouth script hides an empty passphrase"
fi
if grep -q 'Image=file:///usr/share/wallpapers/atropos-02' "$root/performance/plasmalogin.conf" \
    && grep -q '<bootloader-theme>atropos</bootloader-theme>' "$root/installer/agama-installer-atropos.kiwi" \
    && grep -q 'file = "logo.png"' "$root/grub/theme.txt" \
    && [[ -s $root/grub/logo.png ]] \
    && ! grep -q 'text = "AtropOS"' "$root/grub/theme.txt" \
    && grep -q 'desktop-color: "#0D202F"' "$root/grub/theme.txt" \
    && ! grep -q 'desktop-image:' "$root/grub/theme.txt" \
    && ! grep -q 'background.png' "$root/grub/grub2-branding-atropos.spec" \
    && grep -q '<package name="grub2-branding-atropos"' "$root/installer/agama-installer-atropos.kiwi" \
    && ! grep -q '<package name="grub2-branding-openSUSE"' "$root/installer/agama-installer-atropos.kiwi"; then
    ok "plasmalogin and the ISO grub theme use AtropOS"
else
    bad "plasmalogin wallpaper or the ISO grub theme is not AtropOS"
fi
cdroot=$root/installer/config-cdroot.tar.xz
if tar -xOJf "$cdroot" ./fix_bootconfig.x86_64 | grep -q 'boot/grub2/themes/atropos' \
    && tar -xOJf "$cdroot" ./fix_bootconfig.aarch64 | grep -q 'boot/grub2/themes/atropos' \
    && tar -xOJf "$cdroot" ./fix_bootconfig.x86_64 | grep -q 'live.net_config_tui=1' \
    && tar -xOJf "$cdroot" ./fix_bootconfig.aarch64 | grep -q 'live.net_config_tui=1'; then
    ok "ISO boot script selects the atropos theme and asks Online for a network"
else
    bad "ISO boot script still forces the openSUSE theme or skips the Online network dialog"
fi
if grep -q '^enable tuned.service$' "$root/performance/80-atropos.preset" \
    && grep -q 'multi-user.target.wants/tuned.service' "$root/performance/atropos-performance.spec" \
    && grep -q 'graphical.target.wants/tuned-ppd.service' "$root/performance/atropos-performance.spec" \
    && grep -q '^BuildRequires:[[:space:]]*tuned$' "$root/performance/atropos-performance.spec" \
    && grep -q '^BuildRequires:[[:space:]]*tuned-ppd$' "$root/performance/atropos-performance.spec" \
    && grep -q '^Requires:[[:space:]]*tuned$' "$root/performance/atropos-performance.spec" \
    && grep -q '^Requires:[[:space:]]*tuned-ppd$' "$root/performance/atropos-performance.spec"; then
    ok "tuned.service and tuned-ppd.service are both enabled"
else
    bad "tuned.service is not enabled with tuned-ppd"
fi
if grep -q 'LayoutName coverswitch' "$root/performance/atropos-cover-switch" \
    && grep -q 'Applications are still installing' "$root/performance/01-flatpaks.qml" \
    && grep -q 'readonly property string phase' "$root/performance/01-flatpaks.qml"; then
    ok "Cover Switch oneshot and the Flatpak welcome page are present"
else
    bad "Cover Switch oneshot or the Flatpak welcome page is missing"
fi
if grep -q '^ExecStart=/usr/sbin/powertop --auto-tune$' "$root/performance/atropos-powertop.service" \
    && grep -q '^Z /var/log/tuned 0755 root root -$' "$root/performance/atropos-tuned-log.conf" \
    && grep -q 'ENV{DM_NAME}=="cr_root"' "$root/performance/90-atropos-hide-system-disk.rules" \
    && grep -q 'UDISKS_IGNORE' "$root/performance/90-atropos-hide-system-disk.rules" \
    && bash -n "$root/performance/atropos-hide-system-disk" \
    && bash -n "$root/performance/atropos-formcard-overlay" \
    && grep -q 'Unknown/Default' "$root/performance/atropos-AboutPage.qml" \
    && ! grep -q '^prefer ' "$root/performance/atropos-formcard.qmldir"; then
    ok "powertop path, tuned log label, hidden system disk, and Discover RPM label are packaged"
else
    bad "powertop path, tuned log label, hidden system disk, or Discover RPM label is missing"
fi
if grep -q 'no output' "$root/packagekit/pk-backend-atropos.c" \
    && grep -q 'ATROPOS_BUNDLE_NAME' "$root/packagekit/pk-backend-atropos.c" \
    && grep -q 'auto-agree-with-licenses' "$root/packagekit/pk-backend-atropos.c" \
    && ! grep -q 'auto-agree-with-product-licenses' "$root/packagekit/pk-backend-atropos.c"; then
    ok "PackageKit errors are not blank and updates are one System updates row"
else
    bad "PackageKit can still report an empty transaction error or list every RPM"
fi
if grep -q '/usr/libexec/atychia-as-root' "$root/atychia/src/atychiad/main.cpp" \
    && ! grep -q 'u"pkexec"' "$root/atychia/src/atychiad/main.cpp" \
    && bash -n "$root/atychia/atychia-as-root" \
    && grep -q 'NOPASSWD: /usr/libexec/atychia-as-root' "$root/atychia/atropos-atychia.sudoers" \
    && grep -q 'SecureAttentionKey' "$root/atychia/z-atropos-atychia.conf"; then
    ok "atychia starts through sudo and the greeter does not take the chord"
else
    bad "atychia still depends on pkexec or does not hide the chord from the greeter"
fi
if awk '
    /^\[Colors:View\]/ { view=1; next }
    /^\[/ { view=0 }
    view && $0 == "BackgroundNormal=27,30,32" { dark=1 }
    END { exit !dark }
' "$root/aether/plasma6-theme-aether-dark-0.0.3a/colors" \
    && grep -q '^DecorationFocus=199,8,11$' "$root/aether/plasma6-theme-aether-dark-0.0.3a/colors" \
    && grep -q '^Name=AetherDark$' "$root/aether/plasma6-theme-aether-dark-0.0.3a/colors" \
    && ! grep -q '0,164,137' "$root/aether/plasma6-theme-aether-dark-0.0.3a/colors" \
    && ! grep -q '61,174,233' "$root/aether/plasma6-theme-aether-dark-0.0.3a/colors"; then
    ok "AetherDark is openSUSE dark with rose accent 199,8,11"
else
    bad "AetherDark is not the openSUSE dark scheme with rose accent"
fi
if [[ -f $root/patterns/bash.bashrc.local ]] \
    && grep -q 'HISTFILE' "$root/patterns/bash.bashrc.local" \
    && bash -n "$root/patterns/bash.bashrc.local" \
    && bash -n "$root/performance/atropos-cover-switch" \
    && bash -n "$root/check/atropos-check"; then
    ok "mcfly hook and shell helpers parse"
else
    bad "mcfly hook or a shell helper failed to parse"
fi

section "PackageKit parser"
pk_src=$root/packagekit/pk-backend-atropos.c
if ! pkg-config --exists glib-2.0 packagekit-glib2; then
    skipped "PackageKit headers are not installed (ATROPOS_PK_TEST)"
else
    pk_bin=$(mktemp /tmp/atropos-pk-test.XXXXXX)
    if gcc -DATROPOS_PK_TEST -Wall -Wextra \
        $(pkg-config --cflags glib-2.0 packagekit-glib2) \
        -o "$pk_bin" "$pk_src" \
        $(pkg-config --libs glib-2.0 packagekit-glib2) >/tmp/atropos-pk-build.txt 2>&1 \
        && "$pk_bin"
    then
        ok "ATROPOS_PK_TEST"
    else
        bad "ATROPOS_PK_TEST failed ($(tr '\n' ' ' < /tmp/atropos-pk-build.txt))"
    fi
    rm -f "$pk_bin"
fi

printf '\n%d ok, %d missing, %d skipped\n' "$pass" "$miss" "$skip"
if ((miss > 0)); then
    exit 1
fi
exit 0
