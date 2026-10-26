#include "lumen/core/Animation.h"
#include "lumen/core/Fs.h"

#include <algorithm>
#include <cmath>

namespace lumen::anim
{
	namespace
	{
		bool Known(const std::string& p) { return p == "position" || p == "rotation" || p == "scale"; }
		float Ease(float t) { return t * t * (3 - 2 * t); }
	}

	Float3 DefaultValue(const std::string& path)
	{
		return path == "scale" ? Float3{1, 1, 1} : Float3{0, 0, 0};
	}

	Json ToJson(const Clip& c)
	{
		Json j = Json::MakeObject();
		j["format"] = "lumen-anim";
		j["version"] = 1;
		j["name"] = c.name;
		j["duration"] = c.duration;
		j["fps"] = c.fps;
		j["loop"] = c.loop;
		Json tracks = Json::MakeArray();
		for (const Track& t : c.tracks)
		{
			Json o = Json::MakeObject();
			if (!t.target.empty()) o["target"] = t.target;
			o["path"] = t.path;
			if (t.interp != "linear") o["interp"] = t.interp;
			Json keys = Json::MakeArray();
			for (const Key& k : t.keys)
			{
				Json a = Json::MakeArray();
				a.Push(k.time);
				a.Push(k.value.x);
				a.Push(k.value.y);
				a.Push(k.value.z);
				keys.Push(std::move(a));
			}
			o["keys"] = std::move(keys); // [time, x, y, z]
			tracks.Push(std::move(o));
		}
		j["tracks"] = std::move(tracks);
		if (!c.events.empty())
		{
			Json ev = Json::MakeArray();
			for (const Event& e : c.events)
			{
				Json o = Json::MakeObject();
				o["time"] = e.time;
				o["name"] = e.name;
				ev.Push(std::move(o));
			}
			j["events"] = std::move(ev);
		}
		return j;
	}

