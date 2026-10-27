#
# spec file for package atropos-performance
#
# Performance defaults adapted from KDE Linux. File headers keep the
# upstream copyright. The Flatpak seeder and the installed-desktop skel
# are AtropOS-specific. New accounts get Aether Dark, Kora, the
# Adobe Source fonts, and the AtropOS splash. The task switcher
# is Cover Switch. Papirus stays installed as the alternate icon theme.

Name:           atropos-performance
Version:        0.4.0
Release:        15
Summary:        AtropOS desktop performance defaults
License:        (GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL) AND Apache-2.0 AND ISC AND CC0-1.0 AND GPL-3.0-or-later AND GPL-2.0-or-later AND LGPL-2.0-or-later
Group:          System/Base
URL:            https://github.com/ahjolinna/AtropOS-Linux
BuildArch:      noarch
BuildRequires:  systemd-rpm-macros
# The wants links below point at units this package does not ship.
# brp-25-symlink rejects the link unless the target is installed.
BuildRequires:  tuned
BuildRequires:  tuned-ppd
Source0:        11-atropos-performance.conf
Source1:        00-atropos-zram.conf
Source2:        00-atropos-system.conf
Source3:        00-atropos-user.conf
Source4:        00-atropos-journald.conf
Source5:        80-atropos.preset
Source6:        atropos-seed-flatpaks
Source7:        atropos-seed-flatpaks.service
Source8:        flatpak-preinstall.txt
Source9:        00-atropos-oomd.conf
Source10:       00-atropos-system-oom.conf
Source11:       00-atropos-user-oom.conf
Source12:       atropos-powertop.service
Source13:       disable-usb-autosuspend-for-input-devices
Source14:       kdeglobals
Source15:       plasmarc
Source16:       kwinrc
Source17:       kcminputrc
Source18:       ksplashrc
Source19:       00-atropos-cmdline.conf
Source20:       atropos-transactional-update.conf
Source21:       ntsync.conf
Source22:       60-ioschedulers.rules
Source23:       60-usb-dirty-pages-udev.rules
Source24:       atropos-usb-dirty-pages
Source25:       20-audio-pm.conf
Source26:       20-audio-pm.rules
Source27:       98-atropos-watchdog.conf
Source28:       00-atropos-limits.conf
Source29:       00-atropos-killuserprocesses.conf
Source30:       00-atropos-nowall.conf
Source31:       baloofilerc
Source32:       qtlogging.ini
Source33:       50-atropos-workarounds.conf
Source34:       60-atropos-ssh-askpass.conf
Source35:       60-atropos-ssh-auth-sock.conf
Source36:       atropos-iw-set-regdomain
Source37:       85-iw-regulatory.rules
Source38:       atropos-iw-set-regdomain.service
Source39:       atropos-iw-set-regdomain.path
Source40:       55-atropos-hardening.conf
Source41:       00-atropos-geoclue.conf
Source42:       atropos-configure-firefox
Source43:       00-atropos-default.js
Source44:       01-atropos-nvidia-hw-decode.js
Source45:       firefox-policies.json
Source46:       atropos-configure-firefox.service
Source47:       atropos-configure-firefox.path
# OpenRGB 1.0~rc3 commit 5e81e26f, from the openSUSE OpenRGB-udev-rules
# package. That package requires the OpenRGB application. The Flatpak does
# not, so the rules and the openrgb group are shipped here instead.
Source48:       60-openrgb.rules
Source49:       openrgb-udev.conf
Source50:       gtk-3.0-settings.ini
Source51:       gtk-4.0-settings.ini
Source52:       91-atropos-source-fonts.conf
Source53:       atropos-cover-switch
Source54:       atropos-cover-switch.service
Source55:       plasmalogin.conf
Source56:       01-flatpaks.qml
Source57:       atropos-tuned-log.conf
Source58:       90-atropos-hide-system-disk.rules
Source59:       atropos-hide-system-disk
Source60:       atropos-hide-system-disk.service
Source61:       atropos-formcard-overlay
Source62:       atropos-formcard-overlay.service
Source63:       atropos-AboutPage.qml
Source64:       atropos-formcard.qmldir
Source65:       atropos-bootloader
Source66:       atropos-bootloader-show.service
Source67:       atropos-bootloader-hide.service
Source68:       10-atropos-tuned-ppd.conf
Requires:       zram-generator
Requires:       flatpak
Requires:       systemd-experimental
Requires:       powertop
Requires:       thermald
Requires:       iw
Requires:       ksshaskpass6
Requires:       geoclue2
Requires:       speech-dispatcher
Requires:       acl
Requires:       plasma6-theme-aether-dark
Requires:       kora-icon-theme
Requires:       papirus-icon-theme
Requires:       adobe-sourcesans3-fonts
Requires:       adobe-sourceserifpro-fonts
Requires:       adobe-sourcecodepro-fonts
Requires:       tuned
Requires:       tuned-ppd
Requires:       python3
Requires:       udev

