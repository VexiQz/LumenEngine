#include "lumen/core/Http.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Log.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Process.h"
#include "lumen/core/Sha256.h"
#include "lumen/core/Strings.h"

#include <chrono>
#include <thread>

namespace lumen::http
{
	namespace
	{
		std::string FindBackendExe(std::string& kind)
		{
			std::string exe = FindExecutable("curl");
			if (!exe.empty())
			{
				kind = "curl";
				return exe;
			}
			exe = FindExecutable("wget");
			if (!exe.empty())
			{
				kind = "wget";
				return exe;
			}
#ifdef _WIN32
			exe = FindExecutable("powershell");
			if (!exe.empty())
			{
				kind = "powershell";
				return exe;
			}
#endif
			kind.clear();
			return {};
		}

		ProcessOptions BuildDownload(const std::string& kind, const std::string& exe, const DownloadOptions& o, const std::string& part)
		{
			ProcessOptions po;
			po.program = exe;
			po.timeoutMs = o.timeoutMs;
			po.cancel = o.cancel;
			if (kind == "curl")
			{
				po.args = {"--fail", "--location", "--silent", "--show-error", "--proto", o.allowInsecureHttp ? "=http,https" : "=https",
					"--proto-redir", o.allowInsecureHttp ? "=http,https" : "=https", "--max-redirs", "10", "--connect-timeout", "30",
					"--retry", "2", "--retry-delay", "2", "--max-filesize", std::to_string(o.maxBytes),
					"--user-agent", std::string("LumenEngine/") + LUMEN_VERSION_STRING, "--output", fs::ToNative(part)};
				for (const std::string& h : o.headers)
				{
					po.args.push_back("-H");
					po.args.push_back(h);
				}
				po.args.push_back("--");
				po.args.push_back(o.url);
			}
			else if (kind == "wget")
			{
				po.args = {"-q", "--tries=2", "--timeout=30", std::string("--user-agent=LumenEngine/") + LUMEN_VERSION_STRING, "-O", fs::ToNative(part)};
				if (!o.allowInsecureHttp)
					po.args.push_back("--https-only");
				for (const std::string& h : o.headers)
					po.args.push_back("--header=" + h);
				po.args.push_back("--");
				po.args.push_back(o.url);
			}
			else
			{
				// PowerShell: the URL and path are passed through the environment to avoid quoting problems.
				po.args = {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command",
					"$ProgressPreference='SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; "
					"Invoke-WebRequest -UseBasicParsing -Uri $env:LUMEN_URL -OutFile $env:LUMEN_OUT"};
				po.env = {{"LUMEN_URL", o.url}, {"LUMEN_OUT", fs::ToNative(part)}};
			}
			return po;
		}
	} // namespace

	std::string Backend()
	{
		std::string kind;
		FindBackendExe(kind);
		return kind;
	}

	bool IsSafeUrl(const std::string& url, bool allowHttp)
	{
		if (url.empty() || url.size() > 2048)
			return false;
		for (char c : url)
			if ((unsigned char)c < 32 || c == ' ' || c == '"' || c == '\'' || c == '`' || c == '<' || c == '>' || c == '\\')
				return false;
		const bool https = str::StartsWith(url, "https://");
		const bool http = str::StartsWith(url, "http://");
		if (!https && !(http && allowHttp))
			return false;
		const size_t hostStart = url.find("://") + 3;
		const size_t hostEnd = url.find_first_of("/?#", hostStart);
		const std::string authority = url.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);
		if (authority.empty() || authority.find('@') != std::string::npos)
			return false; // credentials in URLs leak into logs and lock files
		return true;
	}

	Status Download(const DownloadOptions& options)
	{
		if (!IsSafeUrl(options.url, options.allowInsecureHttp))
			return Status::Fail("refusing unsafe or non-https URL: " + options.url);
		std::string kind;
		const std::string exe = FindBackendExe(kind);
		if (exe.empty())
			return Status::Fail("no download tool found (curl, wget or PowerShell is required)");

		fs::CreateDirs(fs::Parent(options.destPath));
		const std::string part = options.destPath + ".part";
		std::string lastError;
		for (int attempt = 1; attempt <= std::max(1, options.retries); ++attempt)
		{
			fs::Remove(part);
			ProcessOptions po = BuildDownload(kind, exe, options, part);
			std::shared_ptr<AsyncProcess> proc = AsyncProcess::Start(po);
			while (proc->IsRunning())
			{
				if (options.progress)
					options.progress(fs::FileSize(part), 0);
				std::this_thread::sleep_for(std::chrono::milliseconds(120));
			}
			const ProcessResult& r = proc->Wait();
			if (r.cancelled)
			{
				fs::Remove(part);
				return Status::Fail("download cancelled");
			}
			if (r.Success() && fs::IsFile(part) && fs::FileSize(part) > 0)
			{
				if (!options.sha256.empty())
				{
					const std::string got = Sha256::HashFileHex(part);
					if (!str::EqualsIgnoreCase(got, options.sha256))
					{
						fs::Remove(part);
						return Status::Fail("checksum mismatch for " + options.url + " (expected " + options.sha256 + ", got " + got + ")");
					}
				}
				if (fs::FileSize(part) > options.maxBytes)
				{
					fs::Remove(part);
					return Status::Fail("download exceeds the size limit");
				}
				if (!fs::Rename(part, options.destPath))
				{
					fs::Remove(options.destPath);
					if (!fs::Rename(part, options.destPath))
						return Status::Fail("cannot move download into place: " + options.destPath);
				}
				if (options.progress)
				{
					const uint64_t sz = fs::FileSize(options.destPath);
					options.progress(sz, sz);
				}
				return Status::Ok();
			}
			lastError = r.timedOut ? "timed out" : (r.output.empty() ? r.startError : std::string(str::Trim(r.output)));
			LUMEN_WARN("http", "download attempt %d failed (%s): %s", attempt, options.url.c_str(), lastError.c_str());
			std::this_thread::sleep_for(std::chrono::milliseconds(500 * attempt));
		}
		fs::Remove(part);
		return Status::Fail("download failed: " + options.url + ": " + lastError);
	}


	GetResult Get(const std::string& url, const std::vector<std::string>& headers, uint32_t timeoutMs)
	{
		GetResult g;
		if (!IsSafeUrl(url))
		{
			g.error = "unsafe url";
			return g;
		}
		const std::string tmp = fs::Join(platform::TempDir(), fs::UniqueName("lumen-get") + ".tmp");
		DownloadOptions o;
		o.url = url;
		o.destPath = tmp;
		o.headers = headers;
		o.timeoutMs = timeoutMs;
		o.retries = 1;
		o.maxBytes = 64u << 20;
		const Status s = Download(o);
		if (!s)
		{
			g.error = s.message;
			return g;
		}
		fs::ReadText(tmp, g.body);
		fs::Remove(tmp);
		g.ok = true;
		g.status = 200;
		return g;
	}

} // namespace lumen::http
