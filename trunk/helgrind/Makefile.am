include $(top_srcdir)/Makefile.tool.am
include $(top_srcdir)/helgrind/openmp/Makefile.tool.am

SUBDIRS = openmp . docs tests

noinst_PROGRAMS = 
if VGP_X86_LINUX
noinst_PROGRAMS += helgrind-x86-linux vgpreload_helgrind-x86-linux.so
endif
if VGP_AMD64_LINUX
noinst_PROGRAMS += helgrind-amd64-linux vgpreload_helgrind-amd64-linux.so
endif
if VGP_PPC32_LINUX
noinst_PROGRAMS += helgrind-ppc32-linux vgpreload_helgrind-ppc32-linux.so
endif
if VGP_PPC64_LINUX
noinst_PROGRAMS += helgrind-ppc64-linux vgpreload_helgrind-ppc64-linux.so
endif
if VGP_PPC32_AIX5
noinst_PROGRAMS += helgrind-ppc32-aix5 vgpreload_helgrind-ppc32-aix5.so
endif
if VGP_PPC64_AIX5
noinst_PROGRAMS += helgrind-ppc64-aix5 vgpreload_helgrind-ppc64-aix5.so
endif

## msm short

if VGP_X86_LINUX
noinst_PROGRAMS += helgrind-ukas-x86-linux vgpreload_helgrind-ukas-x86-linux.so
endif
if VGP_AMD64_LINUX
noinst_PROGRAMS += helgrind-ukas-amd64-linux vgpreload_helgrind-ukas-amd64-linux.so
endif
if VGP_PPC32_LINUX
noinst_PROGRAMS += helgrind-ukas-ppc32-linux vgpreload_helgrind-ukas-ppc32-linux.so
endif
if VGP_PPC64_LINUX
noinst_PROGRAMS += helgrind-ukas-ppc64-linux vgpreload_helgrind-ukas-ppc64-linux.so
endif
if VGP_PPC32_AIX5
noinst_PROGRAMS += helgrind-ukas-ppc32-aix5 vgpreload_helgrind-ukas-ppc32-aix5.so
endif
if VGP_PPC64_AIX5
noinst_PROGRAMS += helgrind-ukas-ppc64-aix5 vgpreload_helgrind-ukas-ppc64-aix5.so
endif

## msm long

if VGP_X86_LINUX
noinst_PROGRAMS += helgrind-ukal-x86-linux vgpreload_helgrind-ukal-x86-linux.so
endif
if VGP_AMD64_LINUX
noinst_PROGRAMS += helgrind-ukal-amd64-linux vgpreload_helgrind-ukal-amd64-linux.so
endif
if VGP_PPC32_LINUX
noinst_PROGRAMS += helgrind-ukal-ppc32-linux vgpreload_helgrind-ukal-ppc32-linux.so
endif
if VGP_PPC64_LINUX
noinst_PROGRAMS += helgrind-ukal-ppc64-linux vgpreload_helgrind-ukal-ppc64-linux.so
endif
if VGP_PPC32_AIX5
noinst_PROGRAMS += helgrind-ukal-ppc32-aix5 vgpreload_helgrind-ukal-ppc32-aix5.so
endif
if VGP_PPC64_AIX5
noinst_PROGRAMS += helgrind-ukal-ppc64-aix5 vgpreload_helgrind-ukal-ppc64-aix5.so
endif

## msm short no pthread interception

if VGP_X86_LINUX
noinst_PROGRAMS += helgrind-ukas-nolib-x86-linux vgpreload_helgrind-ukas-nolib-x86-linux.so
endif
if VGP_AMD64_LINUX
noinst_PROGRAMS += helgrind-ukas-nolib-amd64-linux vgpreload_helgrind-ukas-nolib-amd64-linux.so
endif
if VGP_PPC32_LINUX
noinst_PROGRAMS += helgrind-ukas-nolib-ppc32-linux vgpreload_helgrind-ukas-nolib-ppc32-linux.so
endif
if VGP_PPC64_LINUX
noinst_PROGRAMS += helgrind-ukas-nolib-ppc64-linux vgpreload_helgrind-ukas-nolib-ppc64-linux.so
endif
if VGP_PPC32_AIX5
noinst_PROGRAMS += helgrind-ukas-nolib-ppc32-aix5 vgpreload_helgrind-ukas-nolib-ppc32-aix5.so
endif
if VGP_PPC64_AIX5
noinst_PROGRAMS += helgrind-ukas-nolib-ppc64-aix5 vgpreload_helgrind-ukas-nolib-ppc64-aix5.so
endif

