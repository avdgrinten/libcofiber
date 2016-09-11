
#include <atomic>
#include <functional>
#include <experimental/optional>
#include <queue>
#include <memory>
#include <mutex>

#include <iostream>

namespace cofiber {

template<typename T>
struct future;

template<typename T>
struct shared_future;

namespace _private {
	template<typename T>
	struct future_manager {
		enum {
			have_result = 1,
			is_attached = 2,
			is_shared = 4
		};

		future_manager()
		: _status(0), _refcount(2), _done(false) { }

		void set_value(T result) {
			_result = std::move(result);

			int bits = _status.fetch_add(have_result, std::memory_order_acq_rel);
			assert(!(bits & have_result));
			if(bits & is_attached) {
				increment_refcount();
				_consumer(future<T>(this));
			}else if(bits & is_shared) {
				std::lock_guard<std::mutex> lock(_mutex);
				assert(!_done);
				while(!_continuations.empty()) {
					auto &consumer = _continuations.front();
					increment_refcount();
					consumer(shared_future<T>(this));
					_continuations.pop();
				}
				_done = true;
			}
		}

		void attach_consumer(std::function<void(future<T>)> consumer) {
			_consumer = std::move(consumer);

			int bits = _status.fetch_add(is_attached, std::memory_order_acq_rel);
			assert(!(bits & is_attached) && !(bits & is_shared));
			if(bits & have_result) {
				increment_refcount();
				_consumer(future<T>(this));
			}
		}

		void share() {
			int bits = _status.fetch_add(is_shared, std::memory_order_acq_rel);
			assert(!(bits & is_shared) && !(bits & is_shared));
			if(bits & have_result) {
				std::lock_guard<std::mutex> lock(_mutex);
				assert(!_done);
				_done = true;
			}
		}

		void add_continuation(std::function<void(shared_future<T>)> continuation) {
			std::lock_guard<std::mutex> lock(_mutex);
			if(_done) {
				increment_refcount();
				continuation(shared_future<T>(this));
			}else{
				_continuations.push(std::move(continuation));
			}
		}
		
		void increment_refcount() {
			_refcount.fetch_add(1, std::memory_order_relaxed);
		}

		void decrement_refcount() {
			int previous = _refcount.fetch_sub(1, std::memory_order_release);
			if(previous == 1) {
				std::atomic_thread_fence(std::memory_order_acquire);
				destroy();
			}
		}

		T &access() {
			assert(_result);
			return *_result;
		}

	protected:
		virtual void destroy() = 0;
	
	private:
		std::atomic<int> _status;
		std::atomic<int> _refcount;
		
		std::experimental::optional<T> _result;

		// TODO: turn this to a variant as soon as libstdc++ supports it. only one the { _consumer }
		// or { _mutex, _continuations, _done } field sets can be active at the same time.

		std::function<void(future<T>)> _consumer;
		
		std::mutex _mutex;
		
		// protected by _mutex.
		std::queue<std::function<void(shared_future<T>)>> _continuations;

		// protected by _mutex.
		// true as soon as the continuation queue has been processed.
		bool _done;
	};
} // namespace private

template<typename T>
struct future {
	friend struct _private::future_manager<T>;

	struct promise_type {
	private:
		struct coroutine_manager : public _private::future_manager<T> {
			coroutine_manager(coroutine_handle<> handle)
			: _handle(handle) { }

		protected:
			void destroy() override {
				std::cout << "destroy() called" << std::endl;
				_handle.destroy();
			}

		private:
			coroutine_handle<> _handle;
		};

	public:
		promise_type()
		: _manager(coroutine_handle<promise_type>::from_promise(*this)) { }

		future get_return_object(coroutine_handle<promise_type> handle) {
			return future(&_manager);
		}

		auto initial_suspend() {
			return suspend_never();
		}

		auto final_suspend() {
			struct awaiter {
				bool await_ready() { return false; }
				void await_resume() { }

				void await_suspend(coroutine_handle<promise_type> handle) {
					handle.promise()._manager.decrement_refcount();
				}
			};
			return awaiter();
		}