%description
Sysctl, zram, systemd-oomd, and powertop defaults taken from KDE Linux,
an Aether Dark skel with Kora icons, Adobe Source fonts, and the
AtropOS splash, the silent boot command line, and a first-boot
install of the Flathub application set. Firefox in that set is
the Flathub app. Papirus stays installed. Welcome Center says those
Flatpaks are still downloading. The login screen uses the AtropOS 02
wallpaper. Cover Switch is applied once for an account still on KWin's
default switcher. tuned.service and tuned-ppd.service are enabled
together. Also the remaining KDE Linux desktop drop-ins: ntsync,
BFQ for spinning disks, USB writeback cap, HDA power save, watchdog
module blacklist, PAM memlock, logout process cleanup, no shutdown wall,
Baloo snapshot skip, quieter Qt logging, the AMD texture workaround,
ksshaskpass, the ssh-agent socket, Wi-Fi country from the timezone,
network hardening, thermald, BeaconDB location, and Firefox Flatpak setup.
Also the OpenRGB udev rules, so the OpenRGB Flatpak can open USB and I2C
RGB devices. The openrgb group is created for the I2C lines. USB devices
use uaccess and do not need that group. New accounts use
org.atropos.desktop as the dark global theme. Welcome Center animates
the Flatpak download. The encrypted system disk is hidden from Dolphin.
Discover's About page says RPM. The systemd-boot menu stays hidden
after a normal shutdown and is shown for 7 seconds after a crash.
Hold Space to open it. Ark is an RPM, not a seeded Flatpak.

%prep

%build

%install
install -D -m 0644 %{SOURCE0} %{buildroot}%{_prefix}/lib/sysctl.d/11-atropos-performance.conf
install -D -m 0644 %{SOURCE1} %{buildroot}%{_prefix}/lib/systemd/zram-generator.conf.d/00-atropos.conf
install -D -m 0644 %{SOURCE2} %{buildroot}%{_prefix}/lib/systemd/system.conf.d/00-atropos.conf
install -D -m 0644 %{SOURCE3} %{buildroot}%{_prefix}/lib/systemd/user.conf.d/00-atropos.conf
install -D -m 0644 %{SOURCE4} %{buildroot}%{_prefix}/lib/systemd/journald.conf.d/00-atropos.conf
install -D -m 0644 %{SOURCE5} %{buildroot}%{_prefix}/lib/systemd/system-preset/80-atropos.preset
install -D -m 0755 %{SOURCE6} %{buildroot}%{_libexecdir}/atropos-seed-flatpaks
install -D -m 0644 %{SOURCE7} %{buildroot}%{_unitdir}/atropos-seed-flatpaks.service
install -D -m 0644 %{SOURCE8} %{buildroot}%{_datadir}/atropos/flatpak-preinstall.txt
install -D -m 0644 %{SOURCE9} %{buildroot}%{_prefix}/lib/systemd/oomd.conf.d/00-atropos.conf
install -D -m 0644 %{SOURCE10} %{buildroot}%{_prefix}/lib/systemd/system/system.slice.d/00-atropos.conf
install -D -m 0644 %{SOURCE11} %{buildroot}%{_prefix}/lib/systemd/system/user.slice.d/00-atropos.conf
install -D -m 0644 %{SOURCE12} %{buildroot}%{_unitdir}/atropos-powertop.service
install -D -m 0755 %{SOURCE13} %{buildroot}%{_prefix}/lib/disable-usb-autosuspend-for-input-devices
install -D -m 0644 %{SOURCE14} %{buildroot}%{_sysconfdir}/skel/.config/kdeglobals
install -D -m 0644 %{SOURCE15} %{buildroot}%{_sysconfdir}/skel/.config/plasmarc
install -D -m 0644 %{SOURCE16} %{buildroot}%{_sysconfdir}/skel/.config/kwinrc
install -D -m 0644 %{SOURCE17} %{buildroot}%{_sysconfdir}/skel/.config/kcminputrc
install -D -m 0644 %{SOURCE18} %{buildroot}%{_sysconfdir}/skel/.config/ksplashrc
install -D -m 0644 %{SOURCE50} %{buildroot}%{_sysconfdir}/skel/.config/gtk-3.0/settings.ini
install -D -m 0644 %{SOURCE51} %{buildroot}%{_sysconfdir}/skel/.config/gtk-4.0/settings.ini
install -D -m 0644 %{SOURCE52} %{buildroot}%{_datadir}/fontconfig/conf.avail/91-atropos-source-fonts.conf
install -d %{buildroot}%{_sysconfdir}/fonts/conf.d
ln -s ../../../usr/share/fontconfig/conf.avail/91-atropos-source-fonts.conf \
    %{buildroot}%{_sysconfdir}/fonts/conf.d/91-atropos-source-fonts.conf
