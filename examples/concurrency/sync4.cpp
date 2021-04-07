#include <atomic>
#include <cstddef>

#ifdef __cpp_lib_hardware_interference_size
    using std::hardware_constructive_interference_size;
    using std::hardware_destructive_interference_size;
#else
    // 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │ ...
    constexpr std::size_t hardware_constructive_interference_size
        = 2 * sizeof(std::max_align_t);
    constexpr std::size_t hardware_destructive_interference_size
    = 2 * sizeof(std::max_align_t);
#endif

#if 0
struct ticket_mutex
{
public:
  void lock()
  {
    auto const my = in.fetch_add( 1, std::memory_order_acquire );
    while ( true )
    {
      auto const now = out.load( std::memory_order_acquire );
      if ( now == my )
      {
        return;
      }
      out.wait( now, std::memory_order_relaxed );
    }
  }

  void unlock()
  {
    out.fetch_add( 1, std::memory_order_release );
    out.notify_all();
  }
  
private:
  alignas( hardware_destructive_interference_size )
    std::atomic<int> in = ATOMIC_VAR_INIT( 0 );
  alignas( hardware_destructive_interference_size )
    std::atomic<int> out = ATOMIC_VAR_INIT( 0 );
}; /* ticket_mutex */
#endif

int main()
{
  return 0;
}
