#pragma once

#include "foreach.hpp"
#include <parallel_hashmap/phmap.h>

#include <array>
#include <atomic>
#include <vector>

namespace aig
{

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
    return *this;
  }

  std::atomic<T> data;
}; /* atomic_wrapper */

struct node
{
  node() = default;
  explicit node( uint32_t value )
    : value( value )
  {}

  operator uint32_t() const
  {
    return value;
  }

  uint32_t value;
}; /* node */

struct signal
{
  signal() = default;
  signal( uint32_t index, bool complement = false )
    : index( index )
    , complement( complement )
  {}

  signal operator!() const
  {
    return {index, !complement};
  }

  signal operator+() const
  {
    return {index, false};
  }

  signal operator-() const
  {
    return {index, true};
  }

  signal operator^( bool complement ) const
  {
    return {index, static_cast<bool>( this->complement ^ complement )};
  }

  bool operator==( signal const& other ) const
  {
    return index == other.index && complement == other.complement;
  }

  bool operator!=( signal const& other ) const
  {
    return !( *this == other );
  }

  bool operator<( signal const& other ) const
  {
    return index < other.index || ( index == other.index && !complement && other.complement );
  }

  union {
    struct
    {
      uint32_t complement : 1;
      uint32_t index : 31;
    };
    uint32_t data;
  };
}; /* signal */

class storage
{
public:
  struct node_type
  {
    std::array<signal, 2u> fanins;    // 8 bytes
    atomic_wrapper<uint32_t> value{}; // 4 bytes
    uint32_t ref_count{};             // 4 bytes
  }; /* node_type (16 bytes) */

  storage()
  {
    /* constant 0 node */
    nodes.emplace_back();
  }

  struct aig_node_eq
  {
    bool operator()( node_type const& a, node_type const& b ) const
    {
      return a.fanins == b.fanins;
    }
  };

  struct aig_node_hash
  {
    uint64_t operator()( node_type const& n ) const
    {
      uint64_t seed = -2011;
      seed += n.fanins[0].index * 7939;
      seed += n.fanins[1].index * 2971;
      seed += n.fanins[0].complement * 911;
      seed += n.fanins[1].complement * 353;
      return seed;
    }
  };

  std::vector<node_type> nodes;
  std::vector<uint32_t> inputs;
  std::vector<signal> outputs;
  phmap::flat_hash_map<node_type, uint32_t, aig_node_hash, aig_node_eq> hash;
}; /* storage */

class network
{
public:
  explicit network( storage& storage_ )
    : storage_( storage_ )
  {
  }

  network( network const& other )
    : storage_( other.storage_ )
  {}

  network& operator=( network const& other )
  {
    storage_ = other.storage_;
    return *this;
  }

  node get_node( signal f ) const
  {
    return node( f.index );
  }

  signal make_signal( node n ) const
  {
    return signal( n, 0 );
  }

  bool is_complemented( signal f ) const
  {
    return f.complement;
  }

  bool is_constant( node n ) const
  {
    return n == 0;
  }

  bool is_pi( node n ) const
  {
    return storage_.nodes[n].fanins[0].data == storage_.nodes[n].fanins[1].data &&
      storage_.nodes[n].fanins[0].data < static_cast<uint32_t>( storage_.inputs.size() );
  }

  signal get_constant( bool value ) const
  {
    return {0, value};
  }

  signal create_pi()
  {
    uint32_t const index = static_cast<uint32_t>( storage_.nodes.size() );
    storage_.nodes.emplace_back();
    storage_.inputs.emplace_back( index );
    return {index, 0};
  }