## msm long no pthread interception

if VGP_X86_LINUX
noinst_PROGRAMS += helgrind-ukal-nolib-x86-linux vgpreload_helgrind-ukal-nolib-x86-linux.so
endif
if VGP_AMD64_LINUX
noinst_PROGRAMS += helgrind-ukal-nolib-amd64-linux vgpreload_helgrind-ukal-nolib-amd64-linux.so
endif
if VGP_PPC32_LINUX
noinst_PROGRAMS += helgrind-ukal-nolib-ppc32-linux vgpreload_helgrind-ukal-nolib-ppc32-linux.so
endif
if VGP_PPC64_LINUX
noinst_PROGRAMS += helgrind-ukal-nolib-ppc64-linux vgpreload_helgrind-ukal-nolib-ppc64-linux.so
endif
if VGP_PPC32_AIX5
noinst_PROGRAMS += helgrind-ukal-nolib-ppc32-aix5 vgpreload_helgrind-ukal-nolib-ppc32-aix5.so
endif
if VGP_PPC64_AIX5
noinst_PROGRAMS += helgrind-ukal-nolib-ppc64-aix5 vgpreload_helgrind-ukal-nolib-ppc64-aix5.so
endif

##

VGPRELOAD_HELGRIND_SOURCES_COMMON = hg_intercepts.c hg_annotations.c

if HG_ENABLE_OMP
VGPRELOAD_HELGRIND_SOURCES_COMMON += hg_openmp.c
endif

HELGRIND_SOURCES_COMMON = \
	hg_basics.c hg_lock_n_thread.c hg_wordset.c \
	hg_lsd.c hg_loops.c hg_dependency.c hg_interval.c hg_logging.c \
	hg_errors.c hg_main.c libhb_core.c

## MSM original

vgpreload_helgrind_x86_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_x86_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
vgpreload_helgrind_x86_linux_so_CFLAGS       = $(AM_CFLAGS_X86_LINUX) $(AM_CFLAGS_PIC)
vgpreload_helgrind_x86_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_X86_LINUX) $(LIBREPLACEGOMP_X86_LINUX)
vgpreload_helgrind_x86_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_X86_LINUX) \
	$(LIBREPLACEMALLOC_LDFLAGS_X86_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_X86_LINUX)

vgpreload_helgrind_amd64_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_amd64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
vgpreload_helgrind_amd64_linux_so_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX) $(AM_CFLAGS_PIC)
vgpreload_helgrind_amd64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_AMD64_LINUX) $(LIBREPLACEGOMP_AMD64_LINUX)
vgpreload_helgrind_amd64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_AMD64_LINUX) \
	$(LIBREPLACEMALLOC_LDFLAGS_AMD64_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_AMD64_LINUX)

vgpreload_helgrind_ppc32_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ppc32_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
vgpreload_helgrind_ppc32_linux_so_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ppc32_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_LINUX) $(LIBREPLACEGOMP_PPC32_LINUX)
vgpreload_helgrind_ppc32_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_LINUX) \
	$(LIBREPLACEMALLOC_LDFLAGS_PPC32_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC32_LINUX)

vgpreload_helgrind_ppc64_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ppc64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
vgpreload_helgrind_ppc64_linux_so_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ppc64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_LINUX) $(LIBREPLACEGOMP_PPC64_LINUX)
vgpreload_helgrind_ppc64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_LINUX) \
	$(LIBREPLACEMALLOC_LDFLAGS_PPC64_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC64_LINUX)

vgpreload_helgrind_ppc32_aix5_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ppc32_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
vgpreload_helgrind_ppc32_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ppc32_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_AIX5) $(LIBREPLACEGOMP_PPC32_AIX5)
vgpreload_helgrind_ppc32_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_AIX5) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC32_AIX5) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC32_AIX5)

vgpreload_helgrind_ppc64_aix5_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ppc64_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
vgpreload_helgrind_ppc64_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ppc64_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_AIX5) $(LIBREPLACEGOMP_PPC64_AIX5)
vgpreload_helgrind_ppc64_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_AIX5) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC64_AIX5) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC64_AIX5)

