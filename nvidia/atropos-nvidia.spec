#
# spec file for package atropos-nvidia
#
# First-boot installer for the open signed NVIDIA G07 driver.
# The driver packages themselves stay out of the Agama solver so the
# proprietary G06 supplements in the NVIDIA repository are not selected
# for the user.

Name:           atropos-nvidia
Version:        0.1.0
Release:        1
Summary:        AtropOS open NVIDIA G07 driver setup
License:        GPL-2.0-or-later AND (GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL) AND CC0-1.0
Group:          System/Base
URL:            https://github.com/AtropOSLinux/
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
Requires:       transactional-update
Requires:       openSUSE-repos-Slowroll-NVIDIA

%description
On first boot, installs the signed open NVIDIA G07 driver built for
kernel-longterm into a new snapshot. The package set follows the
openSUSE NVIDIA SDB for Leap and Tumbleweed. A reboot activates it.
The GPU can sleep, suspend does not freeze the session while the
nvidia module is loaded, and GPU memory allocation skips the
security wipe.

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

%pre
%service_add_pre atropos-nvidia-setup.service

%post
%service_add_post atropos-nvidia-setup.service

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

%changelog
