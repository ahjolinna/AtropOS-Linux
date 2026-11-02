#
# spec file for package zsh-completions
#
# Extra completion functions that are not in zsh yet. Based on the
# 0.34.0 package, updated to 0.36.0. The functions go in site-functions,
# which compinit already searches. They are not executable.

Name:           zsh-completions
Version:        0.36.0
Release:        1
Summary:        Additional completion definitions for zsh
License:        MIT
Group:          System/Shells
URL:            https://github.com/zsh-users/zsh-completions
Source0:        %{name}-%{version}.tar.gz
BuildArch:      noarch
Requires:       zsh

%description
Completion definitions for commands that zsh itself does not complete
yet. compinit loads them from /usr/share/zsh/site-functions.

%prep
%autosetup

%build

%install
install -d %{buildroot}%{_datadir}/zsh/site-functions
install -pm 644 src/_* %{buildroot}%{_datadir}/zsh/site-functions/

%files
%license LICENSE
%doc README.md
# zsh owns /usr/share/zsh. Share that directory at the default mode.
%dir %{_datadir}/zsh
%dir %{_datadir}/zsh/site-functions
%{_datadir}/zsh/site-functions/_*

%changelog
