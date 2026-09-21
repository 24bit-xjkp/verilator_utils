// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

extern "C" const char* __asan_default_options() { return "check_initialization_order=1"; }

extern "C" const char* __ubsan_default_options() { return "print_stacktrace=1"; }

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
