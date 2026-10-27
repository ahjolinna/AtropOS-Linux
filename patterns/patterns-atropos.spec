#
# spec file for package patterns-atropos
#
# Copyright (c) 2025 SUSE LLC
# Copyright (c) 2025 Shawn W Dunn <sfalken@kalpadesktop.org>
#
# Based on patterns-kalpa 6.1.1. AtropOS uses kernel-longterm, systemd-boot,
# Slowroll, and one copy of each requirement.

# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.

# Please submit bugfixes or comments via https://bugs.opensuse.org/
#


%bcond_with betatest

Name:           patterns-atropos
Version:        0.4.0
Release:        20
Summary:        Patterns for AtropOS
License:        MIT
Group:          Metapackages
URL:            https://github.com/AtropOSLinux/
Source0:        %name.rpmlintrc
Source1:        79-atropos-display.preset
Source2:        99-atropos-applications.conf
Source3:        atropos-applications.sh
Source4:        atropos-applications.csh
Source5:        org.qt.qdbusviewer6.desktop
Source6:        atropos-oss-debug.repo
Source7:        bash.bashrc.local
Source8:        82-atropos-tools.preset
ExclusiveArch:  x86_64 aarch64

%description
This is an internal package that is used to create the patterns as part
of the installation source setup. Installation of this package does
not make sense.

%package base
Summary:        AtropOS Desktop
Group:          Metapackages
Provides:       pattern() = atropos_base
Provides:       pattern-category() = AtropOS
Provides:       pattern-icon() = pattern-kubic
Provides:       pattern-order() = 9200
Provides:       pattern-visible()
%if %{with betatest}
# need to require it as recommends are off
Requires:       pattern() = update_test
%endif

### Base System Requirements
Requires:       aaa_base
Requires:       audit
Requires:       bash
Requires:       bash-completion
Requires:       branding
Requires:       build-key
Requires:       busybox
Requires:       ca-certificates
Requires:       ca-certificates-mozilla
Requires:       chrony
Requires:       coreutils
Requires:       coreutils-systemd
# curl indirectly needed by ignition via dracut's url-lib
Requires:       curl
Requires:       distribution-release
Requires:       filesystem
Requires:       glibc
Requires:       glibc-locale
Requires:       glibc-locale-base
Requires:       gzip
Requires:       health-checker
Requires:       health-checker-plugins-MicroOS
Requires:       hostname
Requires:       hwinfo
%ifnarch s390x
Requires:       irqbalance
%endif
Requires:       kernel-longterm
# microos_hardware is not installed. Keep CPU microcode in the base pattern.
%ifarch x86_64
Requires:       ucode-amd
Requires:       ucode-intel
%endif
%ifnarch %{arm}
Requires:       kdump
%endif
Conflicts:      krb5-mini
Requires:       less
Requires:       lastlog2
Requires:       libnss_usrfiles2
Requires:       microos-tools
Requires:       openSUSE-build-key
Requires:       pam
Requires:       pam-config
Requires:       polkit-default-privs
Requires:       procps
Requires:       rpm
Requires:       shadow
Requires:       snapper
Requires:       wtmpdb
# people are addicted to sudo
Requires:       sudo
Requires:       systemd
Requires:       systemd-coredump
Requires:       system-group-wheel
Requires:       sudo-policy-wheel-auth-self
Requires:       terminfo-base
Requires:       timezone
Requires:       udisks2
Requires:       unzip
Requires:       upower
Requires:       usbutils
Requires:       util-linux
Requires:       wget
Requires:       xdg-utils
Requires:       yast2-logs
Requires:       group(nobody)
Requires:       user(nobody)

Obsoletes:      suse-build-key < 12.1

Conflicts:      gettext-runtime-mini

