#
# spec file for package PackageKit-backend-atropos
#
# Discover's Update action stages transactional-update dup. The reboot stays
# with the user.

Name:           PackageKit-backend-atropos
Version:        0.1.0
Release:        6
Summary:        PackageKit backend that stages AtropOS updates
License:        GPL-2.0-or-later
Group:          System/Packages
URL:            https://github.com/AtropOSLinux/
BuildRequires:  gcc
BuildRequires:  glibc-devel
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(packagekit-glib2)
BuildRequires:  systemd-rpm-macros
Source0:        pk-backend-atropos.c
Source1:        atropos-packagekit.conf
Source2:        atropos-offline-update.conf
Source3:        discoverrc
Requires:       PackageKit
Requires:       transactional-update
Requires:       zypper
Provides:       PackageKit-backend
Conflicts:      PackageKit-backend-zypp

%description
PackageKit backend used by Discover on AtropOS. Checking for updates runs
zypper dup --dry-run and reports one System updates row with that
download size. Update All runs transactional-update dup in a new
snapshot and does not reboot. Resolve reports installed packages, and
staged updates from the last check, so Discover can open. Installing or
removing individual RPM packages is refused. Flatpak updates stay on
Discover's Flatpak backend.

%prep

%build
gcc -shared -fPIC %{optflags} -Wall -Wextra \
	$(pkg-config --cflags glib-2.0 packagekit-glib2) \
	-o libpk_backend_atropos.so %{SOURCE0} \
	$(pkg-config --libs glib-2.0 packagekit-glib2)

%install
install -D -m 0755 libpk_backend_atropos.so %{buildroot}%{_libdir}/packagekit-backend/libpk_backend_atropos.so
install -D -m 0644 %{SOURCE1} %{buildroot}%{_unitdir}/packagekit.service.d/atropos.conf
install -D -m 0644 %{SOURCE2} %{buildroot}%{_unitdir}/packagekit-offline-update.service.d/atropos.conf
install -D -m 0644 %{SOURCE3} %{buildroot}%{_sysconfdir}/xdg/discoverrc

%post
%systemd_post packagekit.service
if [ -x /usr/bin/systemctl ]; then
    systemctl try-restart packagekit.service >/dev/null 2>&1 || :
fi

%postun
%systemd_postun packagekit.service
if [ "$1" -eq 0 ] && [ -x /usr/bin/systemctl ]; then
    systemctl try-restart packagekit.service >/dev/null 2>&1 || :
fi

%files
%dir %{_libdir}/packagekit-backend
%{_libdir}/packagekit-backend/libpk_backend_atropos.so
%dir %{_unitdir}/packagekit.service.d
%{_unitdir}/packagekit.service.d/atropos.conf
%dir %{_unitdir}/packagekit-offline-update.service.d
%{_unitdir}/packagekit-offline-update.service.d/atropos.conf
%config(noreplace) %{_sysconfdir}/xdg/discoverrc

%changelog
