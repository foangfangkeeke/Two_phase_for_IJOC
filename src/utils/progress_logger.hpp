#pragma once

#include <iostream>
#include <string>

namespace ProgressLog {

inline void PrintMasterDone(const std::string& label, int iter, double lowerBound,
    long long masterMs, long long elapsedMs)
{
    std::cout << label << " iter " << iter
              << " master done, LB=" << lowerBound
              << ", master_ms=" << masterMs
              << ", elapsed_ms=" << elapsedMs << std::endl;
}

} // namespace ProgressLog