		void return_value(T value) {
			_manager.set_value(std::move(value));
		}

	private:
		coroutine_manager _manager;
	};

	friend void swap(future &a, future &b) {
		using std::swap;
		swap(a._manager_ptr, b._manager_ptr);
	}

	future()
	: _manager_ptr(nullptr) { }

	future(const future &other) = delete;

	future(future &&other)
	: future() {
		swap(*this, other);
	}

	~future() {
		if(_manager_ptr)
			_manager_ptr->decrement_refcount();
	}

	future &operator= (future other) {
		swap(*this, other);
	}

	template<typename F>
	void consume(F consumer) {
		_manager_ptr->attach_consumer(std::move(consumer));
		_manager_ptr->decrement_refcount();
		_manager_ptr = nullptr;
	}

	T get() {
		T result = std::move(_manager_ptr->access());
		_manager_ptr->decrement_refcount();
		_manager_ptr = nullptr;
		return std::move(result);
	}
	
	shared_future<T> share() {
		_manager_ptr->share();
		shared_future<T> result(_manager_ptr);
		_manager_ptr = nullptr;
		return result;
	}

private:
	future(_private::future_manager<T> *manager_ptr)
	: _manager_ptr(manager_ptr) { }

	_private::future_manager<T> *_manager_ptr;
};

template<typename T>
auto cofiber_awaiter(future<T> source) {
	struct awaiter {
		awaiter(future<T> source)
		: _source(std::move(source)) { }
		
		bool await_ready() { return false; }
		T await_resume() { return std::move(*_result); }

		void await_suspend(coroutine_handle<> handle) {
			// storing the handle inside the awaiter makes sure that the following
			// lambda only requires one word of storage so that std::function's
			// small functor optimization kicks in.
			_address = handle.address();

			_source.consume([this] (T result) {
				_result = std::move(result);
				coroutine_handle<>::from_address(_address).resume();
			});
		}

	private:
		future<T> _source;
		void *_address;
		std::experimental::optional<T> _result;
	};
	return awaiter(std::move(source));
}

template<typename T>
struct shared_future {
	friend struct future<T>;
	friend struct _private::future_manager<T>;

	friend void swap(shared_future &a, shared_future &b) {
		using std::swap;
		swap(a._manager_ptr, b._manager_ptr);
	}

	shared_future()
	: _manager_ptr(nullptr) { }

	shared_future(const shared_future &other)
	: _manager_ptr(other._manager_ptr) {
		_manager_ptr->increment_refcount();
	}

	shared_future(shared_future &&other)
	: shared_future() {
		swap(*this, other);
	}

	~shared_future() {
		if(_manager_ptr)
			_manager_ptr->decrement_refcount();
	}

	shared_future &operator= (shared_future other) {
		swap(*this, other);
	}

	template<typename F>
	void continuation(F consumer) {
		_manager_ptr->add_continuation(std::move(consumer));
	}

	const T &get() {
		return _manager_ptr->access();
	}

private:
	shared_future(_private::future_manager<T> *manager_ptr)
	: _manager_ptr(manager_ptr) { }

	_private::future_manager<T> *_manager_ptr;
};

template<typename T>
auto cofiber_awaiter(shared_future<T> source) {
	struct awaiter {
		awaiter(shared_future<T> source)
		: _source(std::move(source)) { }
		
		bool await_ready() { return false; }
		T await_resume() { return std::move(*_result); }

		void await_suspend(coroutine_handle<> handle) {
			// storing the handle inside the awaiter makes sure that the following
			// lambda only requires one word of storage so that std::function's
			// small functor optimization kicks in.
			_address = handle.address();

			_source.continuation([this] (shared_future<T> result) {
				_result = result.get();
				coroutine_handle<>::from_address(_address).resume();
			});
		}

	private:
		shared_future<T> _source;
		void *_address;
		std::experimental::optional<T> _result;
	};
	return awaiter(std::move(source));
}

} // namespace cofiber

