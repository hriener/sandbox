/* lorina: C++ parsing library
 * Copyright (C) 2018-2021  EPFL
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*!
  \file diagnostics.hpp
  \brief Implements diagnostics.

  \author Heinz Riener
*/

#pragma once

#include <fmt/color.h>
#include <map>
#include <iostream>
#include <cassert>

namespace lorina
{

class diagnostic_builder;
class diagnostic_engine;

enum class diagnostic_level
{
  ignore = 0,
  note,
  remark,
  warning,
  error,
  fatal,
};

/*! \brief A builder for diagnostics.
 *
 * An object that encapsulates a diagnostic.  The diagnostic may take
 * additional parameters and is finally issued at the end of its
 * life time.
 */
class diagnostic_builder
{
public:
  /*! \brief Constructs a diagnostic builder.
   *
   * \param diag Diagnostic engine
   * \param level Severity level
   * \param message Diagnostic message
   */
  inline explicit diagnostic_builder( diagnostic_engine& diag, diagnostic_level level, const std::string& message );

  /*! \brief Destructs the diagnostic builder and issues the diagnostic. */
  inline ~diagnostic_builder();

  diagnostic_engine& _diag; /*!< diagnostic engine */
  diagnostic_level _level; /*!< diagnostic level */
  std::string _message; /*!< diagnostic message */
}; /* diagnostic_builder */

/*! \brief A diagnostic engine. */
class diagnostic_engine
{
public:
  /*! \brief Creates a diagnostic builder.
   *
   * \param level Severity level
   * \param message Diagnostic message
   */
  virtual inline diagnostic_builder report( diagnostic_level level, const std::string& message );

  /*! \brief Emits a diagnostic message.
   *
   * \param level Severity level
   * \param message Diagnostic message
   */
  virtual inline void emit( diagnostic_level level, const std::string& message ) const;

public:
  mutable unsigned number_of_diagnostics = 0; /*!< Number of diagnostics constructed via report */
}; /* diagnostic_engine */

diagnostic_builder::diagnostic_builder( diagnostic_engine& diag, diagnostic_level level, const std::string& message )
    : _diag( diag ), _level( level ), _message( message )
{
}

diagnostic_builder::~diagnostic_builder()
{
  _diag.emit( _level, _message );
}

diagnostic_builder diagnostic_engine::report( diagnostic_level level, const std::string& message )
{
  ++number_of_diagnostics;
  return diagnostic_builder( *this, level, message );
}

void diagnostic_engine::emit( diagnostic_level level, const std::string& message ) const
{

  switch ( level )
  {
  case diagnostic_level::ignore:
    break;
  case diagnostic_level::note:
    fmt::print( stderr, fmt::emphasis::bold | fg( fmt::color::red ), "[i] " );
    fmt::print( stderr, "{}\n", message );
    break;
  case diagnostic_level::remark:
    fmt::print( stderr, fmt::emphasis::bold | fg( fmt::color::red ), "[i] " );
    fmt::print( stderr, "{}\n", message );
    break;
  case diagnostic_level::warning:
    fmt::print( stderr, fmt::emphasis::bold | fg( fmt::color::red ), "[w] " );
    fmt::print( stderr, "{}\n", message );
    break;
  case diagnostic_level::error:
    fmt::print( stderr, fmt::emphasis::bold | fg( fmt::color::red ), "[e] " );
    fmt::print( stderr, "{}\n", message );
    break;
  case diagnostic_level::fatal:
  default:
    fmt::print( stderr, fmt::emphasis::bold | fg( fmt::color::red ), "[E] " );
    fmt::print( stderr, "{}\n", message );
    break;
  }
}

class silent_diagnostic_engine : public diagnostic_engine
{
public:
  virtual void emit( diagnostic_level level, const std::string& message ) const override
  {
    (void)level;
    (void)message;
  }
}; /* silent_diagnostic_engine */

} // namespace lorina
