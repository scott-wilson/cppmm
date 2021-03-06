#pragma once

#include <vector>
#include <string>

#include <clang/AST/DeclCXX.h>

namespace cppmm {

void add_namespace_rename(const std::string& from, const std::string& to);

const std::string& rename_namespace(const std::string& in);

const std::string
prefix_from_namespaces(const std::vector<std::string>& cpp_namespaces,
                       const std::string& sep);

bool match_namespaces(const std::vector<std::string>& a,
                      const std::vector<std::string>& b);

std::vector<std::string> get_namespaces(const clang::DeclContext* parent);
}
