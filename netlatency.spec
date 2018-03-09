Name:       netlatency
Version:    %VERSION%
Release:    %SNAPSHOT%%{?dist}
Summary:    A network latency measurement toolset
License:    free
Source:     %SRC_PACKAGE_NAME%.tar.gz

%description
A network latency measurement toolset.

%prep
%autosetup -n %SRC_PACKAGE_NAME%

%build

%install
%{make_install}

%files
