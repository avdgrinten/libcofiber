
#include <cofiber.hpp>

namespace _cofiber_private {
	thread_local std::vector<char *> alloc_cache;
	thread_local std::vector<activation_struct> stack;
	thread_local std::unordered_map<const char *, uint64_t> leaks;
}

