/* One place for the release string. It was three -- the loading line in entry.c and the two
   packaging scripts' defaults -- and entry.c was the one that got forgotten, so v0.5.4-test
   shipped a runtime that logged itself as v0.5.3-test. The menu is C++ and compiled on its
   own, so this lives in its own header rather than in common.h. */
#ifndef HFR_VERSION_H
#define HFR_VERSION_H
#define HFR_VERSION "0.9-test"
#endif
