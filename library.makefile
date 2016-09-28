
OBJECTS = obj/globals.o obj/trampoline.o
TESTS = tests/main tests/stash tests/future

.PHONY: all
all: bin/libcofiber.so

.PHONY: clean
clean:
	rm -rf obj/ bin/ tests/

.PHONY: test
test: $(TESTS)
	@for i in $(TESTS); do echo "Running $$i"; ./$$i; done

.PHONY: install
install: bin/libcofiber.so
	mkdir -p $(DESTDIR)$(prefix)lib $(DESTDIR)$(prefix)include $(DESTDIR)$(prefix)include/cofiber
	install bin/libcofiber.so $(DESTDIR)$(prefix)lib/
	install --mode=0644 $S/include/cofiber.hpp $(DESTDIR)$(prefix)include/
	install --mode=0644 $S/include/cofiber/stash.hpp $(DESTDIR)$(prefix)include/cofiber/

obj bin tests:
	mkdir -p $@

obj/%.o: $S/src/%.cpp | obj
	$(CXX) -c -o $@ -fPIC -std=c++14 -I $S/include $S/src/$*.cpp

obj/%.o: $S/src/%.s | obj
	$(AS) -o $@ $S/src/$*.s

bin/libcofiber.so: $(OBJECTS) | bin
	$(CXX) -shared -o $@ $(OBJECTS)

tests/%: $S/tests/%.cpp bin/libcofiber.so | tests
	$(CXX) -o $@ -std=c++14 -I $S/include $S/tests/$*.cpp bin/libcofiber.so

