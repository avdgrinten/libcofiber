
#include <iostream>

#include <cofiber.hpp>
#include <cofiber/future.hpp>

COFIBER_ROUTINE(cofiber::future<void>, f(), [] () {
	COFIBER_RETURN();
});

COFIBER_ROUTINE(cofiber::no_future, g(), [] () {
	COFIBER_AWAIT f();
	std::cout << "Await complete" << std::endl;
});

int main() {
	g();
}