### Bootloader
# Installed system. The Agama live ISO may still boot with grub2.
Requires:       systemd-boot
Requires:       sdbootutil
Requires:       sdbootutil-snapper
Requires:       sdbootutil-dracut-measure-pcr
Requires:       efibootmgr
%ifarch aarch64 x86_64
Requires:       mokutil
Requires:       shim
%endif
Requires:       uefi_mbr
# TPM-2.0 support (boo#1211835)
Requires:       tpm2.0-abrmd
Requires:       tpm2-0-tss
Requires:       tpm2.0-tools
Requires:       dracut-pcr-signature
# Plymouth on the installed system. Branding stays installed so the
# plymouth-branding dependency is met and bgrt remains the fallback.
# plymouth-theme-atropos selects the AtropOS theme. The splash command
# line is shipped by atropos-performance.
Requires:       plymouth
Requires:       plymouth-dracut
Requires:       plymouth-branding-openSUSE
Requires:       plymouth-theme-atropos

### Filesystem Support
Requires:       btrfsmaintenance
Requires:       btrfsprogs
# Support CIFS mounting via mount (boo#1225682)
Requires:       cifs-utils
Requires:       dosfstools
# exfat is an important filesystem too (boo#1222955)
Requires:       exfatprogs
# Add gvfs and gvfs-backends for usability with some flatpaks (boo#1216667)
Requires:       gvfs
Requires:       gvfs-backends
# Add for mounting network shares in userspace (boo#1210125)
Requires:       kdenetwork-filesharing
Requires:       kdnssd
Requires:       kio-fuse
Requires:       ntfs-3g
Requires:       ntfsprogs

### SELinux requirements
Requires:       container-selinux
Requires:       policycoreutils
Requires:       policycoreutils-python-utils
Requires:       selinux-policy-targeted
Requires:       selinux-tools
# Relabel a read-only btrfs root from the initrd. Aeon uses this package.
Requires:       selinux-autorelabel
# Flatpak Steam, Proton, Wine, and Lutris do not depend on this RPM.
# It enables selinuxuser_execmod, execstack, and execheap.
Requires:       selinux-policy-targeted-gaming

### Virtualization and container support
# Kapsule is a KDE Linux pre-alpha. It is not packaged for openSUSE,
# and it needs Incus. Distrobox is the container tool. Podman requires
# runc, netavark, aardvark-dns, passt, conmon, catatonit, and
# fuse-overlayfs, so a new container can be created without extra packages.
Requires:       distrobox
Requires:       podman
Requires:       qemu-guest-agent
Requires:       spice-vdagent

# sysextmgr breaks transactional-update. Leave it out.

### Device support requirements
## Audio Support: Pipewire is the Default sound server
Requires:       alsa-ucm-conf
Requires:       gstreamer-plugin-pipewire
Requires:       pipewire-alsa
Requires:       pipewire-pulseaudio
# Phonon and its VLC backend are not required. Plasma plays through
# PipeWire. Okular, which used Phonon, is the Flathub app.
Requires:       plasma6-pa
## Printing and Scanning Support
Requires:       cups
Requires:       cups-filters
Requires:       cups-pk-helper
Requires:       epson-inkjet-printer-escpr
Requires:       ghostscript
Requires:       hplip-hpijs
Requires:       kde-print-manager
Requires:       OpenPrintingPPDs
Requires:       printer-driver-brlaser
# Scanner Support (boo#1214614)
Requires:       sane-backends
Requires:       system-config-printer-common
Requires:       system-config-printer-dbus-service
Requires:       udev-configure-printer
## Thunderbolt device management (boo#1208150)
Requires:       bolt
## Bluetooth
Requires:       bluedevil6
Requires:       bluez-cups
Requires:       bluez-firmware
# Support bluetooth file transfer (boo#1225682)
Requires:       bluez-obexd
## For i2c dev handling and permissions
Requires:       ddcutil-i2c-udev-rules
## Add steam-devices to negate users needing to after install
Requires:       steam-devices
# Needed to ensure the desktop can handle varied hardware out of the box
Requires:       kernel-firmware-all
Requires:       sof-firmware
# Support screen rotation (boo#1222711)
Requires:       iio-sensor-proxy
## Support wacom tablets
Requires:       libinput-udev
# Add switcheroo-control
Requires:       switcheroo-control
# Support Vulkan (boo#1223443)
Requires:       libvulkan_radeon
Requires:       libvulkan_intel
# Fingerprint readers, FIDO security keys, and smartcard readers.
# Installed only. No PAM factor is added, so a missing finger or key
# cannot block login. Fingerprint enrollment is System Settings, and
# the lock screen uses kde-fingerprint. pcscd.socket is enabled by the
# SUSE preset. libfido2-1 does not require its udev rules.
Requires:       fprintd
Requires:       fprintd-pam
Requires:       pam_u2f
Requires:       libfido2-1
Requires:       libfido2-udev
Requires:       pcsc-ccid
Requires:       pcsc-acsccid
# Add Mesa-demos-egl for kinfocenter6 (kde#502129)
Requires:       Mesa-demo-egl

