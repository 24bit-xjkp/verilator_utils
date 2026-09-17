// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" const char* __asan_default_options() { return "check_initialization_order=1,detect_container_overflow=0"; }
