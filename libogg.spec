%define name	libogg
%define version	1.0beta4
%define release 1

Summary:	Ogg Bitstream Library
Name:		%{name}
Version:	%{version}
Release:	%{release}
Group:		Libraries/Multimedia
Copyright:	LGPL
URL:		http://www.xiph.org/
Vendor:		Xiphophorus <team@xiph.org>
Source:		ftp://ftp.xiph.org/pub/ogg/%{name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-root

%description
Libogg is a library for manipulating ogg bitstreams.  It handles
both making ogg bitstreams and getting packets from ogg bitstreams.

%package devel
Summary: Ogg Bitstream Library Development
Group: Development/Libraries
Requires: libogg = %{version}

%description devel
The libogg-devel package contains the header files and documentation
needed to develop applications with libogg.

%prep
%setup -q -n %{name}-%{version}

%build
if [ ! -f configure ]; then
  CFLAGS="$RPM_FLAGS" ./autogen.sh --prefix=/usr
else
  CFLAGS="$RPM_FLAGS" ./configure --prefix=/usr
fi
make

%install
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install

%files
%defattr(-,root,root)
%doc AUTHORS
%doc CHANGES
%doc COPYING
%doc README
/usr/lib/libogg.so.*

%files devel
%doc doc/index.html
%doc doc/framing.html
%doc doc/oggstream.html
%doc doc/white-ogg.png
%doc doc/white-xifish.png
%doc doc/stream.png
/usr/include/ogg/ogg.h
/usr/include/ogg/os_types.h
/usr/include/ogg/config_types.h
/usr/lib/libogg.a
/usr/lib/libogg.so
/usr/share/aclocal/ogg.m4

%clean 
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%changelog
* Sat Sep 02 2000 Jack Moffitt <jack@icecast.org>
- initial spec file created