install -D -m 0644 %{SOURCE19} %{buildroot}%{_prefix}/kernel/cmdline.d/00-atropos-silent.conf
install -D -m 0644 %{SOURCE20} %{buildroot}%{_prefix}/etc/transactional-update.conf.d/atropos.conf
install -D -m 0644 %{SOURCE21} %{buildroot}%{_prefix}/lib/modules-load.d/ntsync.conf
install -D -m 0644 %{SOURCE22} %{buildroot}%{_prefix}/lib/udev/rules.d/60-ioschedulers.rules
install -D -m 0644 %{SOURCE23} %{buildroot}%{_prefix}/lib/udev/rules.d/60-usb-dirty-pages-udev.rules
install -D -m 0755 %{SOURCE24} %{buildroot}%{_prefix}/lib/atropos-usb-dirty-pages
install -D -m 0644 %{SOURCE25} %{buildroot}%{_prefix}/lib/modprobe.d/20-audio-pm.conf
install -D -m 0644 %{SOURCE26} %{buildroot}%{_prefix}/lib/udev/rules.d/20-audio-pm.rules
install -D -m 0644 %{SOURCE27} %{buildroot}%{_prefix}/lib/modprobe.d/98-atropos-watchdog.conf
install -D -m 0644 %{SOURCE28} %{buildroot}%{_sysconfdir}/security/limits.d/00-atropos.conf
install -D -m 0644 %{SOURCE29} %{buildroot}%{_prefix}/lib/systemd/logind.conf.d/00-atropos-killuserprocesses.conf
install -D -m 0644 %{SOURCE30} %{buildroot}%{_prefix}/lib/systemd/logind.conf.d/00-atropos-nowall.conf
# plasma6-branding-openSUSE owns /etc/xdg/baloofilerc and replaces it.
# /usr/etc/xdg is already on XDG_CONFIG_DIRS, and KConfig merges the two.
install -D -m 0644 %{SOURCE31} %{buildroot}%{_prefix}/etc/xdg/baloofilerc
install -D -m 0644 %{SOURCE32} %{buildroot}%{_sysconfdir}/xdg/QtProject/qtlogging.ini
install -D -m 0644 %{SOURCE33} %{buildroot}%{_prefix}/lib/environment.d/50-atropos-workarounds.conf
install -D -m 0644 %{SOURCE34} %{buildroot}%{_prefix}/lib/environment.d/60-atropos-ssh-askpass.conf
install -D -m 0644 %{SOURCE35} %{buildroot}%{_prefix}/lib/environment.d/60-atropos-ssh-auth-sock.conf
install -D -m 0755 %{SOURCE36} %{buildroot}%{_prefix}/lib/atropos-iw-set-regdomain
install -D -m 0644 %{SOURCE37} %{buildroot}%{_prefix}/lib/udev/rules.d/85-iw-regulatory.rules
install -D -m 0644 %{SOURCE38} %{buildroot}%{_unitdir}/atropos-iw-set-regdomain.service
install -D -m 0644 %{SOURCE39} %{buildroot}%{_unitdir}/atropos-iw-set-regdomain.path
install -D -m 0644 %{SOURCE40} %{buildroot}%{_prefix}/lib/sysctl.d/55-atropos-hardening.conf
install -D -m 0644 %{SOURCE41} %{buildroot}%{_sysconfdir}/geoclue/conf.d/00-atropos.conf
install -D -m 0755 %{SOURCE42} %{buildroot}%{_libexecdir}/atropos-configure-firefox
install -D -m 0644 %{SOURCE43} %{buildroot}%{_datadir}/atropos/firefox-config/00-atropos-default.js
install -D -m 0644 %{SOURCE44} %{buildroot}%{_datadir}/atropos/firefox-config/01-atropos-nvidia-hw-decode.js
install -D -m 0644 %{SOURCE45} %{buildroot}%{_datadir}/atropos/firefox-config/policies.json
install -D -m 0644 %{SOURCE46} %{buildroot}%{_unitdir}/atropos-configure-firefox.service
install -D -m 0644 %{SOURCE47} %{buildroot}%{_unitdir}/atropos-configure-firefox.path
install -D -m 0644 %{SOURCE48} %{buildroot}%{_prefix}/lib/udev/rules.d/60-openrgb.rules
install -D -m 0644 %{SOURCE49} %{buildroot}%{_prefix}/lib/sysusers.d/openrgb-udev.conf
install -D -m 0755 %{SOURCE53} %{buildroot}%{_libexecdir}/atropos-cover-switch
install -D -m 0644 %{SOURCE54} %{buildroot}%{_userunitdir}/atropos-cover-switch.service
mkdir -p %{buildroot}%{_userunitdir}/default.target.wants
ln -s ../atropos-cover-switch.service %{buildroot}%{_userunitdir}/default.target.wants/atropos-cover-switch.service
install -D -m 0644 %{SOURCE55} %{buildroot}%{_sysconfdir}/plasmalogin.conf
install -D -m 0644 %{SOURCE56} %{buildroot}%{_datadir}/plasma/plasma-welcome/extra-pages/01-flatpaks.qml
install -D -m 0644 %{SOURCE57} %{buildroot}%{_prefix}/lib/tmpfiles.d/atropos-tuned-log.conf
install -D -m 0644 %{SOURCE68} %{buildroot}%{_unitdir}/tuned-ppd.service.d/10-atropos-tuned-ppd.conf
install -D -m 0644 %{SOURCE58} %{buildroot}%{_prefix}/lib/udev/rules.d/90-atropos-hide-system-disk.rules
install -D -m 0755 %{SOURCE59} %{buildroot}%{_libexecdir}/atropos-hide-system-disk
install -D -m 0644 %{SOURCE60} %{buildroot}%{_unitdir}/atropos-hide-system-disk.service
install -D -m 0755 %{SOURCE61} %{buildroot}%{_libexecdir}/atropos-formcard-overlay
install -D -m 0644 %{SOURCE62} %{buildroot}%{_unitdir}/atropos-formcard-overlay.service
install -D -m 0644 %{SOURCE63} %{buildroot}%{_datadir}/atropos/formcard/AboutPage.qml
install -D -m 0644 %{SOURCE64} %{buildroot}%{_datadir}/atropos/formcard/qmldir
install -D -m 0755 %{SOURCE65} %{buildroot}%{_libexecdir}/atropos-bootloader
install -D -m 0644 %{SOURCE66} %{buildroot}%{_unitdir}/atropos-bootloader-show.service
install -D -m 0644 %{SOURCE67} %{buildroot}%{_unitdir}/atropos-bootloader-hide.service
# tuned-ppd Requires tuned.service. Preset used to enable only the
# translation daemon, so a skipped scriptlet left the power-profiles
# bus with nothing to start. The links are static.
mkdir -p %{buildroot}%{_unitdir}/multi-user.target.wants
ln -s ../tuned.service %{buildroot}%{_unitdir}/multi-user.target.wants/tuned.service
mkdir -p %{buildroot}%{_unitdir}/graphical.target.wants
ln -s ../tuned-ppd.service %{buildroot}%{_unitdir}/graphical.target.wants/tuned-ppd.service

