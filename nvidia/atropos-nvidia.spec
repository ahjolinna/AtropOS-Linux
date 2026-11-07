#
# spec file for package atropos-nvidia
#
# Agama installs the open signed NVIDIA G07 driver when the
# atropos_nvidia pattern is selected. The install-time repository alias
# is agama-nvidia at priority 200, and Agama removes that alias after
# the solve. This package keeps the same URL at priority 200, so
# Slowroll's libOpenCL1 and nvidia-libXNVCtrl stay installed. First boot
# installs the driver only when those packages are missing.

Name:           atropos-nvidia
Version:        0.1.0
Release:        6
Summary:        AtropOS open NVIDIA G07 driver setup
License:        GPL-2.0-or-later AND (GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL) AND CC0-1.0
Group:          System/Base
URL:            https://github.com/ahjolinna/AtropOS-Linux
ExclusiveArch:  x86_64
BuildRequires:  systemd-rpm-macros
Source0:        atropos-nvidia-setup
Source1:        atropos-nvidia-apply
Source2:        atropos-nvidia-setup.service
Source3:        80-atropos-nvidia.preset
Source4:        80-nvidia-power-management.rules
Source5:        99-nvidia-no-freeze-session.rules
Source6:        10-nvidia-no-freeze-session.conf
Source7:        00-nvidia.conf
Source8:        atropos-nvidia.repo
Source9:        gpg-pubkey-nvidia-tumbleweed.asc
Requires:       transactional-update
# Not openSUSE-repos-Slowroll-NVIDIA. That service enables this URL at
# the default priority and would replace Slowroll's libOpenCL1.

%description
Installs the signed open NVIDIA G07 driver built for kernel-longterm.
The package set follows the openSUSE NVIDIA SDB for Leap and
Tumbleweed. Agama installs that set when the NVIDIA pattern is
selected, so the first Plasma session is not software rendering.
This package keeps the NVIDIA repository at priority 200 afterwards.
Slowroll keeps libOpenCL1 and nvidia-libXNVCtrl. If the driver
packages are missing, first boot installs them and reboots before
the login screen. The GPU can sleep, suspend does not freeze the
session while the nvidia module is loaded, and GPU memory allocation
skips the security wipe.

%prep

%build

%install
install -D -m 0755 %{SOURCE0} %{buildroot}%{_libexecdir}/atropos-nvidia-setup
install -D -m 0755 %{SOURCE1} %{buildroot}%{_libexecdir}/atropos-nvidia-apply
install -D -m 0644 %{SOURCE2} %{buildroot}%{_unitdir}/atropos-nvidia-setup.service
install -D -m 0644 %{SOURCE3} %{buildroot}%{_prefix}/lib/systemd/system-preset/80-atropos-nvidia.preset
install -D -m 0644 %{SOURCE4} %{buildroot}%{_prefix}/lib/udev/rules.d/80-nvidia-power-management.rules
install -D -m 0644 %{SOURCE5} %{buildroot}%{_prefix}/lib/udev/rules.d/99-nvidia-no-freeze-session.rules
install -D -m 0644 %{SOURCE6} %{buildroot}%{_unitdir}/systemd-suspend.service.d/10-nvidia-no-freeze-session.conf
install -D -m 0644 %{SOURCE7} %{buildroot}%{_prefix}/lib/modprobe.d/00-nvidia.conf
install -D -m 0644 %{SOURCE8} %{buildroot}%{_sysconfdir}/zypp/repos.d/atropos-nvidia.repo
install -D -m 0644 %{SOURCE9} %{buildroot}%{_prefix}/lib/rpm/gnupg/keys/gpg-pubkey-nvidia-tumbleweed.asc

%pre
%service_add_pre atropos-nvidia-setup.service

%post
%service_add_post atropos-nvidia-setup.service
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

%preun
%service_del_preun atropos-nvidia-setup.service

%postun
%service_del_postun atropos-nvidia-setup.service

%files
%{_libexecdir}/atropos-nvidia-setup
%{_libexecdir}/atropos-nvidia-apply
%{_unitdir}/atropos-nvidia-setup.service
%{_prefix}/lib/systemd/system-preset/80-atropos-nvidia.preset
%dir %{_prefix}/lib/udev
%dir %{_prefix}/lib/udev/rules.d
%{_prefix}/lib/udev/rules.d/80-nvidia-power-management.rules
%{_prefix}/lib/udev/rules.d/99-nvidia-no-freeze-session.rules
%dir %{_unitdir}/systemd-suspend.service.d
%{_unitdir}/systemd-suspend.service.d/10-nvidia-no-freeze-session.conf
%dir %{_prefix}/lib/modprobe.d
%{_prefix}/lib/modprobe.d/00-nvidia.conf
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