helgrind_x86_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_x86_linux_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
helgrind_x86_linux_CFLAGS       = $(AM_CFLAGS_X86_LINUX) -O2
helgrind_x86_linux_DEPENDENCIES = $(COREGRIND_LIBS_X86_LINUX)
helgrind_x86_linux_LDADD        = $(TOOL_LDADD_X86_LINUX)
helgrind_x86_linux_LDFLAGS      = $(TOOL_LDFLAGS_X86_LINUX)

helgrind_amd64_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_amd64_linux_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
helgrind_amd64_linux_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX) -O2
helgrind_amd64_linux_DEPENDENCIES = $(COREGRIND_LIBS_AMD64_LINUX)
helgrind_amd64_linux_LDADD        = $(TOOL_LDADD_AMD64_LINUX)
helgrind_amd64_linux_LDFLAGS      = $(TOOL_LDFLAGS_AMD64_LINUX)

helgrind_ppc32_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ppc32_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
helgrind_ppc32_linux_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX) -O2
helgrind_ppc32_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_LINUX)
helgrind_ppc32_linux_LDADD        = $(TOOL_LDADD_PPC32_LINUX)
helgrind_ppc32_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_LINUX)

helgrind_ppc64_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ppc64_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
helgrind_ppc64_linux_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX) -O2
helgrind_ppc64_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_LINUX)
helgrind_ppc64_linux_LDADD        = $(TOOL_LDADD_PPC64_LINUX)
helgrind_ppc64_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_LINUX)

helgrind_ppc32_aix5_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ppc32_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
helgrind_ppc32_aix5_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5) -O2
helgrind_ppc32_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_AIX5)
helgrind_ppc32_aix5_LDADD        = $(TOOL_LDADD_PPC32_AIX5)
helgrind_ppc32_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_AIX5)

helgrind_ppc64_aix5_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ppc64_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
helgrind_ppc64_aix5_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5) -O2
helgrind_ppc64_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_AIX5)
helgrind_ppc64_aix5_LDADD        = $(TOOL_LDADD_PPC64_AIX5)
helgrind_ppc64_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_AIX5)

##

## MSM short

HELGRIND_UKAS_CFLAGS = -DLIBHB_MSM_UKAS=1

vgpreload_helgrind_ukas_x86_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukas_x86_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
vgpreload_helgrind_ukas_x86_linux_so_CFLAGS       = $(AM_CFLAGS_X86_LINUX) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ukas_x86_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_X86_LINUX) $(LIBREPLACEGOMP_X86_LINUX)
vgpreload_helgrind_ukas_x86_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_X86_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_X86_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_X86_LINUX)

vgpreload_helgrind_ukas_amd64_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukas_amd64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
vgpreload_helgrind_ukas_amd64_linux_so_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ukas_amd64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_AMD64_LINUX) $(LIBREPLACEGOMP_AMD64_LINUX)
vgpreload_helgrind_ukas_amd64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_AMD64_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_AMD64_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_AMD64_LINUX)

vgpreload_helgrind_ukas_ppc32_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukas_ppc32_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
vgpreload_helgrind_ukas_ppc32_linux_so_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ukas_ppc32_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_LINUX) $(LIBREPLACEGOMP_PPC32_LINUX)
vgpreload_helgrind_ukas_ppc32_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC32_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC32_LINUX)

vgpreload_helgrind_ukas_ppc64_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukas_ppc64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
vgpreload_helgrind_ukas_ppc64_linux_so_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ukas_ppc64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_LINUX) $(LIBREPLACEGOMP_PPC64_LINUX)
vgpreload_helgrind_ukas_ppc64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC64_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC64_LINUX)

vgpreload_helgrind_ukas_ppc32_aix5_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukas_ppc32_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
vgpreload_helgrind_ukas_ppc32_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ukas_ppc32_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_AIX5) $(LIBREPLACEGOMP_PPC32_AIX5)
vgpreload_helgrind_ukas_ppc32_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_AIX5) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC32_AIX5) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC32_AIX5)

vgpreload_helgrind_ukas_ppc64_aix5_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukas_ppc64_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
vgpreload_helgrind_ukas_ppc64_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ukas_ppc64_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_AIX5) $(LIBREPLACEGOMP_PPC64_AIX5)
vgpreload_helgrind_ukas_ppc64_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_AIX5) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC64_AIX5) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC64_AIX5)

