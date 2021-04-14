#include <sandbox/concurrent_thread_manager.hpp>

#include <mockturtle/aig.hpp>
#include <mockturtle/cut.hpp>
#include <mockturtle/verilog_reader.hpp>
#include <lorina/verilog.hpp>
#include <iostream>

int main()
{
  aig::storage store;
  aig::network aig( store );

  lorina::diagnostic_engine diag;
  if ( lorina::read_verilog( "voter.v", aig::verilog_reader( aig ), &diag ) != lorina::return_code::success )
  {
    std::cerr << "parsing failed" << std::endl;
    return EXIT_FAILURE;
  }
  else
  {
    std::cout << "parsing successful" << std::endl;
  }

#if 1
  {
    /* sequentially iterate over all nodes */
    aig.foreach_node( [&]( aig::node n ){
      if ( aig.is_constant( n ) )
        return;

      auto cut = aig::create_cut( aig, n, 1u );
      print_cut( cut );
      aig::release_cut( aig, n, cut, 1u );
      return;
    });
  }
#endif

#if 0
  {
    /* concurrently iterate over all nodes */
    sandbox::bounded_depth_task_manager<64> tm( 6 );
    aig.foreach_node( [&]( aig::node n ){
      if ( aig.is_constant( n ) )
        return;

      tm.submit( [&]{
        auto cut = aig::create_cut( aig, n, 1u );
        print_cut( cut );
        aig::release_cut( aig, n, cut, 1u );
      });

      return;
    });
  }
#endif

  return EXIT_SUCCESS;
}
