#include "SDK-2025-03-07/foobar2000/SDK/foobar2000.h"

DECLARE_COMPONENT_VERSION(
    "Component Update Checker",
    "0.1.0",
    "Checks for updates to installed foobar2000 components.\n"
    "Does not auto-download or auto-install anything."
);

// foobar2000本体との連携に必要なvalidate_build_level_bindableのため。
// (foo_albumtrainのSDKバージョンと揃えて要調整)
VALIDATE_COMPONENT_FILENAME("foo_component_update_checker.dll");
