
#include <atomic>
#include <functional>
#include <queue>
#include <memory>

namespace cofiber {

namespace _future {
	// ------------------------------------------------------------------------
	// Internal shared state.
	// ------------------------------------------------------------------------

	enum {
		has_value = 1,
		has_functor = 2
	};
	
	struct state_base {
		state_base()
		: refs(1), status(0) { }

		std::atomic<int> refs;
		std::atomic<int> status;
		std::function<void()> functor;
	};

	template<typename T>
	struct state_struct : state_base {
		std::aligned_storage<sizeof(T), alignof(T)> storage;
	};

	template<>
	struct state_struct<void> : state_base { };
	
	// ------------------------------------------------------------------------
	// future class.
	// ------------------------------------------------------------------------
	
	template<typename T>
	struct promise;
	
	struct future_base {
		friend void swap(future_base &a, future_base &b) {
			using std::swap;
			swap(a._state, b._state);
		}

		future_base(const future_base &) = delete;
		
		future_base(future_base &&other) {
			swap(*this, other);
		}

		~future_base() {
			if(_state->refs.fetch_sub(1, std::memory_order_release) == 1) {
				std::atomic_thread_fence(std::memory_order_acquire);
				delete _state;
			}
		}

		future_base &operator= (future_base other) {
			swap(*this, other);
			return *this;
		}
		
		template<typename F>
		void then(F &&functor) {
			_state->functor = std::function<void()>(std::forward<F>(functor));
			int s = _state->status.fetch_or(has_functor);
			assert(!(s & has_functor));
			if(s & has_value)
				_state->functor();
		}

		bool await_ready() {
			return false;
		}

		void await_suspend(coroutine_handle<> handle) {
			then([=] {
				handle.resume();
			});
		}
		
	protected:
		explicit future_base(state_base *state)
		: _state(state) {
			_state->refs.fetch_add(1, std::memory_order_relaxed);
		}

		state_base *_state;
	};

	template<typename T>
	struct future : future_base {
		friend class promise<T>;

		T await_resume() {
			auto derived = static_cast<state_struct<T> *>(_state);
			return std::move(*reinterpret_cast<T *>(&derived->storage));
		}

	private:
		explicit future(state_struct<T> *state)
		: future_base(state) { }
	};

	template<>
	struct future<void> : future_base {
		friend class promise<void>;

		void await_resume() { }

	private:
		explicit future(state_struct<void> *state)
		: future_base(state) { }
	};
	
	// ------------------------------------------------------------------------
	// promise class.
	// ------------------------------------------------------------------------

	struct promise_base {
		friend void swap(promise_base &a, promise_base &b) {
			using std::swap;
			swap(a._state, b._state);
		}
		
		promise_base(const promise_base &) = delete;
		
		promise_base(promise_base &&other) {
			swap(*this, other);
		}

		~promise_base() {
			if(_state->refs.fetch_sub(1, std::memory_order_release) == 1) {
				std::atomic_thread_fence(std::memory_order_acquire);
				delete _state;
			}
		}

		promise_base &operator= (promise_base other) {
			swap(*this, other);
			return *this;
		}

	protected:
		explicit promise_base(state_base *state)
		: _state(state) { }

		state_base *_state;
	};
	
	template<typename T>
	struct promise : promise_base {
		promise()
		: promise_base(new state_struct<T>) { }

		void set_value(T value) {
			auto derived = static_cast<state_struct<T> *>(_state);
			new (&derived->storage) T(std::move(value));

			int s = _state->status.fetch_or(has_value);
			assert(!(s & has_value));
			if(s & has_functor)
				_state->functor();
		}
		
		future<T> get_future() {
			auto derived = static_cast<state_struct<T> *>(_state);
			return future<T>(derived);
		}
	};
	
	template<>
	struct promise<void> : promise_base {
		promise()
		: promise_base(new state_struct<void>) { }

		void set_value() {
			int s = _state->status.fetch_or(has_value);
			assert(!(s & has_value));
			if(s & has_functor)
				_state->functor();
		}
		
		future<void> get_future() {
			auto derived = static_cast<state_struct<void> *>(_state);
			return future<void>(derived);
		}
	};
};

using _future::future;
using _future::promise;

template<typename T>
struct coroutine_traits<future<T>> {
	struct promise_type {
		future<T> get_return_object(coroutine_handle<> handle) {
			return _promise.get_future();
		}

		auto initial_suspend() { return suspend_never(); }
		auto final_suspend() { return suspend_never(); }

		template<typename... V>
		void return_value(V &&... value) {
			_promise.set_value(std::forward<V>(value)...);
		}

	private:
		promise<T> _promise;
	};
};

} // namespace cofiber

