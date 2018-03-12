
#ifndef LIBCOFIBER_COFIBER_HPP
#define LIBCOFIBER_COFIBER_HPP

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <future>
#include <new>
#include <utility>
#include <vector>

#define COFIBER_ROUTINE(type, name_args, functor) \
	type name_args { \
		using _cofiber_type = type; \
		return ::_cofiber_private::do_routine<_cofiber_type>(functor); \
	}

#define COFIBER_AWAIT \
	::_cofiber_private::await_expr<_cofiber_type>() |

#define COFIBER_YIELD \
	::_cofiber_private::yield_expr<_cofiber_type>() |

#define COFIBER_RETURN(value) \
	do { \
		::_cofiber_private::do_return<_cofiber_type>(value); \
		return; \
	} while(0)

template<typename T,
		typename = decltype(std::declval<T>().await_ready())>
T &&cofiber_awaiter(T &&awaiter) {
	return std::forward<T>(awaiter);
}

namespace _cofiber_private {
	extern "C" void _cofiber_enter(void *argument, void (*function) (void *, void *),
			void *initial_sp);
	extern "C" void _cofiber_restore(void *argument, void (*hook) (void *, void *),
			void *restore_sp);

	template<typename F>
	void enter(F functor, void *initial_sp) {
		_cofiber_enter(&functor, [] (void *argument, void *original_sp) {
			F stolen = std::move(*static_cast<F *>(argument));
			stolen(original_sp);
		}, initial_sp);
	}

	template<typename F>
	void restore(F functor, void *initial_sp) {
		_cofiber_restore(&functor, [] (void *argument, void *caller_sp) {
			F stolen = std::move(*static_cast<F *>(argument));
			stolen(caller_sp);
		}, initial_sp);
	}

	struct state_struct {
		state_struct()
		: suspended_sp(nullptr) { }

		void *suspended_sp;
	};

	struct activation_struct {
		activation_struct(state_struct *state, void *caller_sp)
		: state(state), caller_sp(caller_sp) { }

		state_struct *state;
		void *caller_sp;
	};

	struct destroy_exception { };

	extern thread_local std::vector<activation_struct> stack;
} // namespace _cofiber_private

namespace cofiber {
	template<typename P = void>
	struct coroutine_handle;
	
	template<>
	struct coroutine_handle<void> {
		static coroutine_handle from_address(void *address) {
			auto state = static_cast<_cofiber_private::state_struct *>(address);
			return coroutine_handle(state);
		}

		coroutine_handle()
		: _state(nullptr) { }

		coroutine_handle(_cofiber_private::state_struct *state)
		: _state(state) { }

		void *address() {
			return _state;
		}

		explicit operator bool () {
			return _state != nullptr;
		}

		void resume() const {
			_cofiber_private::restore([this] (void *caller_sp) {
				_cofiber_private::stack.push_back({ _state, caller_sp });
			}, _state->suspended_sp);
		}

		void destroy() const {
			_cofiber_private::restore([this] (void *caller_sp) {
				_cofiber_private::stack.push_back({ _state, caller_sp });

				throw _cofiber_private::destroy_exception();
			}, _state->suspended_sp);
		}

	protected:
		_cofiber_private::state_struct *_state;
	};

	template<typename P>
	struct coroutine_handle : public coroutine_handle<> {
		static coroutine_handle from_promise(P &p) {
			auto ptr = (char *)&p + sizeof(P);
			assert(uintptr_t(ptr) % alignof(_cofiber_private::state_struct) == 0);
			return coroutine_handle(reinterpret_cast<_cofiber_private::state_struct *>(ptr));
		}

		coroutine_handle() = default;

		coroutine_handle(_cofiber_private::state_struct *state)
		: coroutine_handle<>(state) { }

		P &promise() {
			auto ptr = (char *)_state - sizeof(P);
			assert(uintptr_t(ptr) % alignof(P) == 0);
			return *reinterpret_cast<P *>(ptr);
		}
	};

	template<typename T>
	struct coroutine_traits {
		using promise_type = typename T::promise_type;
	};

	struct suspend_never {
		bool await_ready() { return true; }
		void await_suspend(coroutine_handle<>) { }
		void await_resume() { }
	};

	struct suspend_always {
		bool await_ready() { return false; }
		void await_suspend(coroutine_handle<>) { }
		void await_resume() { }
	};

	struct no_future {
		struct promise_type {
			no_future get_return_object(coroutine_handle<>) {
				return no_future();
			}

			auto initial_suspend() { return suspend_never(); }
			auto final_suspend() { return suspend_never(); }