helgrind_ukas_x86_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukas_x86_linux_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
helgrind_ukas_x86_linux_CFLAGS       = $(AM_CFLAGS_X86_LINUX) $(HELGRIND_UKAS_CFLAGS) -O2
helgrind_ukas_x86_linux_DEPENDENCIES = $(COREGRIND_LIBS_X86_LINUX)
helgrind_ukas_x86_linux_LDADD        = $(TOOL_LDADD_X86_LINUX)
helgrind_ukas_x86_linux_LDFLAGS      = $(TOOL_LDFLAGS_X86_LINUX)

helgrind_ukas_amd64_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukas_amd64_linux_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
helgrind_ukas_amd64_linux_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX) $(HELGRIND_UKAS_CFLAGS) -O2
helgrind_ukas_amd64_linux_DEPENDENCIES = $(COREGRIND_LIBS_AMD64_LINUX)
helgrind_ukas_amd64_linux_LDADD        = $(TOOL_LDADD_AMD64_LINUX)
helgrind_ukas_amd64_linux_LDFLAGS      = $(TOOL_LDFLAGS_AMD64_LINUX)

helgrind_ukas_ppc32_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukas_ppc32_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
helgrind_ukas_ppc32_linux_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX) $(HELGRIND_UKAS_CFLAGS) -O2
helgrind_ukas_ppc32_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_LINUX)
helgrind_ukas_ppc32_linux_LDADD        = $(TOOL_LDADD_PPC32_LINUX)
helgrind_ukas_ppc32_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_LINUX)

helgrind_ukas_ppc64_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukas_ppc64_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
helgrind_ukas_ppc64_linux_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX) $(HELGRIND_UKAS_CFLAGS) -O2
helgrind_ukas_ppc64_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_LINUX)
helgrind_ukas_ppc64_linux_LDADD        = $(TOOL_LDADD_PPC64_LINUX)
helgrind_ukas_ppc64_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_LINUX)

helgrind_ukas_ppc32_aix5_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukas_ppc32_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
helgrind_ukas_ppc32_aix5_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5) $(HELGRIND_UKAS_CFLAGS) -O2
helgrind_ukas_ppc32_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_AIX5)
helgrind_ukas_ppc32_aix5_LDADD        = $(TOOL_LDADD_PPC32_AIX5)
helgrind_ukas_ppc32_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_AIX5)

helgrind_ukas_ppc64_aix5_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukas_ppc64_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
helgrind_ukas_ppc64_aix5_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5) $(HELGRIND_UKAS_CFLAGS) -O2
helgrind_ukas_ppc64_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_AIX5)
helgrind_ukas_ppc64_aix5_LDADD        = $(TOOL_LDADD_PPC64_AIX5)
helgrind_ukas_ppc64_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_AIX5)

##

## MSM long

HELGRIND_UKAL_CFLAGS = -DLIBHB_MSM_UKAL=1

vgpreload_helgrind_ukal_x86_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukal_x86_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
vgpreload_helgrind_ukal_x86_linux_so_CFLAGS       = $(AM_CFLAGS_X86_LINUX) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ukal_x86_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_X86_LINUX) $(LIBREPLACEGOMP_X86_LINUX)
vgpreload_helgrind_ukal_x86_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_X86_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_X86_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_X86_LINUX)

vgpreload_helgrind_ukal_amd64_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukal_amd64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
vgpreload_helgrind_ukal_amd64_linux_so_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ukal_amd64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_AMD64_LINUX) $(LIBREPLACEGOMP_AMD64_LINUX)
vgpreload_helgrind_ukal_amd64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_AMD64_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_AMD64_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_AMD64_LINUX)

vgpreload_helgrind_ukal_ppc32_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukal_ppc32_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
vgpreload_helgrind_ukal_ppc32_linux_so_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ukal_ppc32_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_LINUX) $(LIBREPLACEGOMP_PPC32_LINUX)
vgpreload_helgrind_ukal_ppc32_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC32_LINUX) \
   $(LIBREPLACEGOMP_LDFLAGS_PPC32_LINUX)

vgpreload_helgrind_ukal_ppc64_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukal_ppc64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
vgpreload_helgrind_ukal_ppc64_linux_so_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ukal_ppc64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_LINUX) $(LIBREPLACEGOMP_PPC64_LINUX)
vgpreload_helgrind_ukal_ppc64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC64_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC64_LINUX)

