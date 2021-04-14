#include <array>
#include <atomic>
#include <vector>
#include <iostream>

template <typename T>
struct atomic_wrapper
{
  atomic_wrapper()
    : data()
  {}

  atomic_wrapper( std::atomic<T> const &a )
    : data( a.load() )
  {}

  atomic_wrapper( atomic_wrapper const &other )
    : data( other.data.load() )
  {}

  atomic_wrapper &operator=( atomic_wrapper const &other )
  {
    data.store( other.data.load() );
  }

  std::atomic<T> data;
};

struct A
{
  uint32_t a;
  uint32_t b;
};

void test()
{
  std::vector<atomic_wrapper<A>> as;
  as.emplace_back();
}

int main()
{
  test();
  return 0;
}
