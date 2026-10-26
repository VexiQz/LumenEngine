#pragma once

#include <cstdint>
#include <string>

namespace lumen::diagnostics
{
	void Install(const std::string& logDirectory);
	void Stage(const char* name);
	void FrameCompleted(uint64_t frame);
	void Stop();
}
