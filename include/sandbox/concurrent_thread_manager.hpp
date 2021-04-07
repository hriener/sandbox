#pragma once

#include <aw/aw.hpp>
#include <any_invocable/any_invocable.hpp>
#include <queue>
#include <mutex>
#include <concepts>
#include <iostream>

namespace sandbox
{

struct thread_group
{
public:
  thread_group( std::uint64_t n, std::invocable<std::stop_token> auto&& f )
  {
    for ( auto i : std::views::iota( 0ul, n ) )
    {
      members.emplace_back( f );
    }
  }

  auto size() const
  {
    return members.size();
  }

  void request_stop()
  {
    for ( std::jthread& t : members )
    {
      t.request_stop();
    }
  }

private:
  std::vector<std::jthread> members;
};

template<typename T, std::uint64_t QueueDepth>
struct concurrent_bounded_queue
{
public:
  constexpr concurrent_bounded_queue() = default;

  void enqueue( std::convertible_to<T> auto&& u )
  {
    remaining_space.acquire();
    push( std::forward<decltype( u )>( u ) );
    items_produced.release();
  }

  bool try_enqueue( std::convertible_to<T> auto&& u )
  {
    if ( !remaining_space.try_acquire() )
    {
      return false;
    }
    push( std::forward<decltype( u )>( u ) );
    items_produced.release();
    return true;
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
  void push( std::convertible_to<T> auto&& u )
  {
    std::scoped_lock l( items_mutex );
    items.emplace( std::forward<decltype(u)>( u ) );
  }
  
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

template<std::uint64_t QueueDepth>
struct bounded_depth_task_manager
{
public:
  bounded_depth_task_manager( std::uint64_t n )
    : threads( n, [&]( std::stop_token s ){ process_tasks( s ); } )
  {}

  ~bounded_depth_task_manager()
  {
    std::latch l( threads.size() + 1 );
    for ( auto i : std::views::iota( 0ul, threads.size() ) )
    {
      submit( [&]{ l.arrive_and_wait(); } );
    }
    threads.request_stop();
    l.count_down();
  }

  void submit( std::invocable auto&& f )
  {
    while ( !tasks.try_enqueue( std::forward<decltype( f )>( f ) ) )
    {
      make_progress();
    }
  }

  // void submit( std::invocable auto&& f )
  // {
  //   tasks.enqueue( std::forward<decltype( f )>( f ) );
  // }
  
  void make_progress()
  {
    if ( auto f = tasks.try_dequeue() )
    {
      std::move( *f )();
    }
  }

private:
  void process_tasks( std::stop_token s )
  {
    while ( !s.stop_requested() )
    {
      tasks.dequeue()();
    }

    while ( true )
    {
      if ( auto f = tasks.try_dequeue() )
        std::move( *f )();
      else
        break;
    }
  }

private:
  concurrent_bounded_queue<ofats::any_invocable<void()>, QueueDepth> tasks;
  thread_group threads;
};

} // sandbox
