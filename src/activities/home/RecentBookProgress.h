#pragma once

#include <cstddef>

struct RecentBook;

namespace RecentBookProgress {
float loadPercent(const RecentBook& book);
bool hasPercent(float progress);
void formatPercent(float progress, char* buffer, size_t len);
}  // namespace RecentBookProgress
