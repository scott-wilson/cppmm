#include "decls.hpp"
#include "exports.hpp"
#include "namespaces.hpp"
#include "vector.hpp"

#include "pystring.h"

#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Type.h>

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

using namespace clang;

template <>
struct fmt::formatter<std::unordered_map<std::string, std::string>> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const std::unordered_map<std::string, std::string>& map,
                FormatContext& ctx) {
        format_to(ctx.out(), "{{");
        // return format_to(ctx.out(), "using {} = {}<{}>;", spec.alias,
        //                  spec.record_cpp_qname,
        //                  pystring::join(", ", spec.template_args));
        bool first = true;
        for (const auto& it : map) {
            if (!first) {
                format_to(ctx.out(), ", ");
            } else {
                first = false;
            }
            format_to(ctx.out(), "{}: {}", it.first, it.second);
        }
        return format_to(ctx.out(), "}}");
    }
};

namespace cppmm {

std::unordered_map<std::string, File> files;
std::unordered_map<std::string, Record> records;
std::unordered_map<std::string, Enum> enums;
std::unordered_map<std::string, Function> functions;
std::unordered_map<std::string, Vector> vectors;

bool is_builtin(const QualType& qt) {
    return (qt->isBuiltinType() ||
            (qt->isPointerType() && qt->getPointeeType()->isBuiltinType()));
}

bool is_recordpointer(const QualType& qt) {
    return ((qt->isReferenceType() || qt->isPointerType()) &&
            qt->getPointeeType()->isRecordType());
}

std::string qualified_type_name(const Type& type) {
    return prefix_from_namespaces(type.namespaces, "::") + type.name;
}

std::string cpp_qname_from_decl(const clang::RecordDecl* decl) {
    std::string cpp_name = decl->getNameAsString();
    std::vector<std::string> namespaces = get_namespaces(decl->getParent());

    std::string cpp_qname = prefix_from_namespaces(namespaces, "::") + cpp_name;

    return cpp_qname;
}

std::string cpp_qname_from_decl(const clang::EnumDecl* decl) {
    std::string cpp_name = decl->getNameAsString();
    std::vector<std::string> namespaces = get_namespaces(decl->getParent());

    std::string cpp_qname = prefix_from_namespaces(namespaces, "::") + cpp_name;

    return cpp_qname;
}

std::string cpp_qname_from_decl(const clang::FunctionDecl* decl) {
    std::string cpp_name = decl->getNameAsString();
    std::vector<std::string> namespaces = get_namespaces(decl->getParent());

    std::string cpp_qname = prefix_from_namespaces(namespaces, "::") + cpp_name;

    return cpp_qname;
}

Record* get_record(const std::string& cpp_qname) {
    auto it_rec = records.find(cpp_qname);
    if (it_rec != records.end()) {
        return &it_rec->second;
    } else {
        return nullptr;
    }
}

Enum* get_enum(const std::string& cpp_qname) {
    auto it_enm = enums.find(cpp_qname);
    if (it_enm != enums.end()) {
        return &it_enm->second;
    } else {
        return nullptr;
    }
}

Record create_record(const CXXRecordDecl* record, std::string cpp_name,
                     std::string c_name, std::vector<std::string> namespaces,
                     std::string cpp_qname, std::string c_qname,
                     std::string filename, RecordKind kind, size_t size,
                     size_t alignment, std::vector<std::string> template_args) {
    SPDLOG_TRACE("create_record {}", cpp_qname);
    std::vector<Param> fields;
    // If it's a value type, expose all the fields
    if (kind == RecordKind::ValueType) {
        for (const auto* field : record->fields()) {
            std::string field_name = field->getNameAsString();
            Param field_param = process_param_type(field_name, field->getType(),
                                                   template_args, {});
            fields.push_back(field_param);
        }
    }
    return Record{
        .cpp_name = cpp_name,
        .namespaces = namespaces,
        .c_name = c_name,
        .kind = kind,
        .filename = filename,
        .fields = fields,
        .methods = {},
        .size = size,
        .alignment = alignment,
        .cpp_qname = cpp_qname,
        .c_qname = c_qname,
        .c_pretty_name = prefix_from_namespaces(rename_all_namespaces(namespaces), "_") + c_name
    };
}

void do_method(
    const CXXMethodDecl* method, Record* record, ExportedRecord& ex_record,
    const std::vector<std::string>& template_args,
    const std::unordered_map<std::string, std::string>& template_named_args) {
    const auto method_name = method->getNameAsString();

    // convert this method so we can match it against our stored ones
    const auto this_ex_method = ExportedMethod(method, {});

    // now see if we can find the method in the exported methods on
    // the exported class
    const cppmm::ExportedMethod* matched_ex_method = nullptr;
    bool rejected = true;
    SPDLOG_TRACE("  matching {}", this_ex_method);
    for (const auto& ex_method : ex_record.methods) {
        SPDLOG_TRACE("    - {}", ex_method);
        if (this_ex_method == ex_method) {
            // found a matching exported method (but may still be
            // ignored)
            rejected = false;
            if (!(ex_method.is_ignored() || ex_method.is_manual())) {
                // not ignored, this is the one we'll use
                matched_ex_method = &ex_method;
                SPDLOG_TRACE("      MATCH");
                break;
            }
        }
    }

    // store the rejected method on the class so we can warn that we
    // didn't find a match
    if (rejected) {
        ex_record.rejected_methods.push_back(this_ex_method);
    }

    // we don't want to bind this method so bail
    if (matched_ex_method == nullptr) {
        return;
    }

    // check if we're dependent
    if (method->isDependentContext() && template_args.empty()) {
        return;
    }

    if (record->methods.find(matched_ex_method->c_name) ==
        record->methods.end()) {
        record->methods.insert(
            std::make_pair(matched_ex_method->c_name,
                           process_method(method, *matched_ex_method, record,
                                          template_args, template_named_args)));
    }
}

Record*
process_record_specialization(const CXXRecordDecl* record,
                              const std::string& cpp_name,
                              const std::vector<std::string>& namespaces,
                              const ExportedSpecialization& ex_spec) {
    auto cpp_qname = prefix_from_namespaces(namespaces, "::") + cpp_name;
    auto record_cpp_qname = cpp_qname;
    cpp_qname +=
        fmt::format("<{}>", pystring::join(", ", ex_spec.template_args));
    auto c_qname = prefix_from_namespaces(namespaces, "_") + ex_spec.alias;
    SPDLOG_TRACE("process_record_specialization {}", record_cpp_qname);
    // fmt::print("{} <{}>\n", ex_spec.alias,
    //            pystring::join(", ", ex_spec.template_args));

    auto it_record = records.find(cpp_qname);
    if (it_record != records.end()) {
        // already done this type, return
        return &it_record->second;
    }

    auto it_ex_record = ex_records.find(record_cpp_qname);
    if (it_ex_record == ex_records.end()) {
        SPDLOG_WARN("Record '{}' has no export definition", record_cpp_qname);
        return nullptr;
    }

    ASTContext& ctx = record->getASTContext();
    size_t size = 0;
    size_t alignment = 0;

    // FIXME: tyring to get the size and alignment for a
    // template/specialization crashes clang
    if (it_ex_record->second.kind == RecordKind::OpaqueBytes) {
        throw std::runtime_error(
            "Template specializations cannot be opaquebytes kind");
    }

    Record rec = create_record(
        record, cpp_name, ex_spec.alias, namespaces, cpp_qname, c_qname,
        it_ex_record->second.filename, it_ex_record->second.kind, size,
        alignment, ex_spec.template_args);

    if (rec.kind == RecordKind::ValueType && !rec.is_pod()) {
        SPDLOG_ERROR("{} is valuetype but not POD", rec.c_qname);
        return nullptr;
    } else if (rec.kind == RecordKind::OpaqueBytes && !rec.is_pod()) {
        SPDLOG_ERROR("{} is opaquebytes but not POD", rec.c_qname);
        return nullptr;
    }

    records[cpp_qname] = rec;
    Record* record_ptr = &records[cpp_qname];
    files[rec.filename].records[cpp_qname] = record_ptr;

    // we need to iterate over all decls in order to get methods
    // that depend on a template arg that's not an arg of the record
    SPDLOG_TRACE("fields:");
    for (const auto* decl : record->decls()) {
        if (isa<FunctionTemplateDecl>(decl)) {
            // TODO: Having templated methods on a templated type is hard
            // because I can't figure out how to detect partial specializations
            // on the bindings, which means we'd need to do a whole bunch of
            // type matching and expansion here to tell which of the bound
            // methods could correspond to an expansion of a templated type
            const auto* ftd = cast<FunctionTemplateDecl>(decl);
            SPDLOG_TRACE("  function template {}", ftd->getNameAsString());
            const auto* fd = ftd->getTemplatedDecl();
            if (fd && isa<CXXMethodDecl>(fd)) {
                const auto* md = cast<CXXMethodDecl>(fd);
                // fmt::print("got method from fd: {}\n",
                // md->getNameAsString());
            }
        } else if (isa<CXXMethodDecl>(decl)) {
            // As long as the method doesn't have its own template parameter
            // list, we can monomorphize it based on the specialization of the
            // parent class.
            const auto* cmd = cast<CXXMethodDecl>(decl);
            do_method(cmd, record_ptr, it_ex_record->second,
                      ex_spec.template_args, ex_spec.template_named_args);
        } else {
            SPDLOG_TRACE("  Unhandled decl kind {}", decl->getDeclKindName());
        }
    }

    return record_ptr;
}

Record* process_record(const CXXRecordDecl* record) {
    SPDLOG_TRACE("process_record {}", record->getQualifiedNameAsString());

    std::string cpp_name = record->getNameAsString();
    std::vector<std::string> namespaces = get_namespaces(record->getParent());
    std::vector<std::string> pretty_namespaces =
        rename_all_namespaces(namespaces);

    const auto c_qname = prefix_from_namespaces(namespaces, "_") + cpp_name;
    const auto c_pretty_name =
        prefix_from_namespaces(pretty_namespaces, "_") + cpp_name;
    auto cpp_qname = prefix_from_namespaces(namespaces, "::") + cpp_name;

    std::vector<std::string> template_args;
    if (record->isDependentContext()) {
        // fmt::print(" {:p} is dependent\n", (void*)record);
        auto it_ex_spec = ex_specs.find(cpp_qname);
        if (it_ex_spec != ex_specs.end()) {
            for (const auto& ex_spec : it_ex_spec->second) {
                Record* _ignore = process_record_specialization(
                    record, cpp_name, namespaces, ex_spec);
            }
            return nullptr;
        } else {
            SPDLOG_ERROR("Could not find specs for {}", cpp_qname);
        }
    } else if (isa<ClassTemplateSpecializationDecl>(record)) {
        const ClassTemplateSpecializationDecl* ctsd =
            cast<ClassTemplateSpecializationDecl>(record);
        // fmt::print("   is a CTSD\n");
        for (const auto& targ : ctsd->getTemplateArgs().asArray()) {
            if (!targ.getAsType()->isBuiltinType()) {
                throw std::runtime_error(fmt::format(
                    "template argument {} on {} is not a builtin.",
                    targ.getAsType().getAsString(), record->getNameAsString()));
            }
            template_args.push_back(targ.getAsType().getAsString());
        }
        cpp_qname += fmt::format("<{}>", pystring::join(", ", template_args));
        // fmt::print("   {}\n", cpp_qname);

        auto it_record = records.find(cpp_qname);
        // fmt::print("searching for {}\n", cpp_qname);
        if (it_record != records.end()) {
            // already done this type, return
            // fmt::print("returning {}: {}\n", it_record->second.cpp_qname,
            //            it_record->second.c_qname);
            return &it_record->second;
        } else {
            SPDLOG_ERROR("Could not find specialized record for {}", cpp_qname);
            return nullptr;
        }
    }

    auto it_record = records.find(cpp_qname);
    if (it_record != records.end()) {
        // already done this type, return
        return &it_record->second;
    }

    auto it_ex_record = ex_records.find(cpp_qname);
    if (it_ex_record == ex_records.end()) {
        // fmt::print("WARNING: record '{}' has no export definition\n",
        // c_name);
        return nullptr;
    }

    // get size and alignment info
    // clang returns in bits so divide by 8 to get bytes
    ASTContext& ctx = record->getASTContext();
    size_t size = 0;
    size_t alignment = 0;

    // FIXME: tyring to get the size and alignment for a template/specialization
    // crashes clang
    if (template_args.empty()) {
        size = ctx.getTypeSize(record->getTypeForDecl()) / 8;
        alignment = ctx.getTypeAlign(record->getTypeForDecl()) / 8;
    } else if (it_ex_record->second.kind == RecordKind::OpaqueBytes) {
        throw std::runtime_error(
            "Template specializations cannot be opaquebytes kind");
    }

    Record rec = create_record(
        record, cpp_name, cpp_name, namespaces, cpp_qname, c_qname,
        it_ex_record->second.filename, it_ex_record->second.kind, size,
        alignment, template_args);

    // fmt::print("Processed record {} -> {} in {}\n", cpp_name, c_qname,
    //    it_ex_record->second.filename)
    if (rec.kind == RecordKind::ValueType && !rec.is_pod()) {
        SPDLOG_ERROR("{} is valuetype but not POD", rec.c_qname);
        return nullptr;
    } else if (rec.kind == RecordKind::OpaqueBytes && !rec.is_pod()) {
        SPDLOG_ERROR("{} is opaquebytes but not POD", rec.c_qname);
        return nullptr;
    }

    records[cpp_qname] = rec;
    Record* record_ptr = &records[cpp_qname];
    files[rec.filename].records[cpp_qname] = record_ptr;
    // fmt::print("MATCHED: {}\n", cpp_name);
    //
    // now do the methods
    // record->dump();
    for (const auto* method_decl : record->methods()) {
        do_method(method_decl, record_ptr, it_ex_record->second, {}, {});
    }

    return record_ptr;
}

Enum* process_enum(const EnumDecl* enum_decl) {
    std::string cpp_name = enum_decl->getNameAsString();
    std::vector<std::string> namespaces =
        get_namespaces(enum_decl->getParent());

    const auto c_name = cpp_name;
    const auto cpp_qname = prefix_from_namespaces(namespaces, "::") + cpp_name;
    const auto c_qname = prefix_from_namespaces(namespaces, "_") + c_name;
    const auto c_pretty_name =
        prefix_from_namespaces(rename_all_namespaces(namespaces), "_") + c_name;

    auto it_enum = enums.find(cpp_qname);
    if (it_enum != enums.end()) {
        // already done this type, return
        return &it_enum->second;
    }

    auto it_ex_enum = ex_enums.find(cpp_qname);
    if (it_ex_enum == ex_enums.end()) {
        // fmt::print("WARNING: enum '{}' has no export definition\n",
        // c_qname);
        return nullptr;
    }

    // fmt::print("Processed enum {} -> {} in {}\n", cpp_name, c_qname,
    //    it_ex_enum->second.filename);

    std::vector<std::pair<std::string, uint64_t>> enumerators;
    for (const auto& ecd : enum_decl->enumerators()) {
        enumerators.push_back(std::make_pair<std::string, uint64_t>(
            ecd->getNameAsString(), ecd->getInitVal().getLimitedValue()));
    }

    enums[cpp_qname] = Enum{.cpp_name = cpp_name,
                            .namespaces = namespaces,
                            .c_name = c_name,
                            .filename = it_ex_enum->second.filename,
                            .enumerators = enumerators,
                            .cpp_qname = cpp_qname,
                            .c_qname = c_qname,
                            .c_pretty_name = c_pretty_name};
    Enum* enm = &enums[cpp_qname];
    files[enm->filename].enums[cpp_qname] = enm;
    return enm;
    // fmt::print("MATCHED: {}\n", cpp_name);
}

Vector* process_vector(const QualifiedType& element_type) {
    std::string ename;
    if (element_type.type.name == "basic_string") {
        ename = "cppmm_string";
    } else {
        ename = element_type.type.get_c_qname();
    }
    std::string c_qname = fmt::format("{}_vector", ename);
    auto it_vec = vectors.find(c_qname);
    if (it_vec != vectors.end()) {
        return &it_vec->second;
    } else {
        auto p = vectors.insert(std::make_pair(element_type.type.get_c_qname(),
                                               Vector{element_type, c_qname}));
        return &p.first->second;
    }
}

QualifiedType process_pointee_type(
    const QualType& qt, const std::vector<std::string>& template_args,
    const std::unordered_map<std::string, std::string>& template_named_args) {
    if (is_builtin(qt)) {
        std::string name = qt.getTypePtr()
                               ->getUnqualifiedDesugaredType()
                               ->getAs<BuiltinType>()
                               ->desugar()
                               .getAsString();

        // C++ doesn't like _Bool but we can include stdbool.h for C
        if (name == "_Bool") {
            name = "bool";
        }
        QualifiedType qtype{cppmm::Type{name, &builtin_int}};
        qtype.requires_cast = false;
        qtype.is_const = qt.isConstQualified();
        return qtype;
    } else if (qt->isTemplateTypeParmType()) {
        const auto* ttpt = qt->castAs<TemplateTypeParmType>();
        // fmt::print("    template type parm type {}\n",
        //            ttpt->desugar().getAsString());
        int index = ttpt->getIndex();
        QualifiedType qtype{cppmm::Type{template_args.at(index), &builtin_int}};
        qtype.is_const = qt.isConstQualified();
        return qtype;
    } else if (qt->isRecordType()) {
        const CXXRecordDecl* crd = qt->getAsCXXRecordDecl();
        if (crd->getNameAsString() == "unique_ptr") {
            const auto* tst = qt->getAs<TemplateSpecializationType>();
            QualifiedType qtype =
                process_pointee_type(tst->getArgs()->getAsType(), template_args,
                                     template_named_args);
            qtype.is_uptr = true;
            qtype.is_const = qt.isConstQualified();
            return qtype;
        } else if (crd->getNameAsString() == "vector") {
            const auto* tst = qt->getAs<TemplateSpecializationType>();
            QualifiedType element_type =
                process_pointee_type(tst->getArgs()->getAsType(), template_args,
                                     template_named_args);
            Vector* vec = process_vector(element_type);

            QualifiedType qtype{Type{vec->c_qname, vec}};
            qtype.requires_cast = true;
            qtype.is_const = qt.isConstQualified();

            return qtype;
        } else if (crd->getNameAsString() == "basic_string") {
            const CXXRecordDecl* crd = qt->getAsCXXRecordDecl();
            std::string type_name = crd->getNameAsString();
            QualifiedType qtype{Type{
                type_name, &builtin_string,
                cppmm::get_namespaces(qt->getAsCXXRecordDecl()->getParent())}};
            qtype.requires_cast = true;
            qtype.is_const = qt.isConstQualified();
            return qtype;
        } else {
            const CXXRecordDecl* crd = qt->getAsCXXRecordDecl();
            cppmm::Record* record_ptr = process_record(crd);
            if (record_ptr == nullptr) {
                // fmt::print("ERROR: could not process record for {}\n",
                //            crd->getNameAsString());
            }
            std::string type_name = crd->getNameAsString();
            QualifiedType qtype{Type{
                type_name, record_ptr,
                cppmm::get_namespaces(qt->getAsCXXRecordDecl()->getParent())}};
            qtype.requires_cast = !(crd->getNameAsString() == "string_view");
            qtype.is_const = qt.isConstQualified();
            return qtype;
        }
    } else if (qt->isEnumeralType()) {
        const auto* enum_decl = qt->getAs<EnumType>()->getDecl();
        const cppmm::Enum* enm = process_enum(enum_decl);

        QualifiedType qtype{Type{enm->cpp_name, enm, enm->namespaces}};
        qtype.is_const = qt.isConstQualified();
        return qtype;
    } else if (qt->isDependentType()) {
        // fmt::print("Got dependent type: {}\n", qt.getAsString());
        const auto* tst = qt->getAs<TemplateSpecializationType>();
        if (!tst) {
            const auto* icnt = qt->getAs<InjectedClassNameType>();
            if (!icnt) {
                throw std::runtime_error("Could not get ICNT");
            }
            tst = icnt->getInjectedTST();
            if (!tst) {
                throw std::runtime_error("Could not get TST from ICNT");
            }
        }
        // fmt::print("    with {} args\n", tst->getNumArgs());
        // Expand the list of template arg names (i.e. T, U etc.) into the
        // actual type names
        std::vector<std::string> this_template_args;
        for (int i = 0; i < tst->getNumArgs(); ++i) {
            // fmt::print("        {}\n",
            //            tst->getArgs()[i].getAsType().getAsString());
            std::string t_arg_name =
                tst->getArgs()[i].getAsType().getAsString();
            auto it_arg = template_named_args.find(t_arg_name);
            if (it_arg != template_named_args.end()) {
                this_template_args.push_back(it_arg->second);
            } else {
                SPDLOG_ERROR("Could not find matching argument for {}",
                             t_arg_name);
                SPDLOG_ERROR("template_args are: [{}]",
                             pystring::join(", ", template_args));
                SPDLOG_ERROR("template_named_args are: {}",
                             template_named_args);
                qt->dump();
                return QualifiedType{Type{"UNHANDLED", &builtin_int}};
            }
        }

        auto template_name = tst->getTemplateName();
        const auto* td = template_name.getAsTemplateDecl();
        // fmt::print("TD: {}\n", td->getQualifiedNameAsString());

        auto* nd = td->getTemplatedDecl();
        // fmt::print("templated decl: {} {:p}\n", nd->getNameAsString(),
        //            (void*)nd);
        if (isa<CXXRecordDecl>(nd)) {
            auto* crd = cast<CXXRecordDecl>(nd);
            // fmt::print("cxxrecorddecl: {:p}\n", (void*)crd);
            std::string cpp_name = crd->getNameAsString();
            auto namespaces = get_namespaces(crd->getParent());
            auto cpp_qname =
                prefix_from_namespaces(namespaces, "::") + cpp_name;
            auto record_cpp_qname = cpp_qname;
            // fmt::print("called with template args <{}>\n",
            //            pystring::join(", ", this_template_args));

            // now find the matching specialization
            const auto& specs = ex_specs[record_cpp_qname];
            for (const auto& spec : specs) {
                if (this_template_args == spec.template_args) {
                    const Record* record = process_record_specialization(
                        crd, cpp_name, namespaces, spec);
                    QualifiedType qtype{Type{cpp_name, record, namespaces}};
                    qtype.requires_cast = true;
                    qtype.is_const = qt.isConstQualified();
                    return qtype;
                }
            }
        }

        qt->dump();
        return QualifiedType{Type{"UNHANDLED", &builtin_int}};

        // const auto* enum_decl = qt->getAs<EnumType>()->getDecl();
        // const cppmm::Enum* enm = process_enum(enum_decl);

        // QualifiedType qtype{Type{enm->cpp_name, enm, enm->namespaces}};
        // qtype.is_const = qt.isConstQualified();
        // return qtype;
    } else {
        SPDLOG_ERROR("Unhandled type: {}", qt.getAsString());
        qt->dump();
        return QualifiedType{Type{"UNHANDLED", &builtin_int}};
    }
}

QualifiedType process_qualified_type(
    const QualType& qt, const std::vector<std::string>& template_args,
    const std::unordered_map<std::string, std::string>& template_named_args) {
    bool is_ptr = qt->isPointerType();
    bool is_ref = qt->isReferenceType();

    if (is_ptr || is_ref) {
        QualifiedType result = process_pointee_type(
            qt->getPointeeType(), template_args, template_named_args);
        result.is_ptr = is_ptr;
        result.is_ref = is_ref;
        return result;
    } else {
        QualifiedType result =
            process_pointee_type(qt, template_args, template_named_args);
        return result;
    }
}

Param process_param_type(
    const std::string& param_name, const QualType& qt,
    const std::vector<std::string>& template_args,
    const std::unordered_map<std::string, std::string>& template_named_args) {
    QualifiedType qtype =
        process_qualified_type(qt, template_args, template_named_args);
    return Param{param_name, qtype};
}

std::string get_decl_comment(const Decl* decl) {
    ASTContext& ctx = decl->getASTContext();
    SourceManager& sm = ctx.getSourceManager();
    const RawComment* rc = ctx.getRawCommentForDeclNoCache(decl);
    std::string result;
    if (rc) {
        std::string raw = rc->getRawText(sm).str();
        // dedent lines
        std::vector<std::string> lines;
        pystring::splitlines(raw, lines);
        for (auto& line : lines) {
            line = pystring::strip(line);
        }

        result = pystring::join("\n", lines);
    }

    return result;
}

// Function create_function(const FunctionDecl* function, const
// ExportedFunction& ex_function) {

// }
//

Function* generate_function_specialization(
    const FunctionDecl* function, const ExportedFunction& ex_function,
    const std::vector<std::string>& namespaces,
    const std::vector<std::string>& template_args,
    const std::unordered_map<std::string, std::string>& template_named_args) {
    std::vector<Param> params;
    int i = 0;
    for (const auto& p : function->parameters()) {
        auto param_name = p->getNameAsString();

        if (param_name.empty()) {
            // library doesn't name the parameter, first try and see if the
            // binding named it...
            if (ex_function.param_names[i].empty()) {
                // binding didnt' name it, auto-generate a name
                param_name = fmt::format("_param_{:02}", i);
            } else {
                // take the name from the binding
                param_name = ex_function.param_names[i];
            }
        }

        // fmt::print("param {}\n", param_name);
        params.push_back(process_param_type(
            param_name, p->getType(), template_args, template_named_args));
        i++;
    }

    std::string comment = get_decl_comment(function);

    // fmt::print("returning function {}\n", ex_function.cpp_name);
    Function func =
        Function{ex_function.cpp_name,
                 ex_function.c_name,
                 process_qualified_type(function->getReturnType(),
                                        template_args, template_named_args),
                 params,
                 comment,
                 namespaces,
                 ex_function.filename,
                 template_args};

    if (files.find(func.filename) == files.end()) {
        files[func.filename] = {};
    }
    auto& file = files[func.filename];
    if (file.functions.find(func.cpp_qname) == file.functions.end()) {
        // file.functions[matched_ex_function->c_name] =
        //     process_function(function, *matched_ex_function, namespaces);
        auto fp = functions.insert(std::make_pair(func.cpp_qname, func));
        file.functions.insert(
            std::make_pair(func.cpp_qname, &fp.first->second));
    }

    return file.functions[func.cpp_qname];
}

void process_function(const FunctionDecl* function,
                      const ExportedFunction& ex_function,
                      std::vector<std::string> namespaces) {
    // fmt::print("process_function {}\n",
    // function->getQualifiedNameAsString());

    std::vector<std::string> template_args;
    if (function->isDependentContext()) {
        // fmt::print("    is dependent\n");
        const auto& specs =
            ex_files[ex_function.filename]
                .function_specializations[ex_function.cpp_qname];
        const auto& named_args = ex_files[ex_function.filename]
                                     .spec_named_args[ex_function.cpp_qname];
        // fmt::print("    has {} specializations:\n", specs.size());
        for (int i = 0; i < specs.size(); ++i) {
            generate_function_specialization(function, ex_function, namespaces,
                                             specs[i], named_args[i]);
            // fmt::print("        <{}>\n", pystring::join(", ", specv));
        }
        return;
    } else if (function->isFunctionTemplateSpecialization()) {
        // fmt::print("    is a template specialization\n");
    }

    generate_function_specialization(function, ex_function, namespaces, {}, {});
}

Method process_method(
    const CXXMethodDecl* method, const ExportedMethod& ex_method,
    const Record* record, const std::vector<std::string>& template_args,
    const std::unordered_map<std::string, std::string>& template_named_args) {
    // fmt::print("process_method {}\n", method->getQualifiedNameAsString());

    std::vector<Param> params;
    int i = 0;
    for (const auto& p : method->parameters()) {
        auto param_name = p->getNameAsString();

        if (param_name.empty()) {
            // library doesn't name the parameter, first try and see if the
            // binding named it...
            if (ex_method.param_names[i].empty()) {
                // binding didnt' name it, auto-generate a name
                param_name = fmt::format("_param_{:02}", i);
            } else {
                // take the name from the binding
                param_name = ex_method.param_names[i];
            }
        }

        // fmt::print("param {}\n", param_name);
        params.push_back(process_param_type(
            param_name, p->getType(), template_args, template_named_args));
        i++;
    }

    std::string comment = get_decl_comment(method);

    bool is_constructor = false;
    bool is_copy_constructor = false;
    bool is_copy_assignment = method->isCopyAssignmentOperator();
    bool is_operator = false;
    bool is_conversion_operator = false;
    std::string op;

    if (isa<CXXConstructorDecl>(method)) {
        const auto* ctor = cast<CXXConstructorDecl>(method);
        is_constructor = true;
        is_copy_constructor = ctor->isCopyConstructor();
    } else if (ex_method.cpp_name.find("operator") == 0) {
        // we assume that clang always concatenates the operator name with the
        // symbols so we can use this to tell the difference with a conversion
        // operator. FIXME: is this true?
        std::vector<std::string> toks;
        pystring::split(ex_method.cpp_name, toks);
        if (toks.size() == 1) {
            op = ex_method.cpp_name.substr(8, 100);
            is_operator = true;
            // fmt::print("Got operator '{}'\n", op);
        } else {
            op = toks[1];
            is_conversion_operator = true;
            // fmt::print("Got conversion op to '{}'\n", op);
        }
    }

    // do some parameter patching. clang ignores the parameter name for copy
    // ctor and assignment but we need one so just use "other"
    if (is_copy_constructor || is_copy_assignment) {
        params[0].name = "other";
    }

    std::vector<std::string> namespaces = record->namespaces;
    namespaces.push_back(record->c_name);

    return Method{ex_method.cpp_name,
                  ex_method.c_name,
                  process_qualified_type(method->getReturnType(), template_args,
                                         template_named_args),
                  params,
                  comment,
                  namespaces,
                  method->isConst(),
                  method->isStatic(),
                  is_constructor,
                  is_copy_constructor,
                  is_copy_assignment,
                  is_operator,
                  is_conversion_operator,
                  op,
                  template_args};
}

} // namespace cppmm