vgpreload_helgrind_ukal_ppc32_aix5_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukal_ppc32_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
vgpreload_helgrind_ukal_ppc32_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ukal_ppc32_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_AIX5) $(LIBREPLACEGOMP_PPC32_AIX5)
vgpreload_helgrind_ukal_ppc32_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_AIX5) \
   $(LIBREPLACEGOMP_LDFLAGS_PPC32_AIX5) \
	$(LIBREPLACEMALLOC_LDFLAGS_PPC32_AIX5)

vgpreload_helgrind_ukal_ppc64_aix5_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukal_ppc64_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
vgpreload_helgrind_ukal_ppc64_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5) $(AM_CFLAGS_PIC)
vgpreload_helgrind_ukal_ppc64_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_AIX5) $(LIBREPLACEGOMP_PPC64_AIX5)
vgpreload_helgrind_ukal_ppc64_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_AIX5) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC64_AIX5) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC64_AIX5)

helgrind_ukal_x86_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukal_x86_linux_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
helgrind_ukal_x86_linux_CFLAGS       = $(AM_CFLAGS_X86_LINUX) $(HELGRIND_UKAL_CFLAGS) -O2
helgrind_ukal_x86_linux_DEPENDENCIES = $(COREGRIND_LIBS_X86_LINUX)
helgrind_ukal_x86_linux_LDADD        = $(TOOL_LDADD_X86_LINUX)
helgrind_ukal_x86_linux_LDFLAGS      = $(TOOL_LDFLAGS_X86_LINUX)

helgrind_ukal_amd64_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukal_amd64_linux_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
helgrind_ukal_amd64_linux_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX) $(HELGRIND_UKAL_CFLAGS) -O2
helgrind_ukal_amd64_linux_DEPENDENCIES = $(COREGRIND_LIBS_AMD64_LINUX)
helgrind_ukal_amd64_linux_LDADD        = $(TOOL_LDADD_AMD64_LINUX)
helgrind_ukal_amd64_linux_LDFLAGS      = $(TOOL_LDFLAGS_AMD64_LINUX)

helgrind_ukal_ppc32_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukal_ppc32_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
helgrind_ukal_ppc32_linux_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX) $(HELGRIND_UKAL_CFLAGS) -O2
helgrind_ukal_ppc32_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_LINUX)
helgrind_ukal_ppc32_linux_LDADD        = $(TOOL_LDADD_PPC32_LINUX)
helgrind_ukal_ppc32_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_LINUX)

helgrind_ukal_ppc64_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukal_ppc64_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
helgrind_ukal_ppc64_linux_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX) $(HELGRIND_UKAL_CFLAGS) -O2
helgrind_ukal_ppc64_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_LINUX)
helgrind_ukal_ppc64_linux_LDADD        = $(TOOL_LDADD_PPC64_LINUX)
helgrind_ukal_ppc64_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_LINUX)

helgrind_ukal_ppc32_aix5_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukal_ppc32_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
helgrind_ukal_ppc32_aix5_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5) $(HELGRIND_UKAL_CFLAGS) -O2
helgrind_ukal_ppc32_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_AIX5)
helgrind_ukal_ppc32_aix5_LDADD        = $(TOOL_LDADD_PPC32_AIX5)
helgrind_ukal_ppc32_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_AIX5)

helgrind_ukal_ppc64_aix5_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukal_ppc64_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
helgrind_ukal_ppc64_aix5_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5) $(HELGRIND_UKAL_CFLAGS) -O2
helgrind_ukal_ppc64_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_AIX5)
helgrind_ukal_ppc64_aix5_LDADD        = $(TOOL_LDADD_PPC64_AIX5)
helgrind_ukal_ppc64_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_AIX5)

##

LIBPRELOAD_NOLIB_CFLAGS = -DDEACTIVATE_PTHREAD_INTERCEPTION=1

## MSM short no pthread interception

vgpreload_helgrind_ukas_nolib_x86_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukas_nolib_x86_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
vgpreload_helgrind_ukas_nolib_x86_linux_so_CFLAGS       = $(AM_CFLAGS_X86_LINUX) $(AM_CFLAGS_PIC) $(LIBPRELOAD_NOLIB_CFLAGS)
vgpreload_helgrind_ukas_nolib_x86_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_X86_LINUX) $(LIBREPLACEGOMP_X86_LINUX)
vgpreload_helgrind_ukas_nolib_x86_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_X86_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_X86_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_X86_LINUX)

