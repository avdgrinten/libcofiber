
#include <atomic>
#include <functional>
#include <queue>
#include <memory>

namespace cofiber {

namespace _future {
	enum {
		has_value = 1,
		has_functor = 2
	};

	template<typename T>
	struct state_struct {
		state_struct()
		: refs(1), status(0) { }

		std::atomic<int> refs;
		std::atomic<int> status;
//		std::aligned_storage<sizeof(T), alignof(T)> storage;
		std::function<void()> functor;
	};
	
	template<typename T>
	struct promise;

	template<typename T>
	struct future {
		friend class promise<T>;

		friend void swap(future &a, future &b) {
			using std::swap;
			swap(a._state, b._state);
		}

		future(const future &) = delete;
		
		future(future &&other) {
			swap(*this, other);
		}

		~future() {
			if(_state->refs.fetch_sub(1, std::memory_order_release) == 1) {
				std::atomic_thread_fence(std::memory_order_acquire);
				delete _state;
			}
		}

		future &operator= (future other) {
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

		void await_resume() {

		}
	private:
		explicit future(state_struct<T> *state)
		: _state(state) {
			_state->refs.fetch_add(1, std::memory_order_relaxed);
		}

		state_struct<T> *_state;
	};

	template<typename T>
	struct promise {
		friend void swap(promise &a, promise &b) {
			using std::swap;
			swap(a._state, b._state);
		}
		
		promise(const promise &) = delete;
		
		promise(promise &&other) {
			swap(*this, other);
		}

		~promise() {
			if(_state->refs.fetch_sub(1, std::memory_order_release) == 1) {
				std::atomic_thread_fence(std::memory_order_acquire);
				delete _state;
			}
		}

		promise &operator= (promise other) {
			swap(*this, other);
			return *this;
		}

		promise()
		: _state(new state_struct<T>) { }

		future<T> get_future() {
			return future<T>(_state);
		}

		void set_value() {
			int s = _state->status.fetch_or(has_value);
			assert(!(s & has_value));
			if(s & has_functor)
				_state->functor();
		}

	private:
		state_struct<T> *_state;
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

		void return_value() {
			_promise.set_value();
		}

	private:
		promise<T> _promise;
	};
};

} // namespace cofiber

