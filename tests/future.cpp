
#include <cofiber.hpp>
#include <cofiber/future.hpp>

COFIBER_ROUTINE(cofiber::future<int>, f(), [] () {
	COFIBER_RETURN(5);
});

COFIBER_ROUTINE(cofiber::no_future, g(), [] () {
	cofiber::future<int> future = f();
	cofiber::shared_future<int> shared = future.share();

	int x = COFIBER_AWAIT shared;
	std::cout << "consumer got " << x << std::endl;
	int y = COFIBER_AWAIT shared;
	std::cout << "consumer got " << y << std::endl;
});

int main() {
	g();
}

