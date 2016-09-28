
#include <cofiber.hpp>

namespace _cofiber_private {
	thread_local std::vector<activation_struct> stack;
}

