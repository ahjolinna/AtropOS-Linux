#
# spec file for package atropos-check
#
# Boot-time and on-demand check for an installed AtropOS system.
# The source-tree check stays in scripts/validate-configs.sh.

Name:           atropos-check
Version:        0.1.0
Release:        5
Summary:        Check and repair an installed AtropOS system
License:        MIT
Group:          System/Base
URL:            https://github.com/AtropOSLinux/
BuildArch:      noarch
BuildRequires:  systemd-rpm-macros
Source0:        atropos-check
Source1:        atropos-check.service
Source2:        81-atropos-check.preset
Requires:       systemd

%description
Reports whether an installed AtropOS system still matches the Kalpa and
Slowroll layout: the btrfs root, the boot command line, the PackageKit
backend, Discover, Baloo, and the systemd policy. The boot service repairs
only writable /etc drift. It does not remount disks, rewrite fstab, rewrite
boot entries, or change the display manager.

%prep

%build

%install
install -D -m 0755 %{SOURCE0} %{buildroot}%{_sbindir}/atropos-check
install -D -m 0644 %{SOURCE1} %{buildroot}%{_unitdir}/atropos-check.service
install -D -m 0644 %{SOURCE2} %{buildroot}%{_prefix}/lib/systemd/system-preset/81-atropos-check.preset

%pre
%service_add_pre atropos-check.service

%post
%service_add_post atropos-check.service

%preun
%service_del_preun atropos-check.service

%postun
%service_del_postun atropos-check.service

%files
%{_sbindir}/atropos-check
%{_unitdir}/atropos-check.service
%{_prefix}/lib/systemd/system-preset/81-atropos-check.preset

%changelog
