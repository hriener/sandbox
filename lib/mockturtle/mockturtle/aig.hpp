#pragma once

#include "foreach.hpp"

#include <cstdint>
#include <array>
#include <vector>

#include <parallel_hashmap/phmap.h>

namespace mockturtle
{

struct node_pointer
{
public:
  node_pointer() = default;
  node_pointer( uint32_t index, uint32_t weight )
    : weight( weight ), index( index )
  {}

  union
  {
    struct
    {
      uint32_t weight : 1;
      uint32_t index : 31;
    };
    uint32_t data;
  };

  bool operator==( node_pointer const& other ) const
  {
    return data == other.data;
  }
}; /* node_pointer */

struct signal
{
  signal() = default;
  signal( uint32_t index, bool complement = false )
    : index( index )
    , complement( complement )
  {}

  signal( node_pointer const& p )
    : complement( p.weight ), index( p.index )
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

  operator node_pointer() const
  {
    return {index, complement};
  }

#if __cplusplus > 201703L
  bool operator==( node_pointer const& other ) const
  {
    return data == other.data;
  }
#endif

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
    std::array<node_pointer, 2u> fanins;
    uint32_t fanout_size{};
    uint32_t value{};
    uint32_t visited{};
    uint32_t level{};
  }; /* node_type */

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
      seed += n.fanins[0].weight * 911;
      seed += n.fanins[1].weight * 353;
      return seed;
    }
  };

  std::vector<node_type> nodes;
  std::vector<uint32_t> inputs;
  std::vector<signal> outputs;
  phmap::flat_hash_map<node_type, uint32_t, aig_node_hash, aig_node_eq> hash;

  uint32_t trav_id = 0u;
  uint32_t depth = 0u;
}; /* storage */

class aig_network
{
public:
  using node = uint32_t;

  node get_node( signal const& f ) const
  {
    return f.index;
  }

  signal make_signal( node const& n ) const
  {
    return signal( n, 0 );
  }

  bool is_complemented( signal const& f ) const
  {
    return f.complement;
  }

  bool is_pi( node const& n ) const
  {
    return storage_.nodes[n].fanins[0].data == storage_.nodes[n].fanins[1].data &&
      storage_.nodes[n].fanins[0].data < static_cast<uint32_t>( storage_.inputs.size() );
  }

  signal get_constant( bool value ) const
  {
    return {0, value};
  }

  signal create_pi( std::string const& name = std::string() )
  {
    (void)name;

    const auto index = static_cast<uint32_t>( storage_.nodes.size() );
    storage_.nodes.emplace_back();
    storage_.inputs.emplace_back( index );
    return {index, 0};
  }

  node create_po( signal const& f, std::string const& name = std::string() )
  {
    (void)name;

    /* increase ref-count to fanins */
    storage_.nodes[f.index].fanout_size++;
    auto const po_index = static_cast<uint32_t>( storage_.outputs.size() );
    storage_.outputs.emplace_back( f.index, f.complement );
    storage_.depth = std::max( storage_.depth, level( f.index ) );
    return po_index;
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
    const auto it = storage_.hash.find( node );
    if ( it != storage_.hash.end() )
    {
      // assert( !is_dead( it->second ) );
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
    storage_.nodes[a.index].fanout_size++;
    storage_.nodes[b.index].fanout_size++;

    // for ( auto const& fn : _events->on_add )
    // {
    //   fn( index );
    // }

    return {index, 0};
  }

  uint32_t depth() const
  {
    return storage_.depth;
  }

  uint32_t level( node const& n ) const
  {
    return storage_.nodes[n].level;
  }

  template<typename Fn>
  void foreach_node( Fn&& fn ) const
  {
    auto r = detail::range<uint32_t>( storage_.nodes.size() );
    detail::foreach_element( r.begin(), r.end(), fn );
  }

  template<typename Fn>
  void foreach_fanin( node const& n, Fn&& fn ) const
  {
    if ( n == 0 || is_pi( n ) )
      return;

    static_assert( detail::is_callable_without_index_v<Fn, signal, bool> ||
                   detail::is_callable_with_index_v<Fn, signal, bool> ||
                   detail::is_callable_without_index_v<Fn, signal, void> ||
                   detail::is_callable_with_index_v<Fn, signal, void> );

    /* we don't use foreach_element here to have better performance */
    if constexpr ( detail::is_callable_without_index_v<Fn, signal, bool> )
    {
      if ( !fn( signal{storage_.nodes[n].fanins[0]} ) )
        return;
      fn( signal{storage_.nodes[n].fanins[1]} );
    }
    else if constexpr ( detail::is_callable_with_index_v<Fn, signal, bool> )
    {
      if ( !fn( signal{storage_.nodes[n].fanins[0]}, 0 ) )
        return;
      fn( signal{storage_.nodes[n].fanins[1]}, 1 );
    }
    else if constexpr ( detail::is_callable_without_index_v<Fn, signal, void> )
    {
      fn( signal{storage_.nodes[n].fanins[0]} );
      fn( signal{storage_.nodes[n].fanins[1]} );
    }
    else if constexpr ( detail::is_callable_with_index_v<Fn, signal, void> )
    {
      fn( signal{storage_.nodes[n].fanins[0]}, 0 );
      fn( signal{storage_.nodes[n].fanins[1]}, 1 );
    }
  }

  template<typename Fn>
  void foreach_po( Fn&& fn ) const
  {
    detail::foreach_element( storage_.outputs.begin(), storage_.outputs.begin() + storage_.outputs.size(), fn );
  }

public:
  storage storage_;
}; /* aig_network */

} /* mockturtle */
