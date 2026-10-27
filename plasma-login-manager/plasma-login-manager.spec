#
# spec file for package plasma-login-manager
#
# Copyright (c) 2025 SUSE LLC and contributors
#
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

%define kf6_version 6.27.0
%define qt6_version 6.11.2

Name:           plasma-login-manager
# set_version rewrites this line from the Plasma/6.7 tag. A macro is left
# in place, because that rewrite stops at '%'.
Version:        6.7.5
Release:        0
Summary:        QML based login manager for the Plasma Desktop
License:        GPL-2.0-or-later AND GPL-3.0-or-later
URL:            https://invent.kde.org/plasma/plasma-login-manager
Source:         %{name}-%{version}.tar.xz
Source2:        noqmlcache.conf
#BuildRequires:  opus-devel
BuildRequires:  cmake >= 3.22
BuildRequires:  desktop-file-utils
BuildRequires:  kf6-extra-cmake-modules
BuildRequires:  pam-devel
BuildRequires:  shadow
BuildRequires:  systemd
BuildRequires:  systemd-rpm-macros
BuildRequires:  sysuser-tools

BuildRequires:  cmake(KF6Config) >= %{kf6_version}
BuildRequires:  cmake(KF6Package) >= %{kf6_version}
BuildRequires:  cmake(KF6WindowSystem) >= %{kf6_version}
BuildRequires:  cmake(KF6I18n) >= %{kf6_version}
BuildRequires:  cmake(KF6DBusAddons) >= %{kf6_version}
BuildRequires:  cmake(KF6KCMUtils) >= %{kf6_version}
BuildRequires:  cmake(KF6Auth) >= %{kf6_version}
BuildRequires:  cmake(KF6KIO) >= %{kf6_version}
BuildRequires:  cmake(KF6CoreAddons) >= %{kf6_version}
BuildRequires:  cmake(KF6Screen) >= %{version}
BuildRequires:  cmake(LayerShellQt)
BuildRequires:  cmake(LibKWorkspace) >= %{version}
BuildRequires:  cmake(LibKLookAndFeel) >= %{version}
BuildRequires:  cmake(PlasmaQuick)
BuildRequires:  cmake(Qt6Core) >= %{qt6_version}
BuildRequires:  cmake(Qt6DBus) >= %{qt6_version}
BuildRequires:  cmake(Qt6Gui) >= %{qt6_version}
BuildRequires:  cmake(Qt6LinguistTools) >= %{qt6_version}
BuildRequires:  cmake(Qt6Qml) >= %{qt6_version}
BuildRequires:  cmake(Qt6Quick) >= %{qt6_version}
BuildRequires:  cmake(Qt6ShaderTools) >= %{qt6_version}
BuildRequires:  cmake(Qt6Test) >= %{qt6_version}
BuildRequires:  cmake(Qt6QuickTest) >= %{qt6_version}

BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  pkgconfig(systemd)
BuildRequires:  pkgconfig(xau)
BuildRequires:  pkgconfig(xcb)
BuildRequires:  pkgconfig(xcb-xkb)

%systemd_requires
%sysusers_requires
Requires:       kf6-kauth
Requires:       kf6-kimageformats
Requires:       kwin6
Requires:       (%{name}-kcm if systemsettings6)


%description
Plasma Login provides a display manager for KDE Plasma
and with an new frontend providing a greeter,
wallpaper plugin integration and a System Settings module (KCM).

%package kcm
Summary:        KDE KCM for %{name}
Requires:       %{name} = %{version}-%{release}

%description kcm
KCM for plasmalogin, for systemsettings integration

%prep
%autosetup -p1 -n %{name}-%{version}

%build
LOGIN_DEFS_PATH="%{_sysconfdir}/login.defs"
[ -e "$LOGIN_DEFS_PATH" ] || LOGIN_DEFS_PATH="%{_distconfdir}/login.defs"

%cmake_kf6 \
  -DPAM_OS_CONFIGURATION:STRING="suse" \
  -DLOGIN_DEFS_PATH:PATH="${LOGIN_DEFS_PATH}" \
%kf6_build

%install
%kf6_install
%sysusers_generate_pre %{buildroot}%{_sysusersdir}/plasmalogin.conf plasmalogin plasmalogin.conf

%find_lang plasma_login
%find_lang kcm_plasmalogin

install -Dpm 644 %{SOURCE2} %{buildroot}%{_unitdir}/plasmalogin.service.d/noqmlcache.conf

# De-conflict the dbus file
mv %{buildroot}%{_datadir}/dbus-1/system.d/org.freedesktop.DisplayManager{,-plasmalogin}.conf

# Make PAM services compatible with older systems
%if 0%{?suse_version} < 1550
    mkdir -p %{buildroot}%{_sysconfdir}
    mv %{buildroot}%{_prefix}/lib/pam.d %{buildroot}%{_sysconfdir}/pam.d
    sed -i'' '/postlogin-/d' %{buildroot}%{_sysconfdir}/pam.d/*
%endif

%check
desktop-file-validate %{buildroot}/%{_datadir}/applications/kcm_plasmalogin.desktop

%pre -f plasmalogin.pre
%systemd_pre plasmalogin.service

%post
%systemd_post plasmalogin.service
%systemd_user_post plasma-login.service plasma-login-kwin_wayland.service plasma-login-wayland.target plasma-wallpaper.service
%tmpfiles_create %{_tmpfilesdir}/plasmalogin.conf

%preun
%systemd_preun plasmalogin.service
%systemd_user_preun plasma-login.service plasma-login-kwin_wayland.service plasma-login-wayland.target plasma-wallpaper.service

%postun
%systemd_postun plasmalogin.service
%systemd_user_postun plasma-login.service plasma-login-kwin_wayland.service plasma-login-wayland.target plasma-wallpaper.service

%files -f plasma_login.lang
%license LICENSES/*
%doc README.md
%dir %{_datadir}/plasmalogin
%{?_pam_vendordir}%{!?_pam_vendordir:%{_sysconfdir}/pam.d}/plasmalogin*
%{_kf6_dbuspolicydir}/org.freedesktop.DisplayManager-plasmalogin.conf
%{_kf6_bindir}/plasmalogin
%{_kf6_bindir}/startplasma-login-wayland
%{_kf6_bindir}/plasma-login-wallpaper
%{_libexecdir}/plasmalogin-helper
%{_libexecdir}/plasmalogin-helper-start-x11user
%{_libexecdir}/plasma-login-greeter
%{_sysusersdir}/plasmalogin.conf
%{_tmpfilesdir}/plasmalogin.conf
%{_unitdir}/plasmalogin.service
%{_unitdir}/plasmalogin.service.d/
%{_userunitdir}/plasma-login.service
%{_userunitdir}/plasma-login-kwin_wayland.service
%{_userunitdir}/plasma-login-wayland.target
%{_userunitdir}/plasma-wallpaper.service
%{_datadir}/plasmalogin/scripts/

%files kcm -f kcm_plasmalogin.lang
%{_kf6_libexecdir}/kauth/kcmplasmalogin_authhelper
%{_kf6_pluginsdir}/plasma/kcms/systemsettings/kcm_plasmalogin.so
%{_kf6_applicationsdir}/kcm_plasmalogin.desktop
%{_kf6_sharedir}/dbus-1/system-services/org.kde.kcontrol.kcmplasmalogin.service
%{_kf6_dbuspolicydir}/org.kde.kcontrol.kcmplasmalogin.conf
%{_datadir}/polkit-1/actions/org.kde.kcontrol.kcmplasmalogin.policy

%changelog
