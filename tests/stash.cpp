
#include <iostream>

#include <cofiber.hpp>
#include <cofiber/stash.hpp>

cofiber::stash<int> stash;

COFIBER_ROUTINE(cofiber::no_future, test(), [] () {
	COFIBER_AWAIT stash;
	printf("stash contains %d!\n", *stash);
})

int main() {
	printf("Before coroutine\n");
	test();
	printf("After coroutine\n");
	stash.set_value(42);
}