# from data/COMMON-DESKTOP
Requires:       desktop-data
Requires:       desktop-file-utils
#
### Power Management
# power-profiles-daemon misbehaves on some machines. tuned-ppd serves the
# same org.freedesktop.UPower.PowerProfiles API and conflicts with it.
# powerdevil6 only recommends power-profiles-daemon, so this require wins.
Requires:       tuned-ppd
Conflicts:      power-profiles-daemon

### Desktop performance
# zram-generator replaces systemd-zram-service so the KDE Linux zram size applies once.
# atropos-performance enables systemd-oomd (from systemd-experimental) and powertop.
Requires:       zram-generator
Requires:       dbus-broker
Requires:       flatpak
Requires:       atropos-performance
# Boot-time check. Repairs writable /etc drift and does not roll back snapshots.
Requires:       atropos-check

### Networking requirements
Requires:       avahi
Requires:       ethtool
Requires:       fcoe-utils
Requires:       iproute2
Requires:       iputils
# For Mobile Broadband Support (boo#1230006)
Requires:       libmbim
Requires:       NetworkManager
Requires:       NetworkManager-bluetooth
Requires:       NetworkManager-openvpn
Requires:       NetworkManager-wifi
Requires:       openvpn-auth-pam-plugin
Requires:       plasma6-nm
Requires:       plasma6-nm-openconnect
Requires:       plasma6-nm-openvpn
Requires:       wireguard-tools

### Package management
# Discover shows Flatpaks, firmware, and Slowroll updates. The PackageKit
# backend stages transactional-update dup and does not reboot. The zypp
# backend is not installed, so Discover cannot commit into the running snapshot.
Requires:       discover-backend-flatpak
Requires:       discover-backend-fwupd
Requires:       discover-backend-packagekit
Requires:       discover6-notifier
Requires:       PackageKit-backend-atropos
Requires:       transactional-update
Requires:       transactional-update-zypp-config
Requires:       transactional-update-notifier
Requires:       zypper
Requires:       zypp-no-recommends
# zypper ps is useless in transactional mode. It also checks for
# /run/reboot-needed though which is created by transactional-update
Requires:       zypper-needs-restarting

