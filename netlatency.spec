Name:       netlatency
Version:    %{_version}
Release:    %{_snapshot}%{?dist}
Summary:    A network latency measurement toolset
License:    free
Source:     %{_src_package_name}.tar.gz

%description
A network latency measurement toolset.

%prep
%autosetup -n %{_src_package_name}

%build

%install
%{make_install}

%files
