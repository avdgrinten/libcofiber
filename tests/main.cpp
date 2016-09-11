
#include <iostream>

#include <cofiber.hpp>

COFIBER_ROUTINE(cofiber::no_future, test(), [], {
	printf("Hello world\n");
})

int main() {
	printf("Before coroutine\n");
	test();
	printf("After coroutine\n");
}