### Plasma requirements
Requires:       aurorae6
Requires:       breeze6-wallpapers
Requires:       kf6-breeze-icons
Requires:       ffmpegthumbs
Requires:       flatpak-kcm6
Requires:       kaccounts-integration
Requires:       kaccounts-providers
Requires:       kde-gtk-config6
Requires:       kde-gtk-config6-gtk3
Requires:       kde-inotify-survey
Requires:       kdeconnect-kde
Requires:       kdegraphics-thumbnailers
Requires:       kdeplasma6-addons
Requires:       kf6-baloo-file
Requires:       kf6-purpose
Requires:       kf6-qqc2-desktop-style
Requires:       kgamma6
Requires:       kio-extras
Requires:       kio-gdrive
Requires:       ksshaskpass6
Requires:       kunifiedpush
Requires:       kwalletmanager
Requires:       libappindicator3-1
Requires:       ocean-sound-theme6
Requires:       oxygen5-sounds
Requires:       pam_kwallet6
Requires:       pinentry
Requires:       plasma-browser-integration
Requires:       plasma6-session
# Welcome Center. It remembers its own seen version and shows itself once.
Requires:       plasma6-welcome
Requires:       plasma6-desktop-emojier
Requires:       plasma6-systemmonitor
Requires:       plasma6-workspace-wallpapers
Requires:       qt6-imageformats
# plasma-login-manager replaces SDDM. Kalpa's display-manager.service
# alias points at display-manager-legacy.service, and a preset cannot
# retarget that symlink, so %post force-enables plasmalogin.service.
# plasmalogin.service already waits on udevadm settle.
Requires:       plasma-login-manager
Requires:       plasma-login-manager-kcm
# Ctrl+Alt+Shift+Esc session recovery. It returns to the previous TTY.
Requires:       atychia
Conflicts:      sddm-qt6
Conflicts:      sddm-kcm6
# Needed for proper theming support with GTK Flatpaks
Requires:       xdg-desktop-portal-gtk
Requires:       xdg-desktop-portal-kde6

### Themeing and Branding
# Stock openSUSE Plasma branding. There is no plasma6-branding-MicroOS.
# Logos and presets do have a MicroOS package: that is the immutable set.
# atropos-performance disables transactional-update.timer over that preset.
# kalpa-themes is not used. The installed desktop is Aether Dark,
# Kora, the Adobe Source fonts, and the AtropOS wallpapers.
# The global theme wallpaper is atropos-02.
# The silent video is the Video wallpaper type in the same package.
# The login splash is AtropOS. Aether Dark does not ship a splash.
# kdeplasma6-addons provides the Cover Switch task switcher.
# kora-icon-theme ships the dark theme as "kora". Papirus stays
# installed so it can be selected later.
Requires:       plasma6-theme-aether-dark
Requires:       atropos-wallpapers
Requires:       plasma6-lookandfeel-atropos
Requires:       kora-icon-theme
Requires:       papirus-icon-theme
# Third-party Qt themes. The default widget style stays Breeze.
Requires:       kvantum-qt6
Requires:       kvantum-qt5
Requires:       kvantum-manager
Requires:       kvantum-themes
Requires:       plasma6-branding-openSUSE
Requires:       distribution-logos-openSUSE-MicroOS
Requires:       (gtk4-metatheme-breeze if gtk4)
Requires:       (gtk2-metatheme-breeze if gtk2)
Requires:       (gtk3-metatheme-breeze if gtk3)
Requires:       hicolor-icon-theme-branding-openSUSE
Requires:       AtropOS-release
Requires:       systemd-icon-branding-openSUSE
Requires:       systemd-presets-branding-MicroOS

### Applications
# RPMs that need to talk to the system directly. Photos, Okular, KWrite,
# MPC-QT, KCalc, KolourPaint, Safe Eyes, and Firefox come from Flathub
# through atropos-performance on first boot. Ark is an RPM so Dolphin
# can open archives with the system service menus.
Requires:       dolphin
Requires:       ark
Requires:       konsole
Requires:       partitionmanager
Requires:       spectacle
Requires:       krfb
Requires:       krdc
Requires:       khelpcenter
Requires:       fastfetch
Requires:       susepaste
Requires:       vim-small
Requires:       openssh

### Tools
# di-edid-decode is /usr/bin/di-edid-decode from libdisplay-info-tools.
Requires:       sensors
Requires:       libdisplay-info-tools
Requires:       clinfo
Requires:       inxi
Requires:       conky
Requires:       htop
Requires:       git
Requires:       plocate
# unrar is in the non-oss repository. The product already enables that repo.
Requires:       unrar
Requires:       lrzip
Requires:       android-udev-rules
Requires:       android-tools
# Installed with ENABLED=no. No preset starts ufw.service.
Requires:       ufw
# The hunspell package also requires the en_US dictionary.
Requires:       hunspell
Requires:       kup-backup
Requires:       orca
Requires:       yubikey-manager
Requires:       flatpak-builder
Requires:       mcfly

