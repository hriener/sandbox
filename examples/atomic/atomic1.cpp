#include <array>
#include <atomic>
#include <vector>
#include <iostream>

void example1()
{
  std::atomic<long> value( 0 );
  ++value;
  value += 5;
}

struct struct_with_atomic_data
{
  std::atomic<uint32_t> a;
  uint32_t b;
};

void example2()
{
  struct_with_atomic_data d;
  d.a = 42;
  d.b = 42;
}

struct node_type
{
  std::array<uint32_t, 2u> fanins;
  uint32_t fanout_size{};
  std::atomic<uint32_t> value;
  uint32_t visited{};
  uint32_t level{};
}; /* node_type */

void example3()
{
  std::vector<node_type> nodes;
}

void example4( uint32_t thread_id = 42 )
{
  std::atomic<uint32_t> traversal_id{0};
  uint32_t current = 0;
  if ( !traversal_id.compare_exchange_weak( current, thread_id ) )
  {
    std::cout << "failed" << std::endl;
  }
  else
  {
    std::cout << "success" << std::endl;
  }

  std::cout << traversal_id << std::endl;
}

int main()
{
  example4();
  return 0;
}
