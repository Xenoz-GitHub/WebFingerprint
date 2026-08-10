#pragma once

#include <string>
#include <vector>

#include "webfingerprint/engine/evidence.h"
#include "webfingerprint/engine/rule.h"

namespace wf::engine {

class FingerprintEngine {
public:
    FingerprintEngine();
    explicit FingerprintEngine(std::vector<TechnologyDef> technologies);

    void add(TechnologyDef technology);

    std::vector<Technology> analyze(const Evidence& evidence) const;

private:
    std::vector<TechnologyDef> technologies_;
};

}
