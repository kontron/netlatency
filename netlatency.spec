Name:       netlatency
Version:    %VERSION%
Release:    %SNAPSHOT%%{?dist}
Summary:    A network latency measurement toolset
License:    free
Source:     %SRC_PACKAGE_NAME%.tar.gz
BuildRequires:  glib2-devel
BuildRequires:  jansson-devel
BuildRequires:  gcc
Requires:       jansson
Requires:       glib2
Requires:       python3-matplotlib
Requires:       python3-numpy

%global debug_package %{nil}

%description
A network latency measurement toolset.

%prep
%autosetup -n %SRC_PACKAGE_NAME%

%build

%install
%{make_install}

%files
/usr/sbin/nl-rx
/usr/sbin/nl-tx
/usr/bin/nl-report
/usr/bin/nl-calc
/usr/bin/nl-trace
/usr/bin/nl-xlat-ts
# Man Pages get auto-compressed by rpm's buildroot policy scripts.
# https://fedoraproject.org/wiki/Packaging:Guidelines#Manpages 
%{_mandir}/man1/nl-rx.1*
%{_mandir}/man1/nl-tx.1*
%{_mandir}/man1/nl-report.1*
%{_mandir}/man1/nl-calc.1*
%{_mandir}/man1/nl-trace.1*
%{_mandir}/man1/nl-xlat-ts.1*