### Fonts
Requires:       adobe-sourcecodepro-fonts
Requires:       adobe-sourcesans3-fonts
Requires:       adobe-sourcesanspro-fonts
Requires:       adobe-sourceserifpro-fonts
Requires:       adwaita-fonts
Requires:       cantarell-fonts
Requires:       dejavu-fonts
Requires:       ghostscript-fonts-std
Requires:       ghostscript-fonts-std-converted
Requires:       google-carlito-fonts
Requires:       google-noto-sans-cjk-fonts
Requires:       google-noto-coloremoji-fonts
Requires:       google-noto-sans-symbols-fonts
Requires:       google-noto-sans-symbols2-fonts
Requires:       google-opensans-fonts
Requires:       google-roboto-fonts
Requires:       hack-fonts
Requires:       ibm-plex-mono-fonts
Requires:       liberation-fonts
Requires:       suse-fonts

%ifarch x86_64
%package nvidia
Summary:        NVIDIA open driver (signed G07)
Group:          Metapackages
Provides:       pattern() = atropos_nvidia
Provides:       pattern-category() = AtropOS
Provides:       pattern-icon() = pattern-kubic
Provides:       pattern-order() = 9210
Provides:       pattern-visible()
ExclusiveArch:  x86_64
Requires:       atropos-nvidia
# Keep a supplemented solve on the open longterm module.
Conflicts:      nvidia-open-driver-G07-signed-kmp-default
Conflicts:      nvidia-open-driver-G07-signed-cuda-kmp-default
Conflicts:      nvidia-open-driver-G07-signed-cuda-kmp-longterm
Conflicts:      nvidia-open-driver-G06-signed-kmp-meta
Conflicts:      nvidia-driver-G06-kmp-meta

%description nvidia
Open NVIDIA driver for Turing and newer GPUs. First boot installs
nvidia-open-driver-G07-signed-kmp-meta, the kernel-longterm module, and
the matching G07 userspace from the NVIDIA repository, then asks for a
reboot. The proprietary driver and the CUDA kernel module are left out.
%endif

%package debug
Summary:        Debugging tools
Group:          Metapackages
Provides:       pattern() = atropos_debug
Provides:       pattern-category() = AtropOS
Provides:       pattern-icon() = pattern-kubic
Provides:       pattern-order() = 9220
Provides:       pattern-visible()
Requires:       gdb
Requires:       strace
Requires:       ltrace
Requires:       perf
Requires:       elfutils
Requires:       elfutils-debuginfod
Requires:       binutils
Requires:       valgrind
Requires:       heaptrack
Requires:       bcc-tools
Requires:       bpftrace

%description debug
Debuggers and tracers, including gdb. Also enables the Slowroll debuginfo
repository so symbols can be installed later. It does not download every
debuginfo package. kdump and the crashkernel reservation are not included.

%description base
AtropOS base system. Immutable Plasma desktop built from openSUSE Slowroll
packages, with TPM measured boot, systemd-oomd, Aether Dark, and the
AtropOS 02 wallpaper.

%prep
# empty on purpose

%build
# empty on purpose

%install
mkdir -p %{buildroot}%{_docdir}/patterns-atropos/
PATTERNS='
    base
    debug
'
%ifarch x86_64
PATTERNS="$PATTERNS
    nvidia
"
%endif
for i in $PATTERNS; do
    echo "This file marks the pattern $i to be installed." \
        > %{buildroot}%{_docdir}/patterns-atropos/${i}.txt
