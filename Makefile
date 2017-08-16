# Makefile at top of site support tree
# Makefile,v 1.3 1999/03/09 20:28:34 anj Exp
TOP = .
include $(TOP)/configure/CONFIG

DIRS += miscUtils
DIRS += devBusMapped
DIRS += drvRTEMSDma
DIRS += devEpicsDma
ifneq ($(EPICS_HOST_ARCH),solaris-sparc-gnu)
DIRS += devGenVar
endif

include $(TOP)/configure/RULES_TOP
