#include <mockturtle/aig.hpp>
#include <iostream>

using namespace mockturtle;

int main()
{
  aig_network aig;
  auto const a = aig.create_pi();
  auto const b = aig.create_pi();
  auto const c = aig.create_pi();

  auto const n0 = aig.create_and( a, b );
  auto const n1 = aig.create_and( n0, c );
  aig.create_po( n1 );

  aig.foreach_node( [&]( auto n ){
    std::cout << "node: " << n << std::endl;
    aig.foreach_fanin( n, [&]( auto fi, auto index ){
      std::cout << "  index: " << index << " fanin: " << ( aig.is_complemented( fi ) ? "-" : "+" ) << aig.get_node( fi ) << std::endl;
    });
  });

  aig.foreach_po( [&]( auto f ){
    std::cout << "output: " << ( aig.is_complemented( f ) ? "-" : "+" ) << ' ' << aig.get_node( f ) << std::endl;
  });

  return 0;
}
