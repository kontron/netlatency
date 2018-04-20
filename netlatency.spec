Name:       netlatency
Version:    %VERSION%
Release:    %SNAPSHOT%%{?dist}
Summary:    A network latency measurement toolset
License:    free
Source:     %SRC_PACKAGE_NAME%.tar.gz
BuildRequires:  glib2-devel jansson-devel
Requires:       glib2 jansson


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
