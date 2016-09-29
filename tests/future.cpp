
#include <iostream>

#include <cofiber.hpp>
#include <cofiber/future.hpp>

COFIBER_ROUTINE(cofiber::future<int>, h(), [] () {
	COFIBER_RETURN(42);
});

COFIBER_ROUTINE(cofiber::future<void>, f(), [] () {
	auto x = COFIBER_AWAIT h();
	std::cout << "h() returned " << x << std::endl;
	COFIBER_RETURN();
});

COFIBER_ROUTINE(cofiber::no_future, g(), [] () {
	COFIBER_AWAIT f();
	std::cout << "Await complete" << std::endl;
});

int main() {
	g();
}