vgpreload_helgrind_ukas_nolib_amd64_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukas_nolib_amd64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
vgpreload_helgrind_ukas_nolib_amd64_linux_so_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX) $(AM_CFLAGS_PIC) $(LIBPRELOAD_NOLIB_CFLAGS)
vgpreload_helgrind_ukas_nolib_amd64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_AMD64_LINUX) $(LIBREPLACEGOMP_AMD64_LINUX)
vgpreload_helgrind_ukas_nolib_amd64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_AMD64_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_AMD64_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_AMD64_LINUX)

vgpreload_helgrind_ukas_nolib_ppc32_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukas_nolib_ppc32_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
vgpreload_helgrind_ukas_nolib_ppc32_linux_so_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX) $(AM_CFLAGS_PIC) $(LIBPRELOAD_NOLIB_CFLAGS)
vgpreload_helgrind_ukas_nolib_ppc32_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_LINUX) $(LIBREPLACEGOMP_PPC32_LINUX)
vgpreload_helgrind_ukas_nolib_ppc32_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC32_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC32_LINUX)

vgpreload_helgrind_ukas_nolib_ppc64_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukas_nolib_ppc64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
vgpreload_helgrind_ukas_nolib_ppc64_linux_so_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX) $(AM_CFLAGS_PIC) $(LIBPRELOAD_NOLIB_CFLAGS)
vgpreload_helgrind_ukas_nolib_ppc64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_LINUX) $(LIBREPLACEGOMP_PPC64_LINUX)
vgpreload_helgrind_ukas_nolib_ppc64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC64_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC64_LINUX)

vgpreload_helgrind_ukas_nolib_ppc32_aix5_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukas_nolib_ppc32_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
vgpreload_helgrind_ukas_nolib_ppc32_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5) $(AM_CFLAGS_PIC) $(LIBPRELOAD_NOLIB_CFLAGS)
vgpreload_helgrind_ukas_nolib_ppc32_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_AIX5) $(LIBREPLACEGOMP_PPC32_AIX5)
vgpreload_helgrind_ukas_nolib_ppc32_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_AIX5) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC32_AIX5) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC32_AIX5)

vgpreload_helgrind_ukas_nolib_ppc64_aix5_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukas_nolib_ppc64_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
vgpreload_helgrind_ukas_nolib_ppc64_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5) $(AM_CFLAGS_PIC) $(LIBPRELOAD_NOLIB_CFLAGS)
vgpreload_helgrind_ukas_nolib_ppc64_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_AIX5) $(LIBREPLACEGOMP_PPC64_AIX5)
vgpreload_helgrind_ukas_nolib_ppc64_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_AIX5) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC64_AIX5) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC64_AIX5)

helgrind_ukas_nolib_x86_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukas_nolib_x86_linux_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
helgrind_ukas_nolib_x86_linux_CFLAGS       = $(AM_CFLAGS_X86_LINUX) $(HELGRIND_UKAS_CFLAGS) -O2
helgrind_ukas_nolib_x86_linux_DEPENDENCIES = $(COREGRIND_LIBS_X86_LINUX)
helgrind_ukas_nolib_x86_linux_LDADD        = $(TOOL_LDADD_X86_LINUX)
helgrind_ukas_nolib_x86_linux_LDFLAGS      = $(TOOL_LDFLAGS_X86_LINUX)

helgrind_ukas_nolib_amd64_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukas_nolib_amd64_linux_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
helgrind_ukas_nolib_amd64_linux_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX) $(HELGRIND_UKAS_CFLAGS) -O2
helgrind_ukas_nolib_amd64_linux_DEPENDENCIES = $(COREGRIND_LIBS_AMD64_LINUX)
helgrind_ukas_nolib_amd64_linux_LDADD        = $(TOOL_LDADD_AMD64_LINUX)
helgrind_ukas_nolib_amd64_linux_LDFLAGS      = $(TOOL_LDFLAGS_AMD64_LINUX)

helgrind_ukas_nolib_ppc32_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukas_nolib_ppc32_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
helgrind_ukas_nolib_ppc32_linux_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX) $(HELGRIND_UKAS_CFLAGS) -O2
helgrind_ukas_nolib_ppc32_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_LINUX)
helgrind_ukas_nolib_ppc32_linux_LDADD        = $(TOOL_LDADD_PPC32_LINUX)
helgrind_ukas_nolib_ppc32_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_LINUX)

