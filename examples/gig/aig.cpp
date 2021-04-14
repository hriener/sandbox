#include <mockturtle/aig.hpp>
#include <mockturtle/cut.hpp>
#include <iostream>

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
