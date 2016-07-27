
OBJECTS = obj/trampoline.o
TESTS = tests/main

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
	mkdir -p $(DESTDIR)$(prefix)lib $(DESTDIR)$(prefix)include
	install bin/libcofiber.so $(DESTDIR)$(prefix)lib/
	install --mode=0644 $S/include/cofiber.hpp $(DESTDIR)$(prefix)include/

obj bin tests:
	mkdir -p $@

obj/%.o: $S/src/%.s | obj
	$(AS) -o $@ $S/src/$*.s

bin/libcofiber.so: $(OBJECTS) | bin
	$(LD) -shared -o $@ $(OBJECTS)

tests/%: $S/tests/%.cpp bin/libcofiber.so | tests
	$(CXX) -o $@ -std=c++14 -I $S/include $S/tests/$*.cpp bin/libcofiber.so