done
install -D -m 0644 %{SOURCE1} %{buildroot}%{_prefix}/lib/systemd/system-preset/79-atropos-display.preset
install -D -m 0644 %{SOURCE2} %{buildroot}%{_prefix}/lib/environment.d/99-atropos-applications.conf
install -D -m 0644 %{SOURCE3} %{buildroot}%{_sysconfdir}/profile.d/atropos-applications.sh
install -D -m 0644 %{SOURCE4} %{buildroot}%{_sysconfdir}/profile.d/atropos-applications.csh
install -D -m 0644 %{SOURCE5} %{buildroot}%{_datadir}/atropos/applications/org.qt.qdbusviewer6.desktop
install -D -m 0644 %{SOURCE6} %{buildroot}%{_sysconfdir}/zypp/repos.d/atropos-oss-debug.repo
install -D -m 0644 %{SOURCE7} %{buildroot}%{_sysconfdir}/bash.bashrc.local
install -D -m 0644 %{SOURCE8} %{buildroot}%{_prefix}/lib/systemd/system-preset/82-atropos-tools.preset
install -D -m 0644 %{SOURCE5} %{buildroot}%{_sysconfdir}/skel/.local/share/applications/org.qt.qdbusviewer6.desktop

%post base
# Disable the legacy alias before enabling plasmalogin. Both units use
# Alias=display-manager.service, and disabling the old one afterwards
# would remove the symlink this enable just created.
if [ -x /usr/bin/systemctl ]; then
    systemctl --no-reload disable display-manager-legacy.service >/dev/null 2>&1 || :
    systemctl --no-reload disable sddm.service >/dev/null 2>&1 || :
    systemctl --no-reload enable --force plasmalogin.service >/dev/null 2>&1 || :
    # plocate's own scriptlet runs before this package's preset is installed.
    systemctl --no-reload preset plocate-updatedb.timer >/dev/null 2>&1 || :
fi
# Aeon and MicroOS relabel a read-only root from /etc/selinux/.autorelabel.
# /.autorelabel cannot be written on the snapshot. selinux-autorelabel
# removes the marker after a successful relabel and leaves
# /etc/selinux/.relabelled. Later installs do not request it again.
# Kiwi writes /.kconfig and /.profile while building the live ISO.
if [ ! -e /.kconfig ] && [ ! -e /.profile ] \
    && [ ! -e /etc/selinux/.relabelled ] \
    && [ ! -e /etc/selinux/.autorelabel ]; then
    mkdir -p /etc/selinux
    : > /etc/selinux/.autorelabel
fi

%files base
%dir %{_docdir}/patterns-atropos
%{_docdir}/patterns-atropos/base.txt
%{_prefix}/lib/systemd/system-preset/79-atropos-display.preset
%{_prefix}/lib/systemd/system-preset/82-atropos-tools.preset
%config(noreplace) %{_sysconfdir}/bash.bashrc.local
%{_prefix}/lib/environment.d/99-atropos-applications.conf
%{_sysconfdir}/profile.d/atropos-applications.sh
%{_sysconfdir}/profile.d/atropos-applications.csh
%dir %{_datadir}/atropos
%dir %{_datadir}/atropos/applications
%{_datadir}/atropos/applications/org.qt.qdbusviewer6.desktop
%dir %{_sysconfdir}/skel/.local
%dir %{_sysconfdir}/skel/.local/share
%dir %{_sysconfdir}/skel/.local/share/applications
%{_sysconfdir}/skel/.local/share/applications/org.qt.qdbusviewer6.desktop

%ifarch x86_64
%files nvidia
%{_docdir}/patterns-atropos/nvidia.txt
%endif

%files debug
%dir %{_docdir}/patterns-atropos
%{_docdir}/patterns-atropos/debug.txt
%dir %{_sysconfdir}/zypp
%dir %{_sysconfdir}/zypp/repos.d
%config(noreplace) %{_sysconfdir}/zypp/repos.d/atropos-oss-debug.repo

%changelog