	bool FromJson(const Json& j, Clip& out, std::string* error)
	{
		out = Clip{};
		if (!j.IsObject() || j.GetString("format") != "lumen-anim")
		{
			if (error) *error = "not a lumen-anim file";
			return false;
		}
		out.name = j.GetString("name", "clip");
		out.duration = (float)j.GetDouble("duration", 1.0);
		out.fps = (float)j.GetDouble("fps", 30.0);
		out.loop = j.GetBool("loop", false);
		for (const Json& o : j.Get("tracks").Items())
		{
			Track t;
			t.target = o.GetString("target");
			t.path = o.GetString("path", "position");
			if (!Known(t.path)) continue;
			t.interp = o.GetString("interp", "linear");
			for (const Json& k : o.Get("keys").Items())
			{
				if (k.Size() < 4) continue;
				t.keys.push_back({(float)k.At(0).AsDouble(), {(float)k.At(1).AsDouble(), (float)k.At(2).AsDouble(), (float)k.At(3).AsDouble()}});
			}
			std::stable_sort(t.keys.begin(), t.keys.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
			out.tracks.push_back(std::move(t));
		}
		for (const Json& o : j.Get("events").Items())
			if (!o.GetString("name").empty()) out.events.push_back({(float)o.GetDouble("time", 0), o.GetString("name")});
		SortEvents(out);
		if (out.duration <= 0) out.duration = ComputeDuration(out);
		return true;
	}

	bool Load(const std::string& path, Clip& out, std::string* error)
	{
		std::string text;
		if (!fs::ReadText(path, text))
		{
			if (error) *error = "cannot read " + path;
			return false;
		}
		Json j;
		std::string perr;
		if (!Json::Parse(text, j, &perr))
		{
			if (error) *error = perr;
			return false;
		}
		return FromJson(j, out, error);
	}

	bool Save(const std::string& path, const Clip& c)
	{
		fs::CreateDirs(fs::Parent(path));
		return fs::WriteTextIfChanged(path, ToJson(c).Dump(1) + "\n");
	}

	Float3 Sample(const Track& t, float time)
	{
		if (t.keys.empty()) return DefaultValue(t.path);
		if (time <= t.keys.front().time || t.keys.size() == 1) return t.keys.front().value;
		if (time >= t.keys.back().time) return t.keys.back().value;
		size_t hi = 1;
		while (hi < t.keys.size() && t.keys[hi].time < time) ++hi;
		const Key& a = t.keys[hi - 1];
		const Key& b = t.keys[hi];
		if (t.interp == "step") return a.value;
		float u = (time - a.time) / std::max(1e-6f, b.time - a.time);
		if (t.interp == "smooth") u = Ease(u);
		return {a.value.x + (b.value.x - a.value.x) * u, a.value.y + (b.value.y - a.value.y) * u, a.value.z + (b.value.z - a.value.z) * u};
	}

	int FindKey(const Track& t, float time, float tol)
	{
		int best = -1;
		float bd = tol;
		for (size_t i = 0; i < t.keys.size(); ++i)
		{
			const float d = std::fabs(t.keys[i].time - time);
			if (d <= bd) { bd = d; best = (int)i; }
		}
		return best;
	}

	int AddKey(Track& t, float time, const Float3& value)
	{
		time = std::max(0.0f, time);
		const int existing = FindKey(t, time, 1e-4f);
		if (existing >= 0)
		{
			t.keys[(size_t)existing].value = value;
			return existing;
		}
		size_t i = 0;
		while (i < t.keys.size() && t.keys[i].time < time) ++i;
		t.keys.insert(t.keys.begin() + (std::ptrdiff_t)i, Key{time, value});
		return (int)i;
	}

	bool RemoveKey(Track& t, int index)
	{
		if (index < 0 || (size_t)index >= t.keys.size()) return false;
		t.keys.erase(t.keys.begin() + index);
		return true;
	}

	int MoveKey(Track& t, int index, float newTime)
	{
		if (index < 0 || (size_t)index >= t.keys.size()) return index;
		Key k = t.keys[(size_t)index];
		k.time = std::max(0.0f, newTime);
		t.keys.erase(t.keys.begin() + index);
		size_t i = 0;
		while (i < t.keys.size() && t.keys[i].time < k.time) ++i;
		t.keys.insert(t.keys.begin() + (std::ptrdiff_t)i, k);
		return (int)i;
	}

	Track* FindTrack(Clip& c, const std::string& target, const std::string& path)
	{
		for (Track& t : c.tracks)
			if (t.target == target && t.path == path) return &t;
		return nullptr;
	}

	Track& EnsureTrack(Clip& c, const std::string& target, const std::string& path)
	{
		if (Track* t = FindTrack(c, target, path)) return *t;
		Track t;
		t.target = target;
		t.path = path;
		c.tracks.push_back(std::move(t));
		return c.tracks.back();
	}

	float ComputeDuration(const Clip& c)
	{
		float d = 0.05f;
		for (const Track& t : c.tracks)
			if (!t.keys.empty()) d = std::max(d, t.keys.back().time);
		for (const Event& e : c.events) d = std::max(d, e.time);
		return d;
	}

	void SortEvents(Clip& c)
	{
		std::stable_sort(c.events.begin(), c.events.end(), [](const Event& a, const Event& b) { return a.time < b.time; });
	}

	std::vector<const Event*> EventsBetween(const Clip& c, float from, float to)
	{
		std::vector<const Event*> out;
		if (to >= from)
		{
			for (const Event& e : c.events)
				if (e.time > from && e.time <= to) out.push_back(&e);
		}
		else // wrapped around the end of a looping clip
		{
			for (const Event& e : c.events)
				if (e.time > from && e.time <= c.duration) out.push_back(&e);
			for (const Event& e : c.events)
				if (e.time >= 0 && e.time <= to) out.push_back(&e);
		}
		return out;
	}

} // namespace lumen::anim