helgrind_ukas_nolib_ppc64_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukas_nolib_ppc64_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
helgrind_ukas_nolib_ppc64_linux_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX) $(HELGRIND_UKAS_CFLAGS) -O2
helgrind_ukas_nolib_ppc64_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_LINUX)
helgrind_ukas_nolib_ppc64_linux_LDADD        = $(TOOL_LDADD_PPC64_LINUX)
helgrind_ukas_nolib_ppc64_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_LINUX)

helgrind_ukas_nolib_ppc32_aix5_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukas_nolib_ppc32_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
helgrind_ukas_nolib_ppc32_aix5_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5) $(HELGRIND_UKAS_CFLAGS) -O2
helgrind_ukas_nolib_ppc32_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_AIX5)
helgrind_ukas_nolib_ppc32_aix5_LDADD        = $(TOOL_LDADD_PPC32_AIX5)
helgrind_ukas_nolib_ppc32_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_AIX5)

helgrind_ukas_nolib_ppc64_aix5_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukas_nolib_ppc64_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
helgrind_ukas_nolib_ppc64_aix5_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5) $(HELGRIND_UKAS_CFLAGS) -O2
helgrind_ukas_nolib_ppc64_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_AIX5)
helgrind_ukas_nolib_ppc64_aix5_LDADD        = $(TOOL_LDADD_PPC64_AIX5)
helgrind_ukas_nolib_ppc64_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_AIX5)

##

## MSM long no pthread interception

vgpreload_helgrind_ukal_nolib_x86_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukal_nolib_x86_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
vgpreload_helgrind_ukal_nolib_x86_linux_so_CFLAGS       = $(AM_CFLAGS_X86_LINUX) $(AM_CFLAGS_PIC) $(LIBPRELOAD_NOLIB_CFLAGS)
vgpreload_helgrind_ukal_nolib_x86_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_X86_LINUX) $(LIBREPLACEGOMP_X86_LINUX)
vgpreload_helgrind_ukal_nolib_x86_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_X86_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_X86_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_X86_LINUX)

vgpreload_helgrind_ukal_nolib_amd64_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukal_nolib_amd64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
vgpreload_helgrind_ukal_nolib_amd64_linux_so_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX) $(AM_CFLAGS_PIC) $(LIBPRELOAD_NOLIB_CFLAGS)
vgpreload_helgrind_ukal_nolib_amd64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_AMD64_LINUX) $(LIBREPLACEGOMP_AMD64_LINUX)
vgpreload_helgrind_ukal_nolib_amd64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_AMD64_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_AMD64_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_AMD64_LINUX)

vgpreload_helgrind_ukal_nolib_ppc32_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukal_nolib_ppc32_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
vgpreload_helgrind_ukal_nolib_ppc32_linux_so_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX) $(AM_CFLAGS_PIC) $(LIBPRELOAD_NOLIB_CFLAGS)
vgpreload_helgrind_ukal_nolib_ppc32_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_LINUX) $(LIBREPLACEGOMP_PPC32_LINUX)
vgpreload_helgrind_ukal_nolib_ppc32_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC32_LINUX) \
   $(LIBREPLACEGOMP_LDFLAGS_PPC32_LINUX)

vgpreload_helgrind_ukal_nolib_ppc64_linux_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukal_nolib_ppc64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
vgpreload_helgrind_ukal_nolib_ppc64_linux_so_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX) $(AM_CFLAGS_PIC) $(LIBPRELOAD_NOLIB_CFLAGS)
vgpreload_helgrind_ukal_nolib_ppc64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_LINUX) $(LIBREPLACEGOMP_PPC64_LINUX)
vgpreload_helgrind_ukal_nolib_ppc64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_LINUX) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC64_LINUX) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC64_LINUX)

vgpreload_helgrind_ukal_nolib_ppc32_aix5_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukal_nolib_ppc32_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
vgpreload_helgrind_ukal_nolib_ppc32_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5) $(AM_CFLAGS_PIC) $(LIBPRELOAD_NOLIB_CFLAGS)
vgpreload_helgrind_ukal_nolib_ppc32_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_AIX5) $(LIBREPLACEGOMP_PPC32_AIX5)
vgpreload_helgrind_ukal_nolib_ppc32_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_AIX5) \
   $(LIBREPLACEGOMP_LDFLAGS_PPC32_AIX5) \
	$(LIBREPLACEMALLOC_LDFLAGS_PPC32_AIX5)

