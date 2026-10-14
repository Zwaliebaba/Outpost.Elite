// GameLogic/Task.h
#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace Elite
{

/// A routine of the game that can wait (D77): a C++20 coroutine that runs when it is awaited and returns a T. Awaiting a Task runs
/// it until it finishes or a wait suspends it; a suspended task is resumed by the host (Platform::RunUntil), and when it
/// finishes, the routine that awaited it carries on, by symmetric transfer, with no stack growth. An exception leaves the task
/// and is thrown again where it was awaited. A Task is awaited once.
template <typename T> class [[nodiscard]] Task;

namespace TaskDetail
{

struct TaskPromise
{
  std::coroutine_handle<> continuation;
  std::exception_ptr error;

  [[nodiscard]] std::suspend_always initial_suspend() const noexcept
  {
    return {};
  }

  struct FinalAwaiter
  {
    [[nodiscard]] bool await_ready() const noexcept
    {
      return false;
    }

    template <typename Promise> [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> _done) const noexcept
    {
      const std::coroutine_handle<> next = _done.promise().continuation;
      return next ? next : std::noop_coroutine();
    }

    void await_resume() const noexcept {}
  };

  [[nodiscard]] FinalAwaiter final_suspend() const noexcept
  {
    return {};
  }

  void unhandled_exception() noexcept
  {
    error = std::current_exception();
  }
};

} // namespace TaskDetail

template <typename T> class [[nodiscard]] Task
{
public:
  struct promise_type : TaskDetail::TaskPromise
  {
    std::optional<T> value;

    [[nodiscard]] Task get_return_object() noexcept
    {
      return Task(std::coroutine_handle<promise_type>::from_promise(*this));
    }

    template <typename U> void return_value(U&& _value)
    {
      value.emplace(std::forward<U>(_value));
    }
  };

  Task(Task&& _other) noexcept
    : m_handle(std::exchange(_other.m_handle, nullptr))
  {
  }

  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&&) = delete;

  ~Task()
  {
    if (m_handle)
    {
      m_handle.destroy();
    }
  }

  [[nodiscard]] bool await_ready() const noexcept
  {
    return false;
  }

  [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<> _awaiting) noexcept
  {
    m_handle.promise().continuation = _awaiting;
    return m_handle;
  }

  T await_resume()
  {
    if (m_handle.promise().error)
    {
      std::rethrow_exception(m_handle.promise().error);
    }
    std::optional<T>& value = m_handle.promise().value;
    if (!value)
    {
      throw std::logic_error("Task: awaited for a value it did not return");
    }
    return std::move(*value);
  }

private:
  explicit Task(std::coroutine_handle<promise_type> _handle) noexcept
    : m_handle(_handle)
  {
  }

  std::coroutine_handle<promise_type> m_handle;
};

template <> class [[nodiscard]] Task<void>
{
public:
  struct promise_type : TaskDetail::TaskPromise
  {
    [[nodiscard]] Task get_return_object() noexcept
    {
      return Task(std::coroutine_handle<promise_type>::from_promise(*this));
    }

    void return_void() const noexcept {}
  };

  Task(Task&& _other) noexcept
    : m_handle(std::exchange(_other.m_handle, nullptr))
  {
  }

  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&&) = delete;

  ~Task()
  {
    if (m_handle)
    {
      m_handle.destroy();
    }
  }

  [[nodiscard]] bool await_ready() const noexcept
  {
    return false;
  }

  [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<> _awaiting) noexcept
  {
    m_handle.promise().continuation = _awaiting;
    return m_handle;
  }

  void await_resume() const
  {
    if (m_handle.promise().error)
    {
      std::rethrow_exception(m_handle.promise().error);
    }
  }

  /// The task as a host starts it: resumed from its start, its frame kept until the Task is destroyed (Platform).
  [[nodiscard]] std::coroutine_handle<> Handle() const noexcept
  {
    return m_handle;
  }

  [[nodiscard]] bool Done() const noexcept
  {
    return m_handle.done();
  }

  /// Throws what the task ended with, if it ended by an exception.
  void Rethrow() const
  {
    if (m_handle.promise().error)
    {
      std::rethrow_exception(m_handle.promise().error);
    }
  }

private:
  explicit Task(std::coroutine_handle<promise_type> _handle) noexcept
    : m_handle(_handle)
  {
  }

  std::coroutine_handle<promise_type> m_handle;
};

} // namespace Elite