%pre
%service_add_pre atropos-seed-flatpaks.service atropos-powertop.service atropos-iw-set-regdomain.path atropos-configure-firefox.path atropos-hide-system-disk.service atropos-formcard-overlay.service atropos-bootloader-show.service atropos-bootloader-hide.service

%post
%service_add_post atropos-seed-flatpaks.service atropos-powertop.service atropos-iw-set-regdomain.path atropos-configure-firefox.path atropos-hide-system-disk.service atropos-formcard-overlay.service atropos-bootloader-show.service atropos-bootloader-hide.service
# systemd-oomd.service, thermald.service, and transactional-update.timer
# are owned by other packages, so the preset helper will not select them.
# Apply 80-atropos.preset directly. The timer is disabled there; the unit
# stays available so Discover can start an update.
if [ -x /usr/bin/systemctl ]; then
    systemctl --no-reload preset systemd-oomd.service >/dev/null 2>&1 || :
    systemctl --no-reload preset thermald.service >/dev/null 2>&1 || :
    systemctl --no-reload preset transactional-update.timer >/dev/null 2>&1 || :
    # tuned-ppd is required by the pattern. Preset it here as well, because
    # its own scriptlet may have run before this preset file existed.
    systemctl --no-reload preset power-profiles-daemon.service >/dev/null 2>&1 || :
    systemctl --no-reload preset tuned.service >/dev/null 2>&1 || :
    systemctl --no-reload preset tuned-ppd.service >/dev/null 2>&1 || :
