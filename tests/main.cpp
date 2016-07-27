
#include <iostream>

#include <cofiber.hpp>

auto test() {
	return cofiber_routine<cofiber::no_future>([=] () {
		printf("Hello world\n");
		throw _cofiber_private::destroy_exception();
	});
}

int main() {
	printf("Before coroutine\n");
	test();
	printf("After coroutine\n");
}

