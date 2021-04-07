#include <aw/aw.hpp>
#include <queue>
#include <mutex>

template<typename T, std::uint64_t QueueDepth>
struct concurrent_bounded_queue
{
public:
  constexpr concurrent_bounded_queue() = default;

  void enqueue( std::convertible_to<T> auto&& u )
  {
    remaining_space.acquire();
    {
      std::scoped_lock l( items_mutex );
      items.emplace( std::forward<decltype( u )>( u ) );
    }
    items_produced.release();
  }

  T dequeue()
  {
    items_produced.acquire();
    T tmp = pop();
    remaining_space.release();
    return std::move( tmp );
  }
  
  std::optional<T> try_dequeue()
  {
    if ( !items_produced.try_acquire() )
    {
      return {};
    }
    T tmp = pop();
    remaining_space.release();
    return std::move( tmp );
  }
  
private:
  T pop()
  {
    std::optional<T> tmp;
    std::scoped_lock lock( items_mutex );
    tmp = std::move( items.front() );
    items.pop();
    return std::move( *tmp );
  }

private:
  std::queue<T> items;
  std::mutex items_mutex;
  std::counting_semaphore<QueueDepth> items_produced{0};
  std::counting_semaphore<QueueDepth> remaining_space{QueueDepth};
}; /* concurrent_bounded_queue */

int main()
{
  return 0;
}