fi
# Agama installs this package into the target. Root flatpak does not need
# the system bus, so the download can finish before the first boot. Kiwi
# writes /.kconfig and /.profile while it builds the live ISO; skip that,
# or the Offline image would fetch Flathub into the live root. A failure
# leaves the marker unset and atropos-seed-flatpaks.service tries again.
if [ ! -e /.kconfig ] && [ ! -e /.profile ] && [ -x %{_libexecdir}/atropos-seed-flatpaks ]; then
    %{_libexecdir}/atropos-seed-flatpaks || :
fi
# The OpenRGB Flatpak does not install host rules. Create the group and
# reload udev on the target. Kiwi's /.kconfig and /.profile mean this is
# the live ISO build, so leave the build machine's udev alone.
if [ ! -e /.kconfig ] && [ ! -e /.profile ]; then
    if [ -x /usr/bin/systemd-sysusers ]; then
        systemd-sysusers %{_prefix}/lib/sysusers.d/openrgb-udev.conf || :
    fi
    if [ -x /usr/bin/udevadm ]; then
        udevadm control --reload-rules || :
        udevadm trigger --subsystem-match=usb --subsystem-match=hidraw --subsystem-match=i2c || :
    fi
fi

%preun
%service_del_preun atropos-seed-flatpaks.service atropos-powertop.service atropos-iw-set-regdomain.path atropos-configure-firefox.path atropos-hide-system-disk.service atropos-formcard-overlay.service atropos-bootloader-show.service atropos-bootloader-hide.service
if [ "$1" -eq 0 ] && [ -x /usr/bin/systemctl ]; then
    systemctl --no-reload disable --no-warn systemd-oomd.service >/dev/null 2>&1 || :
fi

%postun
%service_del_postun atropos-seed-flatpaks.service atropos-powertop.service atropos-iw-set-regdomain.path atropos-configure-firefox.path atropos-hide-system-disk.service atropos-formcard-overlay.service atropos-bootloader-show.service atropos-bootloader-hide.service
# The preset file is gone on erase, so MicroOS may enable the timer again.
if [ "$1" -eq 0 ] && [ -x /usr/bin/systemctl ]; then
    systemctl --no-reload preset transactional-update.timer >/dev/null 2>&1 || :
