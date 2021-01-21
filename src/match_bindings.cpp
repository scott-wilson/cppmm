#include "match_bindings.hpp"
#include "attributes.hpp"
#include "namespaces.hpp"
#include "pystring.h"

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ostr.h>

using namespace clang;
using namespace clang::ast_matchers;
namespace ps = pystring;

namespace cppmm {
std::vector<AttrDesc> get_attrs(const clang::Decl* decl) {
    std::vector<AttrDesc> attrs;
    if (decl->hasAttrs()) {
        clang::ASTContext& ctx = decl->getASTContext();
        for (const auto& attr : decl->attrs()) {
            const clang::AnnotateAttr* ann =
                clang::cast<const clang::AnnotateAttr>(attr);
            if (ann) {
                if (auto opt = parse_attributes(ann->getAnnotation().str())) {
                    attrs.push_back(*opt);
                }
            }
        }
    }

    return attrs;
}

void MatchBindingsCallback::run(const MatchFinder::MatchResult& result) {
    if (const TypeAliasDecl* tdecl =
            result.Nodes.getNodeAs<TypeAliasDecl>("typeAliasDecl")) {
        handle_typealias(tdecl);
    } else if (const CXXRecordDecl* record =
                   result.Nodes.getNodeAs<CXXRecordDecl>("recordDecl")) {
        handle_record(record);
    } else if (const EnumDecl* enum_decl =
                   result.Nodes.getNodeAs<EnumDecl>("enumDecl")) {
        handle_enum(enum_decl);
    } else if (const FunctionDecl* function =
                   result.Nodes.getNodeAs<FunctionDecl>("functionDecl")) {
        handle_function(function);
    }
}

void MatchBindingsCallback::handle_typealias(const TypeAliasDecl* alias) {
    // fmt::print("GOT typeAliasDecl: {}\n", alias->getNameAsString());
    const QualType qtype = alias->getUnderlyingType();
    if (const CXXRecordDecl* record = qtype->getAsCXXRecordDecl()) {
        if (isa<ClassTemplateSpecializationDecl>(record)) {
            const ClassTemplateSpecializationDecl* ctsd =
                cast<ClassTemplateSpecializationDecl>(record);
            ClassTemplateDecl* ctd = ctsd->getSpecializedTemplate();
            CXXRecordDecl* scrd = ctd->getTemplatedDecl();

            std::string record_cpp_qname =
                prefix_from_namespaces(get_namespaces(scrd->getParent()),
                                       "::") +
                scrd->getNameAsString();

            if (ex_records.find(record_cpp_qname) == ex_records.end()) {
                // we only want to process specializations for types we care
                // about
                return;
            }

            // fmt::print("record is CTSD\n");
            std::vector<std::string> typelist;
            std::vector<std::string> template_arg_names;
            std::unordered_map<std::string, std::string> template_named_args;
            for (const auto& targ : ctsd->getTemplateArgs().asArray()) {
                if (!targ.getAsType()->isBuiltinType()) {
                    throw std::runtime_error(fmt::format(
                        "template argument {} on {} is not a builtin.",
                        targ.getAsType().getAsString(),
                        record->getNameAsString()));
                }
                typelist.push_back(targ.getAsType().getAsString());
            }

            for (const auto* tp : ctd->getTemplateParameters()->asArray()) {
                template_arg_names.push_back(tp->getNameAsString());
            }

            if (typelist.size() != template_arg_names.size()) {
                throw std::runtime_error("template args differnt sizes");
            }
            for (int i = 0; i < typelist.size(); ++i) {
                template_named_args[template_arg_names[i]] = typelist[i];
            }

            ExportedSpecialization ex_spec;
            ex_spec.record_cpp_qname;
            ex_spec.template_args = typelist;
            ex_spec.template_named_args = template_named_args;
            ex_spec.alias = alias->getNameAsString();
            ex_spec.record_cpp_qname = record_cpp_qname;
            ex_specs[record_cpp_qname].push_back(ex_spec);
        }
    }
}

void MatchBindingsCallback::handle_enum(const EnumDecl* enum_decl) {
    const auto ns = get_namespaces(enum_decl->getParent());
    std::vector<std::pair<std::string, uint64_t>> enumerators;

    ASTContext& ctx = enum_decl->getASTContext();
    SourceManager& sm = ctx.getSourceManager();
    const auto& loc = enum_decl->getLocation();
    std::string filename = sm.getFilename(loc).str();

    ExportedEnum ex_enum;
    ex_enum.cpp_name = enum_decl->getNameAsString();
    ex_enum.c_name = ex_enum.cpp_name;
    ex_enum.c_qname = prefix_from_namespaces(ns, "_") + ex_enum.c_name;
    ex_enum.cpp_qname = prefix_from_namespaces(ns, "::") + ex_enum.cpp_name;

    // fmt::print("Found enum: {}\n", ex_enum.c_qname);
    ex_enum.namespaces = ns;
    ex_enum.filename = filename;

    if (ex_enums.find(ex_enum.cpp_qname) != ex_enums.end()) {
        SPDLOG_WARN("Ignoring duplicate definition for {}\n", ex_enum.c_qname);
        return;
    }

    ex_enums[ex_enum.cpp_qname] = ex_enum;

    if (ex_files.find(filename) == ex_files.end()) {
        ex_files[filename] = {};
    }

    auto& ex_file = ex_files[filename];
    if (ex_file.enums.find(ex_enum.cpp_qname) == ex_file.enums.end()) {
        ex_file.enums[ex_enum.cpp_qname] = &ex_enums[ex_enum.cpp_qname];
        // fmt::print("GOT TYPE: {}\n", ex_record);
    }
}

namespace {
void do_method(const CXXMethodDecl* method, ExportedRecord* ex_record) {
    // If there are attributes, parse them looking for cppmm annotations
    std::vector<AttrDesc> attrs = get_attrs(method);
    ExportedMethod ex_method(method, attrs);

    // store this method signiature to match against in the second pass
    // fmt::print("storing method {}\n", ex_method.c_name);
    ex_record->methods.push_back(ex_method);
}
} // namespace

void MatchBindingsCallback::handle_record(const CXXRecordDecl* record) {
    ExportedRecord ex_record;

    ex_record.cpp_name = record->getNameAsString();
    ex_record.c_name = ex_record.cpp_name;
    ex_record.namespaces = get_namespaces(record->getParent());
    ex_record.c_qname =
        prefix_from_namespaces(ex_record.namespaces, "_") + ex_record.c_name;
    ex_record.cpp_qname =
        prefix_from_namespaces(ex_record.namespaces, "::") + ex_record.cpp_name;

    if (isa<ClassTemplateSpecializationDecl>(record)) {
        // we'll monomorphize and bind only the specializations we call out
        // using type aliases, and those are handled in handle_typealias
        return;
    } else if (record->isDependentContext()) {
        // this is the template type
        // fmt::print("{} ({:p}) is dependent\n", ex_record.cpp_qname,
        //            (void*)record);
        ex_record.is_dependent = true;
    }

    // get attributes for this decl and search for the kind of record we want
    // to create
    std::vector<AttrDesc> attrs = get_attrs(record);
    ex_record.kind = RecordKind::OpaquePtr;
    for (const auto& attr : attrs) {
        if (attr.kind == AttrDesc::Kind::ValueType) {
            ex_record.kind = RecordKind::ValueType;
        } else if (attr.kind == AttrDesc::Kind::OpaqueBytes) {
            ex_record.kind = RecordKind::OpaqueBytes;
        }
    }

    if (ex_records.find(ex_record.cpp_qname) != ex_records.end()) {
        SPDLOG_WARN("Ignoring duplicate definition for {}",
                    ex_record.cpp_qname);
        return;
    }

    // get the filename where this binding is declared
    ASTContext& ctx = record->getASTContext();
    SourceManager& sm = ctx.getSourceManager();
    const auto& loc = record->getLocation();
    std::string filename = sm.getFilename(loc).str();
    ex_record.filename = filename;

    // store the record
    ex_records[ex_record.cpp_qname] = ex_record;
    ExportedRecord* ex_record_ptr = &ex_records[ex_record.cpp_qname];

    // store a pointer to the record in the file
    if (ex_files.find(filename) == ex_files.end()) {
        ex_files[filename] = {};
    }
    auto& ex_file = ex_files[filename];
    if (ex_file.records.find(ex_record.cpp_qname) == ex_file.records.end()) {
        ex_file.records[ex_record.cpp_qname] = ex_record_ptr;
    }

    // now do all the record's methods
    for (const auto* decl : record->decls()) {
        if (isa<FunctionTemplateDecl>(decl)) {
            // TODO: Having templated methods on a templated type is hard
            // because I can't figure out how to detect partial specializations
            // on the bindings, which means we'd need to do a whole bunch of
            // type matching and expansion here to tell which of the bound
            // methods could correspond to an expansion of a templated type
            const auto* ftd = cast<FunctionTemplateDecl>(decl);
            // fmt::print("got FTD {}\n", ftd->getNameAsString());
            const auto* fd = ftd->getTemplatedDecl();
            if (fd && isa<CXXMethodDecl>(fd)) {
                const auto* md = cast<CXXMethodDecl>(fd);
                do_method(md, ex_record_ptr);
            }
        } else if (isa<CXXMethodDecl>(decl)) {
            // As long as the method doesn't have its own template parameter
            // list, we can monomorphize it based on the specialization of the
            // parent class.
            const auto* cmd = cast<CXXMethodDecl>(decl);
            do_method(cmd, ex_record_ptr);
        }
    }
}

void MatchBindingsCallback::handle_function(const FunctionDecl* function) {
    // fmt::print("GOT FUNCTION: {}\n", function->getNameAsString());
    std::vector<AttrDesc> attrs = get_attrs(function);

    // figure out which file we're in
    ASTContext& ctx = function->getASTContext();
    SourceManager& sm = ctx.getSourceManager();
    std::string filename = sm.getFilename(function->getBeginLoc()).str();

    auto namespaces = get_namespaces(function->getParent());

    ExportedFunction ex_function(function, filename, attrs, namespaces);

    std::vector<std::string> template_args;
    std::vector<std::string> template_arg_names;
    std::unordered_map<std::string, std::string> template_named_args;
    if (function->isDependentContext()) {
        // fmt::print("function {} is a dependent\n",
        // function->getNameAsString());

    } else if (function->isFunctionTemplateSpecialization()) {
        // fmt::print("function {} is a specialization\n",
        //            function->getNameAsString());
        for (const auto& targ :
             function->getTemplateSpecializationArgs()->asArray()) {
            template_args.push_back(targ.getAsType().getAsString());
        }
        // fmt::print("  with args: <{}>\n", pystring::join(", ",
        // template_args));

        const FunctionTemplateDecl* ftd = function->getPrimaryTemplate();
        for (const auto& tp : ftd->getTemplateParameters()->asArray()) {
            // fmt::print("TP: {}\n", tp->getNameAsString());
            template_arg_names.push_back(tp->getNameAsString());
        }
    }

    for (int i = 0; i < template_args.size(); ++i) {
        template_named_args[template_arg_names[i]] = template_args[i];
    }

    ex_files[filename].functions.push_back(ex_function);
    if (!template_args.empty()) {
        ex_files[filename]
            .function_specializations[ex_function.cpp_qname]
            .push_back(template_args);
        ex_files[filename].spec_named_args[ex_function.cpp_qname].push_back(
            template_named_args);
    }
}

MatchBindingsConsumer::MatchBindingsConsumer(ASTContext* context) {
    // match all record declrations in the cppmm_bind namespace
    DeclarationMatcher enum_decl_matcher =
        enumDecl(hasAncestor(namespaceDecl(hasName("cppmm_bind"))),
                 unless(isImplicit()))
            .bind("enumDecl");
    _match_finder.addMatcher(enum_decl_matcher, &_handler);

    // match all record declrations in the cppmm_bind namespace
    DeclarationMatcher record_decl_matcher =
        cxxRecordDecl(hasAncestor(namespaceDecl(hasName("cppmm_bind"))),
                      unless(isImplicit()))
            .bind("recordDecl");
    _match_finder.addMatcher(record_decl_matcher, &_handler);

    // match all typedef declrations in the cppmm_bind namespace
    DeclarationMatcher typedef_decl_matcher =
        typeAliasDecl(hasAncestor(namespaceDecl(hasName("cppmm_bind"))),
                      unless(isImplicit()))
            .bind("typeAliasDecl");
    _match_finder.addMatcher(typedef_decl_matcher, &_handler);

    // match all function declarations
    DeclarationMatcher function_decl_matcher =
        functionDecl(hasAncestor(namespaceDecl(hasName("cppmm_bind"))),
                     unless(hasAncestor(recordDecl())))
            .bind("functionDecl");
    _match_finder.addMatcher(function_decl_matcher, &_handler);
}

void MatchBindingsConsumer::HandleTranslationUnit(ASTContext& context) {
    _match_finder.matchAST(context);
}

} // namespace cppmm
