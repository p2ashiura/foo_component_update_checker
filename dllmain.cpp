#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"

DECLARE_COMPONENT_VERSION(
    "Component Update Checker",
    "2.1.0",
    "Checks installed third-party foobar2000 components for available updates and notifies you when one is found — it does not auto-download, auto-install, or auto-replace anything.\n"
    "\n"
    "(C) p2ashiura\n"
    "Released under the MIT License.\n"
    "https://github.com/p2ashiura/foo_component_update_checker"
);

// foobar2000本体との連携に必要なvalidate_build_level_bindableのため。
// (foo_albumtrainのSDKバージョンと揃えて要調整)
VALIDATE_COMPONENT_FILENAME("foo_component_update_checker.dll");