  signal create_and( signal a, signal b )
  {
    /* order inputs */
    if ( a.index > b.index )
    {
      std::swap( a, b );
    }

    /* trivial cases */
    if ( a.index == b.index )
    {
      return ( a.complement == b.complement ) ? a : get_constant( false );
    }
    else if ( a.index == 0 )
    {
      return a.complement ? b : get_constant( false );
    }

    storage::node_type node;
    node.fanins[0] = a;
    node.fanins[1] = b;

    /* structural hashing */
    auto const it = storage_.hash.find( node );
    if ( it != storage_.hash.end() )
    {
      return {it->second, 0};
    }

    uint32_t const index = storage_.nodes.size();
    if ( index >= .9 * storage_.nodes.capacity() )
    {
      storage_.nodes.reserve( static_cast<uint64_t>( 3.1415f * index ) );
      storage_.hash.reserve( static_cast<uint64_t>( 3.1415f * index ) );
    }

    storage_.nodes.push_back( node );
    storage_.hash[node] = index;

    /* increase ref-count to children */
    storage_.nodes[a.index].ref_count++;
    storage_.nodes[b.index].ref_count++;

    return {index, 0};
  }

  void create_po( signal const& f )
  {
    /* increase ref-count to fanins */
    storage_.nodes[f.index].ref_count++;
    storage_.outputs.emplace_back( f.index, f.complement );
  }

  template<typename Fn>
  void foreach_node( Fn&& fn ) const
  {
    auto r = mockturtle::detail::range<uint32_t>( storage_.nodes.size() );
    mockturtle::detail::foreach_element( r.begin(), r.end(), fn );
  }

  template<typename Fn>
  void foreach_fanin( node const& n, Fn&& fn ) const
  {
    if ( n == 0 || is_pi( n ) )
      return;

    static_assert( mockturtle::detail::is_callable_without_index_v<Fn, signal, bool> ||
                   mockturtle::detail::is_callable_with_index_v<Fn, signal, bool> ||
                   mockturtle::detail::is_callable_without_index_v<Fn, signal, void> ||
                   mockturtle::detail::is_callable_with_index_v<Fn, signal, void> );

    /* we don't use foreach_element here to have better performance */
    if constexpr ( mockturtle::detail::is_callable_without_index_v<Fn, signal, bool> )
    {
      if ( !fn( signal{storage_.nodes[n].fanins[0]} ) )
        return;
      fn( signal{storage_.nodes[n].fanins[1]} );
    }
    else if constexpr ( mockturtle::detail::is_callable_with_index_v<Fn, signal, bool> )
    {
      if ( !fn( signal{storage_.nodes[n].fanins[0]}, 0 ) )
        return;
      fn( signal{storage_.nodes[n].fanins[1]}, 1 );
    }
    else if constexpr ( mockturtle::detail::is_callable_without_index_v<Fn, signal, void> )
    {
      fn( signal{storage_.nodes[n].fanins[0]} );
      fn( signal{storage_.nodes[n].fanins[1]} );
    }
    else if constexpr ( mockturtle::detail::is_callable_with_index_v<Fn, signal, void> )
    {
      fn( signal{storage_.nodes[n].fanins[0]}, 0 );
      fn( signal{storage_.nodes[n].fanins[1]}, 1 );
    }
  }

  bool check_and_mark( node n, uint32_t new_value ) const
  {
    uint32_t current{storage_.nodes[n].value.data.load()};
    return ( current == new_value ) ||
      ( current == 0u && storage_.nodes[n].value.data.compare_exchange_weak( current, new_value ) );
    // uint32_t current{0};
    // return storage_.nodes[n].value.data.compare_exchange_weak( current, new_value );    
  }

  void reset_mark( node n ) const
  {
    storage_.nodes[n].value.data.store( 0u );
  }

  uint32_t mark( node n ) const
  {
    return storage_.nodes[n].value.data.load();
  }

  uint32_t fanin_size( node n ) const
  {
    return ( is_constant( n ) || is_pi( n ) ) ? 0u : 2u;
  }

  uint32_t fanout_size( node n ) const
  {
    return storage_.nodes[n].ref_count;
  }

protected:
  storage& storage_;
}; /* network */

} /* aig */
