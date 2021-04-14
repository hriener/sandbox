#pragma once

#include <mockturtle/aig.hpp>
#include <lorina/verilog.hpp>
#include <unordered_map>

namespace aig
{

class verilog_reader : public lorina::verilog_reader
{
public:
  explicit verilog_reader( aig::network& aig )
    : aig_( aig )
  {}

  void on_inputs( const std::vector<std::string>& names, std::string const& size = "" ) const override
  {
    assert( size.empty() );
    for ( const auto& name : names )
    {
      signals_[name] = aig_.create_pi();
    }
  }

  void on_outputs( const std::vector<std::string>& names, std::string const& size = "" ) const override
  {
    assert( size.empty() );
    for ( const auto& name : names )
    {
      outputs_.emplace_back( name );
    }
  }

  void on_endmodule() const override
  {
    for ( auto const& o : outputs_ )
    {
      aig_.create_po( signals_[o] );
    }
  }

  void on_assign( const std::string& lhs, const std::pair<std::string, bool>& rhs ) const override
  {
    if ( signals_.find( rhs.first ) == std::end( signals_ )  )
    {
      fmt::print( stderr, "[w] undefined signal {} assigned 0\n", rhs.first );
    }

    auto const r = signals_[rhs.first];
    signals_[lhs] = rhs.second ? aig_.create_not( r ) : r;
  }

  void on_and( const std::string& lhs, const std::pair<std::string, bool>& op1, const std::pair<std::string, bool>& op2 ) const override
  {
    if ( signals_.find( op1.first ) == std::end( signals_ ) )
    {
      fmt::print( stderr, "[w] undefined signal {} assigned 0\n", op1.first );
    }
    if ( signals_.find( op2.first ) == std::end( signals_ ) )
    {
      fmt::print( stderr, "[w] undefined signal {} assigned 0\n", op2.first );
    }

    auto const a = signals_[op1.first];
    auto const b = signals_[op2.first];
    signals_[lhs] = aig_.create_and( op1.second ? aig_.create_not( a ) : a, op2.second ? aig_.create_not( b ) : b );
  }

private:
  aig::network& aig_;
  mutable std::unordered_map<std::string, aig::signal> signals_;
  mutable std::vector<std::string> outputs_;
};

} /* namespace aig */
