#pragma once

// SaltyGills Tackle Box: lots of handy tools in one small box.
// Bump FW_VERSION with every release and add an entry to CHANGELOG.md.
#define FW_VERSION "1.8.2"
#define PRODUCT_NAME "SaltyGills Tackle Box"
#define PRODUCT_SHORT "Tackle Box"
#define PRODUCT_TAGLINE "Lots of handy tools in one small box"
#if defined(BOARD_NM_CYD_C5)
#define PRODUCT_HOSTNAME "tacklebox-c5"  // tacklebox-c5.local; both boards can share a network
#else
#define PRODUCT_HOSTNAME "tacklebox"  // tacklebox.local for OTA and on the network
#endif
#define PRODUCT_UA "SaltyGillsTackleBox/" FW_VERSION
