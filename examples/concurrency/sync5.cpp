#include <sandbox/concurrent_thread_manager.hpp>

int main()
{
  std::atomic<std::uint64_t> count( 0 );
  {
    sandbox::bounded_depth_task_manager<64> tm( 6 );
    for ( auto i : std::views::iota( 0, 256 ) )
    {
      tm.submit( [&]{ ++count; } );
    }
  }

  std::cout << count << '\n';
  
  return 0;
}
