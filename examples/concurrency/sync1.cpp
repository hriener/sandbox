#include <concepts>
#include <thread>
#include <vector>
#include <iostream>

namespace stdv = std::views;

struct thread_group
{
public:
  thread_group( std::uint64_t n, std::invocable auto&& f )
  {
    for ( auto i : stdv::iota( 0ul, n ) )
    {
      members.emplace_back( f );
    }
  }

private:
  std::vector<std::jthread> members;
};

int main()
{
  std::atomic<std::uint64_t> count( 0 );
  {
    thread_group tg( 6, [&]{ ++count; } );
  }
  std::cout << count << '\n';
  
  return 0;
}