			void return_value() { }
		};
	};
} // namespace cofiber

namespace _cofiber_private {
	// Helper function for await. We need to take the awaiter as argument
	// so that we can extends its lifetime if it is a reference.
	template<typename X, typename Awaiter>
	auto do_await(Awaiter &&awaiter) {
		using P = typename cofiber::coroutine_traits<X>::promise_type;

		if(!awaiter.await_ready()) {
			_cofiber_private::restore([&awaiter] (void *coroutine_sp) {
				auto state = _cofiber_private::stack.back().state;
				_cofiber_private::stack.pop_back();

				state->suspended_sp = coroutine_sp;

				awaiter.await_suspend(cofiber::coroutine_handle<P>(state));
			}, _cofiber_private::stack.back().caller_sp);
		}

		return awaiter.await_resume();
	}

	template<typename X>
	struct await_expr { };

	template<typename X, typename T>
	decltype(cofiber_awaiter(std::declval<T>()).await_resume())
	operator| (await_expr<X>, T &&expression) {
		using P = typename cofiber::coroutine_traits<X>::promise_type;

		return do_await<X>(cofiber_awaiter(std::forward<T>(expression)));
	}
	
	template<typename X>
	struct yield_expr { };

	template<typename X, typename T>
	auto operator| (yield_expr<X>, T &&expr) {
		using P = typename cofiber::coroutine_traits<X>::promise_type;

		cofiber::coroutine_handle<P> handle(_cofiber_private::stack.back().state);
		return do_await<X>(cofiber_awaiter(handle.promise().yield_value(std::forward<T>(expr))));
	}

	template<typename X>
	void do_return() {
		using P = typename cofiber::coroutine_traits<X>::promise_type;

		cofiber::coroutine_handle<P> handle(_cofiber_private::stack.back().state);
		handle.promise().return_value();
	}

	template<typename X, typename T>
	void do_return(T &&value) {
		using P = typename cofiber::coroutine_traits<X>::promise_type;

		cofiber::coroutine_handle<P> handle(_cofiber_private::stack.back().state);
		handle.promise().return_value(std::forward<T>(value));
	}
	
	template<typename X, typename F>
	X do_routine(F functor) {
		using P = typename cofiber::coroutine_traits<X>::promise_type;

		size_t stack_size = 0x100000;
		auto bottom = (char *)(operator new(stack_size));
		auto sp = bottom + stack_size;
		
		// Allocate both the coroutine state and the promise on the fiber stack.
		sp -= sizeof(_cofiber_private::state_struct);
		assert(uintptr_t(sp) % alignof(_cofiber_private::state_struct) == 0);
		auto state = new (sp) _cofiber_private::state_struct;

		sp -= sizeof(P);
		assert(uintptr_t(sp) % alignof(P) == 0);
		auto promise = new (sp) P;

		// Align the coroutine stack on a 16-byte boundary.
		sp -= (uintptr_t)sp % 16;

		auto object = promise->get_return_object(cofiber::coroutine_handle<P>(state));

		_cofiber_private::enter([f = std::move(functor), bottom, state, promise] (void *original_sp) mutable {
			_cofiber_private::stack.push_back({ state, original_sp });

			try {
				await_expr<X>() | promise->initial_suspend();
				f();
				await_expr<X>() | promise->final_suspend();
			}catch(_cofiber_private::destroy_exception &) {
				// Ignore the exception that is thrown by destroy().
			}catch(...) {
				std::terminate();
			}

			// Destruct the functor here as we never properly unwind this stack. 
			f.~F();
			promise->~P();

			_cofiber_private::restore([bottom, state] (void *coroutine_sp) {
				auto state = _cofiber_private::stack.back().state;
				_cofiber_private::stack.pop_back();
				operator delete(bottom);
			}, _cofiber_private::stack.back().caller_sp);
		}, sp);

		return std::move(object);
	}
} // namespace _cofiber_private

// ----------------------------------------------------------------------------
// Support for std::future
// ----------------------------------------------------------------------------

namespace cofiber {
	template<typename T>
	struct coroutine_traits<std::future<T>> {
		struct promise_type {
			std::future<T> get_return_object(coroutine_handle<> handle) {
				return _promise.get_future();
			}

			auto initial_suspend() { return suspend_never(); }
			auto final_suspend() { return suspend_never(); }

			template<typename... V>
			void return_value(V &&... value) {
				_promise.set_value(std::forward<V>(value)...);
			}

		private:
			std::promise<T> _promise;
		};
	};
}

#endif // LIBCOFIBER_COFIBER_HPP

