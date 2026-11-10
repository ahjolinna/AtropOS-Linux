#
# spec file for package atropos-nvidia
#
# Agama installs the open signed NVIDIA G07 driver when the
# atropos_nvidia pattern is selected. The install-time repository alias
# is agama-nvidia at priority 200, and Agama removes that alias after
# the solve. This package keeps the same URL at priority 200, so
# Slowroll's libOpenCL1 and nvidia-libXNVCtrl stay installed.

Name:           atropos-nvidia
Version:        0.1.0
Release:        8
Summary:        AtropOS open NVIDIA G07 driver setup
License:        GPL-2.0-or-later AND (GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL) AND CC0-1.0
Group:          System/Base
URL:            https://github.com/ahjolinna/AtropOS-Linux
ExclusiveArch:  x86_64
BuildRequires:  systemd-rpm-macros
Source4:        80-nvidia-power-management.rules
Source5:        99-nvidia-no-freeze-session.rules
Source6:        10-nvidia-no-freeze-session.conf
Source7:        00-nvidia.conf
Source8:        atropos-nvidia.repo
Source9:        gpg-pubkey-nvidia-tumbleweed.asc
Source10:       50-atropos-omit-nouveau.conf
# Not openSUSE-repos-Slowroll-NVIDIA. That service enables this URL at
# the default priority and would replace Slowroll's libOpenCL1.

%description
Installs the signed open NVIDIA G07 driver built for kernel-longterm.
The package set follows the openSUSE NVIDIA SDB for Leap and
Tumbleweed. Agama installs that set when the NVIDIA pattern is
selected, so the first Plasma session is not software rendering.
This package keeps the NVIDIA repository at priority 200 afterwards.
Slowroll keeps libOpenCL1 and nvidia-libXNVCtrl. The GPU can sleep, suspend does not freeze the
session while the nvidia module is loaded, and GPU memory allocation
skips the security wipe.

%prep

%build

%install
install -D -m 0644 %{SOURCE4} %{buildroot}%{_prefix}/lib/udev/rules.d/80-nvidia-power-management.rules
install -D -m 0644 %{SOURCE5} %{buildroot}%{_prefix}/lib/udev/rules.d/99-nvidia-no-freeze-session.rules
install -D -m 0644 %{SOURCE6} %{buildroot}%{_unitdir}/systemd-suspend.service.d/10-nvidia-no-freeze-session.conf
install -D -m 0644 %{SOURCE7} %{buildroot}%{_prefix}/lib/modprobe.d/00-nvidia.conf
install -D -m 0644 %{SOURCE10} %{buildroot}%{_prefix}/lib/dracut/dracut.conf.d/50-atropos-omit-nouveau.conf
install -D -m 0644 %{SOURCE8} %{buildroot}%{_sysconfdir}/zypp/repos.d/atropos-nvidia.repo
install -D -m 0644 %{SOURCE9} %{buildroot}%{_prefix}/lib/rpm/gnupg/keys/gpg-pubkey-nvidia-tumbleweed.asc

%post
# Release 7 and older enabled atropos-nvidia-setup.service, which
# installed the driver on first boot. patterns-atropos-nvidia requires
# the driver, so Agama installs it and the unit only delayed the login
# screen. Its unit file is gone; remove the enablement link it left.
rm -f %{_sysconfdir}/systemd/system/multi-user.target.wants/atropos-nvidia-setup.service || :
# Agama drops the agama-nvidia alias before it copies repos.d.
# This package is what keeps the URL on the installed system.
rpm --import %{_prefix}/lib/rpm/gnupg/keys/gpg-pubkey-nvidia-tumbleweed.asc || :
# Every package on download.nvidia.com carries a EULA. transactional-update
# dup passes only --auto-agree-with-product-licenses, so zypper answers
# "no" to the package license and the update aborts with exit 4. That
# breaks Discover, the CLI and the timer alike. Agree at commit, as the
# installer and atropos-nvidia-apply already do with
# --auto-agree-with-licenses. zypper reads this file from the running
# system, so it takes effect after the snapshot carrying it is booted.
conf=%{_sysconfdir}/zypp/zypper.conf
if [ -f "$conf" ] && ! grep -Eq '^[[:space:]]*autoAgreeWithLicenses[[:space:]]*=[[:space:]]*(yes|1|true|on)[[:space:]]*$' "$conf"; then
    if grep -Eq '^[[:space:]]*autoAgreeWithLicenses[[:space:]]*=' "$conf"; then
        sed -i -E 's/^[[:space:]]*autoAgreeWithLicenses[[:space:]]*=.*/autoAgreeWithLicenses = yes/' "$conf" || :
    elif grep -Eq '^#[[:space:]]*autoAgreeWithLicenses[[:space:]]*=' "$conf"; then
        sed -i -E '0,/^#[[:space:]]*autoAgreeWithLicenses[[:space:]]*=.*/s//autoAgreeWithLicenses = yes/' "$conf" || :
    else
        printf '\n[commit]\nautoAgreeWithLicenses = yes\n' >> "$conf" || :
    fi
fi

%files
%dir %{_prefix}/lib/udev
%dir %{_prefix}/lib/udev/rules.d
%{_prefix}/lib/udev/rules.d/80-nvidia-power-management.rules
%{_prefix}/lib/udev/rules.d/99-nvidia-no-freeze-session.rules
%dir %{_unitdir}/systemd-suspend.service.d
%{_unitdir}/systemd-suspend.service.d/10-nvidia-no-freeze-session.conf
%dir %{_prefix}/lib/modprobe.d
%{_prefix}/lib/modprobe.d/00-nvidia.conf
# dracut owns these when it is installed. The OBS build root may not.
%dir %{_prefix}/lib/dracut
%dir %{_prefix}/lib/dracut/dracut.conf.d
%{_prefix}/lib/dracut/dracut.conf.d/50-atropos-omit-nouveau.conf
# rpm and AtropOS-release own these directories when they are installed.
# The OBS build root for this package does not, so own them here too.
%dir %{_prefix}/lib/rpm
%dir %{_prefix}/lib/rpm/gnupg
%dir %{_prefix}/lib/rpm/gnupg/keys
%{_prefix}/lib/rpm/gnupg/keys/gpg-pubkey-nvidia-tumbleweed.asc
%dir %{_sysconfdir}/zypp
%dir %{_sysconfdir}/zypp/repos.d
%config(noreplace) %{_sysconfdir}/zypp/repos.d/atropos-nvidia.repo

%changelog
