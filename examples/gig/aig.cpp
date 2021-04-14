#include <mockturtle/aig.hpp>
#include <iostream>

namespace aig
{

bool trivial( aig::network const& aig, std::vector<node> const& cut )
{
  for( node const& n : cut )
  {
    if ( !aig.is_constant( n ) && !aig.is_pi( n ) )
    {
      return false;
    }
  }
  return true;
}

void print_cut( std::vector<node> const& cut, std::ostream& os = std::cout )
{
  os << "{ ";
  for ( aig::node const& n : cut )
  {
    os << n << ' ';
  }
  os << "}" << std::endl;
}

bool expand0( network const& aig, std::vector<node>& cut, uint32_t thread_id )
{
  /* presume the current cut is trivial (= consists of PIs only) */
  bool is_trivial{true};
  bool cut_has_changed{true};

  /* temporary container to store newly derived leaves */
  std::vector<node> new_cut_nodes;
  new_cut_nodes.reserve( 16 );

  /* repeat expansion towards TFI until a fix-point is reached */
  while ( cut_has_changed )
  {
    is_trivial = true;
    cut_has_changed = false;

    for ( auto it = std::begin( cut ); it != std::end( cut ); )
    {
      assert( !aig.is_constant( *it ) );
      assert( aig.mark( *it ) == thread_id );

      /* skip the PIs */
      if ( aig.is_pi( *it ) )
      {
        ++it;
        continue;
      }

      /* at least one node is not a PI */
      is_trivial = false;

      /* count how many fanins of this node are already in the cut */
      std::optional<node> expansion_point;
      uint32_t count_fanin_inside{0};
      aig.foreach_fanin( *it, [&]( signal const& fi ){
        node const n = aig.get_node( fi );
        if ( aig.mark( n ) == thread_id )
        {
          ++count_fanin_inside;
        }
        else
        {
          expansion_point = n;
        }
      });

      /* if the expansion is not cost-free, then proceeded with the next leaf */
      if ( count_fanin_inside + 1 < aig.fanin_size( *it ) )
      {
        ++it;
        continue;
      }

      if ( expansion_point )
      {
        if ( aig.check_and_mark( *expansion_point, thread_id ) )
        {
          new_cut_nodes.emplace_back( *expansion_point );
        }
      }

      it = cut.erase( it );
      cut_has_changed = true;
    }

    for ( node const& n : new_cut_nodes )
    {
      cut.push_back( n );
    }

    new_cut_nodes.clear();
  }
  return is_trivial;
}

void evaluate_fanin( node const& n, std::vector<std::pair<node, uint32_t>>& candidates )
{
  auto it = std::find_if( std::begin( candidates ), std::end( candidates ),
                          [&n]( auto const& p ){
                            return p.first == n;
                          } );
  if ( it == std::end( candidates ) )
  {
    /* new fanin: referenced for the 1st time */
    candidates.push_back( std::make_pair( n, 1u ) );
  }
  else
  {
    /* otherwise, if not new, then just increase the reference counter */
    ++it->second;
  }
}

node select_next_fanin( aig::network const& aig, std::vector<node> const& cut )
{
  assert( cut.size() > 0u && "cut must not be empty" );
  assert( !trivial( aig, cut ) );

  /* evaluate the fanins with respect to their costs (how often are they referenced) */
  std::vector<std::pair<node, uint32_t>> candidates;
  for ( node const& n : cut )
  {
    if ( aig.is_constant( n ) || aig.is_pi( n ) )
    {
      continue;
    }

    aig.foreach_fanin( n, [&]( signal const& fi ){
      if ( aig.is_constant( aig.get_node( fi ) ) )
      {
        return true;
      }
      evaluate_fanin( aig.get_node( fi ), candidates );
      return true;
    });
  }

  assert( candidates.size() > 0u );

  std::pair<node, uint32_t> best_fanin{candidates[0]};
  for ( auto const& candidate : candidates )
  {
    if ( candidate.second > best_fanin.second ||
         ( candidate.second == best_fanin.second && aig.fanout_size( candidate.first ) > aig.fanout_size( best_fanin.first ) ) )
    {
      best_fanin = candidate;
    }
  }

  assert( best_fanin.first != 0 );
  return best_fanin.first;
}

void expand( aig::network const& aig, std::vector<node>& cut, uint32_t size_limit, uint32_t thread_id )
{
  static constexpr uint32_t const MAX_ITERATIONS{5u};
  if ( expand0( aig, cut, thread_id ) )
  {
    return;
  }

  std::optional<std::vector<node>> best_cut;
  if ( cut.size() <= size_limit )
  {
    *best_cut = cut;
  }

  bool trivial_cut{false};
  uint32_t iterations{0};
  while ( !trivial_cut && ( cut.size() <= size_limit || iterations < MAX_ITERATIONS ) )
  {
    node const n = select_next_fanin( aig, cut );
    if ( aig.check_and_mark( n, thread_id ) )
    {
      cut.push_back( n );
    }

    trivial_cut = expand0( aig, cut, thread_id );
    assert( trivial_cut == trivial( aig, cut ) );

    iterations = cut.size() > size_limit ? iterations + 1 : 0;
    if ( cut.size() <= size_limit &&
         ( !best_cut || best_cut->size() <= size_limit ) )
    {
      best_cut = cut;
    }
  }

  if ( best_cut )
  {
    cut = *best_cut;
  }
  else
  {
    assert( cut.size() > size_limit );
  }
}

std::vector<node> create_cut( network const& aig, node n, uint32_t thread_id )
{
  /* check if the node is not owned by another thread and mark it */
  if ( !aig.check_and_mark( n, thread_id ) )
  {
    return {};
  }

  std::vector<node> cut{n};
  expand( aig, cut, 6u, thread_id );

  return cut;
}

std::vector<node> create_cut( network const& aig, signal s, uint32_t thread_id )
{
  return create_cut( aig, aig.get_node( s ), thread_id );
}

void release_cut( network const& aig, node n, std::vector<node> const& cut, uint32_t thread_id )
{
  if ( aig.mark ( n ) != thread_id )
  {
    return;
  }

  aig.reset_mark( n );
  aig.foreach_fanin( n, [&]( signal const& s ){
    release_cut( aig, aig.get_node( s ), cut, thread_id );
  });
}

} /* aig */

void test()
{
  assert( sizeof( aig::storage::node_type ) == 16 );

  aig::storage store;
  aig::network aig( store );
  auto const x0 = aig.create_pi();
  auto const x1 = aig.create_pi();
  auto const x2 = aig.create_pi();
  auto const n3 = aig.create_and( x0, x1 );
  auto const n4 = aig.create_and( x1, x2 );
  auto const n5 = aig.create_and( n3, n4 );
  aig.create_po( n5 );

  /* mark nodes */
  auto const cut0 = create_cut( aig, n5, 1u );
  print_cut( cut0 );

  auto const cut1 = create_cut( aig, n5, 2u );
  print_cut( cut1 );

  auto const cut2 = create_cut( aig, n5, 1u );
  print_cut( cut2 );
  release_cut( aig, aig.get_node( n5 ), cut0, 1u );

  auto const cut3 = create_cut( aig, n5, 2u );
  print_cut( cut3 );
  release_cut( aig, aig.get_node( n5 ), cut3, 2u );
}

int main()
{
  test();
  return 0;
}
