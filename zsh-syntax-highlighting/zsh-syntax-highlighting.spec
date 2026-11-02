#
# spec file for package zsh-syntax-highlighting
#
# Fish-like command highlighting. 0.8.0 is still the latest release.
# Later master commits are not tagged.

Name:           zsh-syntax-highlighting
Version:        0.8.0
Release:        1
Summary:        Fish-like syntax highlighting for zsh
License:        BSD-3-Clause
Group:          System/Shells
URL:            https://github.com/zsh-users/zsh-syntax-highlighting
Source0:        %{name}-%{version}.tar.gz
BuildArch:      noarch
Requires:       zsh >= 4.3.11

%description
Highlights a command while it is typed, so a syntax error is visible
before it runs.

%prep
%autosetup

%build

%install
install -d %{buildroot}%{_datadir}/%{name}
install -pm 644 zsh-syntax-highlighting.zsh .version .revision-hash \
    %{buildroot}%{_datadir}/%{name}/
for dir in highlighters/*/; do
    name=${dir%/}
    name=${name##*/}
    install -d %{buildroot}%{_datadir}/%{name}/highlighters/"$name"
    install -pm 644 "$dir"*.zsh \
        %{buildroot}%{_datadir}/%{name}/highlighters/"$name"/
done

%files
%license COPYING.md
%doc README.md changelog.md
%{_datadir}/%{name}

%changelog
