#pragma once

#include "generator.hpp"

namespace cppmm {

class GeneratorC : public Generator {
public:
    // FIXME: the logic of what things end up in what maps is a bit gnarly here.
    // We should really move everythign that's in ExportedFile into File during
    // the second phase, and clarify what's expected to be in what maps exactly.
    virtual void
    generate(const std::string& output_dir, const ExportedFileMap& ex_files,
             const FileMap& files, const RecordMap& records,
             const EnumMap& enums, const VectorMap& vectors,
             const std::vector<std::string>& project_includes,
             const std::vector<std::string>& project_libraries) override;
};

} // namespace cppmm
