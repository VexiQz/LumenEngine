// Lumen Engine — keyframe animation clips (`.lanim`): transform tracks addressed by child path, events, JSON IO and sampling.
// Rotations are Euler degrees (same convention as scenes and the Lua API) and are interpolated per component.
#pragma once

#include "lumen/core/Json.h"

namespace lumen::anim
{
	struct Key
	{
		float time = 0;
		Float3 value;
	};

	struct Track
	{
		std::string target;               // "" = the entity the clip is played on, otherwise child names joined by '/'
		std::string path = "position";    // position | rotation | scale
		std::string interp = "linear";    // linear | step | smooth (ease in/out)
		std::vector<Key> keys;            // sorted by time
	};

	struct Event
	{
		float time = 0;
		std::string name;
	};

	struct Clip
	{
		std::string name = "clip";
		float duration = 1.0f;
		float fps = 30.0f;                // timeline display / snapping only
		bool loop = false;
		std::vector<Track> tracks;
		std::vector<Event> events;        // sorted by time
	};

	Json ToJson(const Clip& c);
	bool FromJson(const Json& j, Clip& out, std::string* error = nullptr);
	bool Load(const std::string& path, Clip& out, std::string* error = nullptr);
	bool Save(const std::string& path, const Clip& c);

	Float3 DefaultValue(const std::string& path);                       // 0,0,0 or 1,1,1 for scale
	Float3 Sample(const Track& t, float time);                          // clamps outside the key range; default value when empty
	int FindKey(const Track& t, float time, float tolerance = 0.5f / 60.0f);
	int AddKey(Track& t, float time, const Float3& value);              // replaces a key at the same time, keeps order; returns index
	bool RemoveKey(Track& t, int index);
	int MoveKey(Track& t, int index, float newTime);                    // re-sorts, returns the new index
	Track* FindTrack(Clip& c, const std::string& target, const std::string& path);
	Track& EnsureTrack(Clip& c, const std::string& target, const std::string& path);
	float ComputeDuration(const Clip& c);                               // last key / event time (>= 0.05)
	void SortEvents(Clip& c);
	// Events with from < time <= to (handles looping when `to` < `from` by wrapping at `duration`).
	std::vector<const Event*> EventsBetween(const Clip& c, float from, float to);

} // namespace lumen::anim
