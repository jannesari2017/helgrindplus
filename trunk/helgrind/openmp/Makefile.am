
include $(top_srcdir)/Makefile.all.am
include $(top_srcdir)/Makefile.core.am
include $(top_srcdir)/Makefile.install.am

AM_CPPFLAGS_X86_LINUX   += -DVG_LIBDIR="\"$(valdir)"\"
AM_CPPFLAGS_AMD64_LINUX += -DVG_LIBDIR="\"$(valdir)"\"
AM_CPPFLAGS_PPC32_LINUX += -DVG_LIBDIR="\"$(valdir)"\"
AM_CPPFLAGS_PPC64_LINUX += -DVG_LIBDIR="\"$(valdir)"\"
AM_CPPFLAGS_PPC32_AIX5  += -DVG_LIBDIR="\"$(valdir)"\"
AM_CPPFLAGS_PPC64_AIX5  += -DVG_LIBDIR="\"$(valdir)"\"


default.supp: $(SUPP_FILES)

noinst_LIBRARIES =
if VGP_X86_LINUX
noinst_LIBRARIES += libreplacegomp_toolpreload_x86_linux.a
endif
if VGP_AMD64_LINUX
noinst_LIBRARIES += libreplacegomp_toolpreload_amd64_linux.a
endif
if VGP_PPC32_LINUX
noinst_LIBRARIES += libreplacegomp_toolpreload_ppc32_linux.a
endif
if VGP_PPC64_LINUX
noinst_LIBRARIES += libreplacegomp_toolpreload_ppc64_linux.a
endif
if VGP_PPC32_AIX5
noinst_LIBRARIES += libreplacegomp_toolpreload_ppc32_aix5.a
endif
if VGP_PPC64_AIX5
noinst_LIBRARIES += libreplacegomp_toolpreload_ppc64_aix5.a
endif

VGPRELOAD_GOMP_SOURCES_COMMON = lock.c mutex.c \
    proc.c sem.c bar.c ptrlock.c \
    time.c affinity.c alloc.c barrier.c \
    critical.c env.c error.c iter.c \
    iter_ull.c loop.c loop_ull.c \
    ordered.c parallel.c sections.c \
    single.c task.c team.c work.c
    
libreplacegomp_toolpreload_x86_linux_a_SOURCES = $(VGPRELOAD_GOMP_SOURCES_COMMON)
libreplacegomp_toolpreload_x86_linux_a_CPPFLAGS = $(AM_CPPFLAGS_X86_LINUX)
libreplacegomp_toolpreload_x86_linux_a_CFLAGS = $(AM_CFLAGS_X86_LINUX) -fpic -fno-omit-frame-pointer

libreplacegomp_toolpreload_amd64_linux_a_SOURCES = $(VGPRELOAD_GOMP_SOURCES_COMMON)
libreplacegomp_toolpreload_amd64_linux_a_CPPFLAGS = $(AM_CPPFLAGS_AMD64_LINUX)
libreplacegomp_toolpreload_amd64_linux_a_CFLAGS = $(AM_CFLAGS_AMD64_LINUX) -fpic -fno-omit-frame-pointer

libreplacegomp_toolpreload_ppc32_linux_a_SOURCES = $(VGPRELOAD_GOMP_SOURCES_COMMON)
libreplacegomp_toolpreload_ppc32_linux_a_CPPFLAGS = $(AM_CPPFLAGS_PPC32_LINUX)
libreplacegomp_toolpreload_ppc32_linux_a_CFLAGS = $(AM_CFLAGS_PPC32_LINUX) -fpic -fno-omit-frame-pointer

libreplacegomp_toolpreload_ppc64_linux_a_SOURCES = $(VGPRELOAD_GOMP_SOURCES_COMMON)
libreplacegomp_toolpreload_ppc64_linux_a_CPPFLAGS = $(AM_CPPFLAGS_PPC64_LINUX)
libreplacegomp_toolpreload_ppc64_linux_a_CFLAGS = $(AM_CFLAGS_PPC64_LINUX) -fpic -fno-omit-frame-pointer

libreplacegomp_toolpreload_ppc32_aix5_a_SOURCES = $(VGPRELOAD_GOMP_SOURCES_COMMON)
libreplacegomp_toolpreload_ppc32_aix5_a_CPPFLAGS = $(AM_CPPFLAGS_PPC32_AIX5)
libreplacegomp_toolpreload_ppc32_aix5_a_CFLAGS = $(AM_CFLAGS_PPC32_AIX5) -fpic -fno-omit-frame-pointer
libreplacegomp_toolpreload_ppc32_aix5_a_AR = $(AR) -X32 cru

libreplacegomp_toolpreload_ppc64_aix5_a_SOURCES = $(VGPRELOAD_GOMP_SOURCES_COMMON)
libreplacegomp_toolpreload_ppc64_aix5_a_CPPFLAGS = $(AM_CPPFLAGS_PPC64_AIX5)
libreplacegomp_toolpreload_ppc64_aix5_a_CFLAGS = $(AM_CFLAGS_PPC64_AIX5) -fpic -fno-omit-frame-pointer
libreplacegomp_toolpreload_ppc64_aix5_a_AR = $(AR) -X64 cru