fi

%files
%{_prefix}/lib/sysctl.d/11-atropos-performance.conf
# These drop-in directories are not in the OBS build root. zram-generator
# does not ship its conf.d directory, and systemd-default-settings is not
# installed while the package is built.
%dir %{_prefix}/lib/systemd/zram-generator.conf.d
%{_prefix}/lib/systemd/zram-generator.conf.d/00-atropos.conf
%dir %{_prefix}/lib/systemd/system.conf.d
%{_prefix}/lib/systemd/system.conf.d/00-atropos.conf
%dir %{_prefix}/lib/systemd/user.conf.d
%{_prefix}/lib/systemd/user.conf.d/00-atropos.conf
%dir %{_prefix}/lib/systemd/journald.conf.d
%{_prefix}/lib/systemd/journald.conf.d/00-atropos.conf
%{_prefix}/lib/systemd/system-preset/80-atropos.preset
%dir %{_prefix}/lib/systemd/oomd.conf.d
%{_prefix}/lib/systemd/oomd.conf.d/00-atropos.conf
%dir %{_prefix}/lib/systemd/system/system.slice.d
%{_prefix}/lib/systemd/system/system.slice.d/00-atropos.conf
%dir %{_prefix}/lib/systemd/system/user.slice.d
%{_prefix}/lib/systemd/system/user.slice.d/00-atropos.conf
%{_libexecdir}/atropos-seed-flatpaks
%{_unitdir}/atropos-seed-flatpaks.service
%{_unitdir}/atropos-powertop.service
%{_prefix}/lib/disable-usb-autosuspend-for-input-devices
%dir %{_datadir}/atropos
%{_datadir}/atropos/flatpak-preinstall.txt
%dir %{_sysconfdir}/skel/.config
%{_sysconfdir}/skel/.config/kdeglobals
%{_sysconfdir}/skel/.config/plasmarc
%{_sysconfdir}/skel/.config/kwinrc
%{_sysconfdir}/skel/.config/kcminputrc
%{_sysconfdir}/skel/.config/ksplashrc
%dir %{_sysconfdir}/skel/.config/gtk-3.0
%{_sysconfdir}/skel/.config/gtk-3.0/settings.ini
%dir %{_sysconfdir}/skel/.config/gtk-4.0
%{_sysconfdir}/skel/.config/gtk-4.0/settings.ini
%dir %{_datadir}/fontconfig
%dir %{_datadir}/fontconfig/conf.avail
%{_datadir}/fontconfig/conf.avail/91-atropos-source-fonts.conf
%dir %{_sysconfdir}/fonts
%dir %{_sysconfdir}/fonts/conf.d
%{_sysconfdir}/fonts/conf.d/91-atropos-source-fonts.conf
# sdbootutil reads this directory after /etc/kernel/cmdline. It is not
# shipped by filesystem or sdbootutil in the OBS build root.
%dir %{_prefix}/kernel
%dir %{_prefix}/kernel/cmdline.d
%{_prefix}/kernel/cmdline.d/00-atropos-silent.conf
# transactional-update owns this directory when it is installed. It is not
# in the OBS build root.
%dir %{_prefix}/etc/transactional-update.conf.d
%{_prefix}/etc/transactional-update.conf.d/atropos.conf
%dir %{_prefix}/lib/modules-load.d
%{_prefix}/lib/modules-load.d/ntsync.conf
%dir %{_prefix}/lib/modprobe.d
%{_prefix}/lib/modprobe.d/20-audio-pm.conf
%{_prefix}/lib/modprobe.d/98-atropos-watchdog.conf
%dir %{_prefix}/lib/udev
%dir %{_prefix}/lib/udev/rules.d
%{_prefix}/lib/udev/rules.d/20-audio-pm.rules
%{_prefix}/lib/udev/rules.d/60-ioschedulers.rules
%{_prefix}/lib/udev/rules.d/60-usb-dirty-pages-udev.rules
%{_prefix}/lib/udev/rules.d/85-iw-regulatory.rules
%{_prefix}/lib/udev/rules.d/60-openrgb.rules
%dir %{_prefix}/lib/sysusers.d
%{_prefix}/lib/sysusers.d/openrgb-udev.conf
%{_prefix}/lib/atropos-usb-dirty-pages
%{_prefix}/lib/atropos-iw-set-regdomain
%dir %{_prefix}/lib/sysctl.d
%{_prefix}/lib/sysctl.d/55-atropos-hardening.conf
%dir %{_prefix}/lib/environment.d
%{_prefix}/lib/environment.d/50-atropos-workarounds.conf
%{_prefix}/lib/environment.d/60-atropos-ssh-askpass.conf
%{_prefix}/lib/environment.d/60-atropos-ssh-auth-sock.conf
%dir %{_prefix}/lib/systemd/logind.conf.d
%{_prefix}/lib/systemd/logind.conf.d/00-atropos-killuserprocesses.conf
%{_prefix}/lib/systemd/logind.conf.d/00-atropos-nowall.conf
%dir %{_sysconfdir}/security
%dir %{_sysconfdir}/security/limits.d
%config(noreplace) %{_sysconfdir}/security/limits.d/00-atropos.conf
%dir %{_sysconfdir}/xdg
%dir %{_sysconfdir}/xdg/QtProject
%config(noreplace) %{_sysconfdir}/xdg/QtProject/qtlogging.ini
# /usr/etc exists in the build root. This subdirectory does not.
%dir %{_prefix}/etc/xdg
%{_prefix}/etc/xdg/baloofilerc
%dir %{_sysconfdir}/geoclue
%dir %{_sysconfdir}/geoclue/conf.d
%config(noreplace) %{_sysconfdir}/geoclue/conf.d/00-atropos.conf
%{_unitdir}/atropos-iw-set-regdomain.service
%{_unitdir}/atropos-iw-set-regdomain.path
%{_unitdir}/atropos-configure-firefox.service
%{_unitdir}/atropos-configure-firefox.path
%{_libexecdir}/atropos-configure-firefox
%dir %{_datadir}/atropos/firefox-config
%{_datadir}/atropos/firefox-config/00-atropos-default.js
%{_datadir}/atropos/firefox-config/01-atropos-nvidia-hw-decode.js
%{_datadir}/atropos/firefox-config/policies.json
%{_libexecdir}/atropos-cover-switch
%{_userunitdir}/atropos-cover-switch.service
%dir %{_userunitdir}/default.target.wants
%{_userunitdir}/default.target.wants/atropos-cover-switch.service
%config(noreplace) %{_sysconfdir}/plasmalogin.conf
%dir %{_datadir}/plasma
%dir %{_datadir}/plasma/plasma-welcome
%dir %{_datadir}/plasma/plasma-welcome/extra-pages
%{_datadir}/plasma/plasma-welcome/extra-pages/01-flatpaks.qml
%dir %{_prefix}/lib/tmpfiles.d
%{_prefix}/lib/tmpfiles.d/atropos-tuned-log.conf
%dir %{_unitdir}/tuned-ppd.service.d
%{_unitdir}/tuned-ppd.service.d/10-atropos-tuned-ppd.conf
%{_prefix}/lib/udev/rules.d/90-atropos-hide-system-disk.rules
%{_libexecdir}/atropos-hide-system-disk
%{_unitdir}/atropos-hide-system-disk.service
%{_libexecdir}/atropos-formcard-overlay
%{_unitdir}/atropos-formcard-overlay.service
%dir %{_datadir}/atropos/formcard
%{_datadir}/atropos/formcard/AboutPage.qml
%{_datadir}/atropos/formcard/qmldir
%{_libexecdir}/atropos-bootloader
%{_unitdir}/atropos-bootloader-show.service
%{_unitdir}/atropos-bootloader-hide.service
%dir %{_unitdir}/multi-user.target.wants
%{_unitdir}/multi-user.target.wants/tuned.service
%dir %{_unitdir}/graphical.target.wants
%{_unitdir}/graphical.target.wants/tuned-ppd.service

%changelog
