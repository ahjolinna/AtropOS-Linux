#
# spec file for package agama-products-atropos
#
# AtropOS product definition only. Do not vendor Agama in this package.
# The live image continues to come from upstream Agama.

Name:           agama-products-atropos
Version:        0.4.0
Release:        8
Summary:        AtropOS product definition for the Agama installer
# The product file and the two crown SVGs are GPL-2.0-only.
# The mark is the AtropOS crown, not the openSUSE chameleon.
License:        GPL-2.0-only
Group:          System/YaST
URL:            https://github.com/AtropOSLinux/
BuildArch:      noarch
Source0:        atropos.yaml
Source1:        AtropOS.svg
Source2:        AtropOS-dark.svg
Source3:        LOGO-NOTICE

%description
Installs the AtropOS Desktop product definition for Agama.
This package does not build or replace Agama.

%prep

%build

%install
install -D -m 0644 %{SOURCE0} %{buildroot}%{_datadir}/agama/products.d/atropos.yaml
install -D -m 0644 %{SOURCE1} %{buildroot}%{_datadir}/agama/web_ui/assets/logos/AtropOS.svg
install -D -m 0644 %{SOURCE2} %{buildroot}%{_datadir}/agama/web_ui/assets/logos/AtropOS-dark.svg
install -D -m 0644 %{SOURCE3} %{buildroot}%{_docdir}/%{name}/LOGO-NOTICE

%files
%dir %{_datadir}/agama
%dir %{_datadir}/agama/products.d
%{_datadir}/agama/products.d/atropos.yaml
# agama-web-ui owns these directories on an installed system. The OBS
# build root does not contain that package, so own them here too.
%dir %{_datadir}/agama/web_ui
%dir %{_datadir}/agama/web_ui/assets
%dir %{_datadir}/agama/web_ui/assets/logos
%{_datadir}/agama/web_ui/assets/logos/AtropOS.svg
%{_datadir}/agama/web_ui/assets/logos/AtropOS-dark.svg
%dir %{_docdir}/%{name}
%doc %{_docdir}/%{name}/LOGO-NOTICE

%changelog
