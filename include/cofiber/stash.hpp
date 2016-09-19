
#include <type_traits>
#include <atomic>

namespace cofiber {
	template<typename T>
	struct stash {
	private:
		enum {
			has_value = 1,
			is_waiting = 2
		};

	public:
		stash()
		: _state(0) { }

		stash(const stash &other) = delete;
		stash(stash &&other) = delete;

		~stash() {
			if(_state.load(std::memory_order_relaxed) & has_value)
				reinterpret_cast<T *>(&_storage)->~T();
		}

		stash &operator= (stash other) = delete;

		void set_value(T value) {
			new (&_storage) T(std::move(value));

			int current = _state.fetch_or(has_value, std::memory_order_acq_rel);
			if(current & is_waiting)
				_handle.resume();
		}

		T &operator* () {
			assert(_state.load(std::memory_order_relaxed) & has_value);
			return *reinterpret_cast<T *>(&_storage);
		}
		T *operator-> () {
			return &(**this);
		}

		bool await_ready() {
			return _state.load(std::memory_order_relaxed) & has_value;
		}

		void await_suspend(coroutine_handle<> handle) {
			_handle = handle;

			int current = _state.fetch_or(is_waiting, std::memory_order_acq_rel);
			assert(!(current & is_waiting));
			if(current & has_value)
				_handle.resume();
		}

		void await_resume() { }

	private:
		std::atomic<int> _state;
		typename std::aligned_storage<sizeof(T), alignof(T)>::type _storage;
		coroutine_handle<> _handle;
	};

} // namespace cofiber

