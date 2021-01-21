#include <iostream>

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ostr.h>

#include "method.hpp"
#include "namespaces.hpp"
#include "param.hpp"

#include "pystring.h"

namespace cppmm {

Method::Method(std::string cpp_name, std::string c_name, QualifiedType return_type,
               std::vector<Param> params, std::string comment,
               std::vector<std::string> namespaces, bool is_const,
               bool is_static, bool is_constructor, bool is_copy_constructor,
               bool is_copy_assignment, bool is_operator,
               bool is_conversion_operator, std::string op, std::vector<std::string> template_args)
    : Function(cpp_name, c_name, return_type, params, comment, namespaces, "", template_args),
      is_const(is_const), is_static(is_static), is_constructor(is_constructor),
      is_copy_constructor(is_copy_constructor),
      is_copy_assignment(is_copy_assignment), is_operator(is_operator),
      is_conversion_operator(is_conversion_operator), op(op) {}

} // namespace cppmm
