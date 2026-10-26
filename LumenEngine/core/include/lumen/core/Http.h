// Lumen Engine — HTTP(S) downloads.
//
// TLS is delegated to the platform: `curl` (shipped with Windows 10+, macOS and nearly every Linux), with
// `wget` and PowerShell as fallbacks. Downloads go to "<dest>.part", are verified (SHA-256 when supplied) and
// atomically renamed, so an interrupted transfer never leaves a half written file in place.
#pragma once

#include "lumen/core/Common.h"

#include <atomic>
#include <functional>

namespace lumen::http
{
	struct DownloadOptions
	{
		std::string url;
		std::string destPath;
		std::string sha256;                // hex; empty: not verified
		bool allowInsecureHttp = false;    // plain http:// is refused unless this is set
		int retries = 3;
		uint32_t timeoutMs = 15 * 60 * 1000;
		uint64_t maxBytes = 8ull << 30;
		std::vector<std::string> headers;  // "Name: value"
		std::atomic<bool>* cancel = nullptr;
		// Called regularly with bytes received so far and the expected total (0 when unknown).
		std::function<void(uint64_t done, uint64_t total)> progress;
	};

	Status Download(const DownloadOptions& options);

	struct GetResult
	{
		bool ok = false;
		int status = 0;
		std::string body;
		std::string error;
	};
	GetResult Get(const std::string& url, const std::vector<std::string>& headers = {}, uint32_t timeoutMs = 30000);

	// Which transfer backend is available ("curl", "wget", "powershell" or "").
	std::string Backend();

	// Basic URL validation for package sources: scheme, no control characters, no credentials in the host.
	bool IsSafeUrl(const std::string& url, bool allowHttp = false);

} // namespace lumen::http