vgpreload_helgrind_ukal_nolib_ppc64_aix5_so_SOURCES      = $(VGPRELOAD_HELGRIND_SOURCES_COMMON)
vgpreload_helgrind_ukal_nolib_ppc64_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
vgpreload_helgrind_ukal_nolib_ppc64_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5) $(AM_CFLAGS_PIC) $(LIBPRELOAD_NOLIB_CFLAGS)
vgpreload_helgrind_ukal_nolib_ppc64_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_AIX5) $(LIBREPLACEGOMP_PPC64_AIX5)
vgpreload_helgrind_ukal_nolib_ppc64_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_AIX5) \
   $(LIBREPLACEMALLOC_LDFLAGS_PPC64_AIX5) \
	$(LIBREPLACEGOMP_LDFLAGS_PPC64_AIX5)

helgrind_ukal_nolib_x86_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukal_nolib_x86_linux_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
helgrind_ukal_nolib_x86_linux_CFLAGS       = $(AM_CFLAGS_X86_LINUX) $(HELGRIND_UKAL_CFLAGS) -O2
helgrind_ukal_nolib_x86_linux_DEPENDENCIES = $(COREGRIND_LIBS_X86_LINUX)
helgrind_ukal_nolib_x86_linux_LDADD        = $(TOOL_LDADD_X86_LINUX)
helgrind_ukal_nolib_x86_linux_LDFLAGS      = $(TOOL_LDFLAGS_X86_LINUX)

helgrind_ukal_nolib_amd64_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukal_nolib_amd64_linux_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
helgrind_ukal_nolib_amd64_linux_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX) $(HELGRIND_UKAL_CFLAGS) -O2
helgrind_ukal_nolib_amd64_linux_DEPENDENCIES = $(COREGRIND_LIBS_AMD64_LINUX)
helgrind_ukal_nolib_amd64_linux_LDADD        = $(TOOL_LDADD_AMD64_LINUX)
helgrind_ukal_nolib_amd64_linux_LDFLAGS      = $(TOOL_LDFLAGS_AMD64_LINUX)

helgrind_ukal_nolib_ppc32_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukal_nolib_ppc32_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
helgrind_ukal_nolib_ppc32_linux_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX) $(HELGRIND_UKAL_CFLAGS) -O2
helgrind_ukal_nolib_ppc32_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_LINUX)
helgrind_ukal_nolib_ppc32_linux_LDADD        = $(TOOL_LDADD_PPC32_LINUX)
helgrind_ukal_nolib_ppc32_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_LINUX)

helgrind_ukal_nolib_ppc64_linux_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukal_nolib_ppc64_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
helgrind_ukal_nolib_ppc64_linux_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX) $(HELGRIND_UKAL_CFLAGS) -O2
helgrind_ukal_nolib_ppc64_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_LINUX)
helgrind_ukal_nolib_ppc64_linux_LDADD        = $(TOOL_LDADD_PPC64_LINUX)
helgrind_ukal_nolib_ppc64_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_LINUX)

helgrind_ukal_nolib_ppc32_aix5_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukal_nolib_ppc32_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
helgrind_ukal_nolib_ppc32_aix5_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5) $(HELGRIND_UKAL_CFLAGS) -O2
helgrind_ukal_nolib_ppc32_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_AIX5)
helgrind_ukal_nolib_ppc32_aix5_LDADD        = $(TOOL_LDADD_PPC32_AIX5)
helgrind_ukal_nolib_ppc32_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_AIX5)

helgrind_ukal_nolib_ppc64_aix5_SOURCES      = $(HELGRIND_SOURCES_COMMON)
helgrind_ukal_nolib_ppc64_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
helgrind_ukal_nolib_ppc64_aix5_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5) $(HELGRIND_UKAL_CFLAGS) -O2
helgrind_ukal_nolib_ppc64_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_AIX5)
helgrind_ukal_nolib_ppc64_aix5_LDADD        = $(TOOL_LDADD_PPC64_AIX5)
helgrind_ukal_nolib_ppc64_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_AIX5)

##





hgincludedir = $(includedir)/valgrind

hginclude_HEADERS = helgrind.h

noinst_HEADERS = \
	hg_basics.h hg_lock_n_thread.h hg_errors.h hg_wordset.h \
	libhb.h

EXTRA_DIST = README_MSMProp2.txt README_YARD.txt
