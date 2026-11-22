#
# spec file for package plasma6-applet-atropos-assistant
#
# A Plasma 6 panel widget that chats with Inkling 2 Mini on this computer,
# the same widget as an application window (plasmawindowed), and an
# "ai <question>" KRunner runner. The model and llama.cpp are not packaged:
# the widget downloads them into the user's home through
# atropos-assistant-setup, and the atropos-assistant user service runs
# llama-server. atropos-assistant-service, a D-Bus-activated Python service
# on the session bus, keeps the chats and serves the widget and KRunner.
# The widget id is org.atropos.assistant.

Name:           plasma6-applet-atropos-assistant
Version:        0.1
Release:        1
Summary:        Local AI assistant for Plasma, running Inkling 2 Mini
License:        GPL-2.0-or-later
Group:          System/GUI/KDE
URL:            https://github.com/ahjolinna/AtropOS-Linux
Source0:        %{name}-%{version}.tar.xz
BuildArch:      noarch
BuildRequires:  systemd-rpm-macros
# plasmawindowed, the window, and org.kde.plasma.workspace.dbus come from here.
Requires:       plasma6-workspace
# Slowroll has no qt6qml() providers. The QML modules ship in these packages.
Requires:       kf6-kcmutils-imports
Requires:       kf6-kcoreaddons-imports
Requires:       kf6-kirigami-imports
Requires:       kf6-knotifications-imports
Requires:       libplasma6-components
Requires:       qt6-declarative-imports
# atropos-assistant-setup, -server and -service
Requires:       bash
Requires:       coreutils
Requires:       curl
Requires:       python3-base
Requires:       python3-dbus-python
Requires:       python3-gobject
Requires:       systemd
Requires:       tar
# The Vulkan llama.cpp build loads libvulkan. nvidia-smi or amdgpu sysfs
# sizes the micro-batch; without either it assumes a small GPU.
Recommends:     libvulkan1

%description
Chat with Inkling 2 Mini, Thinking Machines Lab's open-weight model,
running on this computer: from a panel or system tray widget, from the Assistant
window, or from KRunner with "ai <question>". Answers stream in as
Markdown and the model's thinking can be shown. Images and text files
can be attached. Chats are saved, searchable, and export to Markdown,
text or JSON.

The widget downloads llama.cpp and the model (7.8 to 22.9 GB) into
~/.local/share/atropos-assistant and starts the atropos-assistant user
service, which runs llama-server on 127.0.0.1:18080. It can also use
another OpenAI-compatible server, such as LM Studio or vLLM.

%prep
%setup -q

%build

%install
dest=%{buildroot}%{_datadir}/plasma/plasmoids/org.atropos.assistant
install -d "$dest"
cp -a package/. "$dest/"
find "$dest" -type d -exec chmod 0755 {} +
find "$dest" -type f -exec chmod 0644 {} +
install -D -m 0755 server/atropos-assistant-server %{buildroot}%{_bindir}/atropos-assistant-server
install -D -m 0755 server/atropos-assistant-setup %{buildroot}%{_bindir}/atropos-assistant-setup
install -D -m 0755 server/atropos-assistant-service %{buildroot}%{_bindir}/atropos-assistant-service
install -D -m 0644 server/atropos-assistant.service %{buildroot}%{_userunitdir}/atropos-assistant.service
install -D -m 0644 server/atropos-assistant-bus.service %{buildroot}%{_userunitdir}/atropos-assistant-bus.service
install -D -m 0644 server/org.atropos.Assistant.service %{buildroot}%{_datadir}/dbus-1/services/org.atropos.Assistant.service
install -D -m 0644 desktop/org.atropos.assistant.desktop %{buildroot}%{_datadir}/applications/org.atropos.assistant.desktop
install -D -m 0644 desktop/krunner-org.atropos.assistant.desktop %{buildroot}%{_datadir}/krunner/dbusplugins/org.atropos.assistant.desktop
install -D -m 0644 desktop/atropos-assistant.notifyrc %{buildroot}%{_datadir}/knotifications6/atropos-assistant.notifyrc

%files
%defattr(-,root,root)
%doc README.md
%license COPYING
%{_bindir}/atropos-assistant-server
%{_bindir}/atropos-assistant-service
%{_bindir}/atropos-assistant-setup
%{_userunitdir}/atropos-assistant.service
%{_userunitdir}/atropos-assistant-bus.service
%{_datadir}/applications/org.atropos.assistant.desktop
# plasma-workspace, dbus-1, kf6-krunner and kf6-knotifications own these
# directories on an installed system. The OBS build root does not have
# them, so this package owns them too.
%dir %{_datadir}/plasma
%dir %{_datadir}/plasma/plasmoids
%{_datadir}/plasma/plasmoids/org.atropos.assistant
%dir %{_datadir}/dbus-1
%dir %{_datadir}/dbus-1/services
%{_datadir}/dbus-1/services/org.atropos.Assistant.service
%dir %{_datadir}/krunner
%dir %{_datadir}/krunner/dbusplugins
%{_datadir}/krunner/dbusplugins/org.atropos.assistant.desktop
%dir %{_datadir}/knotifications6
%{_datadir}/knotifications6/atropos-assistant.notifyrc

%changelog
