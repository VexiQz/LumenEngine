#include "Panels.h"

#include "lumen/core/Strings.h"
#include "lumen/core/Fs.h"
#include "lumen/core/Terrain.h"

#include <algorithm>

namespace lumen
{
	namespace
	{
		bool Combo(const char* label, std::string& value, const std::vector<std::string>& options)
		{
			bool changed = false;
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::SameLine(92);
			ImGui::SetNextItemWidth(-1);
			ImGui::PushID(label);
			if (ImGui::BeginCombo("##c", value.c_str()))
			{
				for (const std::string& o : options)
					if (ImGui::Selectable(o.c_str(), o == value)) { value = o; changed = true; }
				ImGui::EndCombo();
			}
			ImGui::PopID();
			return changed;
		}
		bool Float(const char* label, float& v, float speed = 0.01f, float mn = 0, float mx = 0, const char* fmt = "%.3f")
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::SameLine(92);
			ImGui::SetNextItemWidth(-1);
			ImGui::PushID(label);
			const bool c = ImGui::DragFloat("##f", &v, speed, mn, mx, fmt);
			ImGui::PopID();
			return c;
		}
		bool Slider(const char* label, float& v, float mn, float mx)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::SameLine(92);
			ImGui::SetNextItemWidth(-1);
			ImGui::PushID(label);
			const bool c = ImGui::SliderFloat("##s", &v, mn, mx, "%.3f");
			ImGui::PopID();
			return c;
		}
		bool Check(const char* label, bool& v) { return ImGui::Checkbox(label, &v); }

		bool Header(const char* icon, const char* title, bool* removed = nullptr)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 5));
			const std::string text = std::string(icon) + "  " + title;
			const bool open = ImGui::CollapsingHeader(text.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);
			ImGui::PopStyleVar();
			if (removed)
			{
				ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 22);
				ImGui::PushID(title);
				if (ImGui::SmallButton(ICON_MD_CLOSE)) *removed = true;
				ImGui::PopID();
			}
			return open;
		}

		const std::vector<std::string> kShaders = {"pbr", "water", "toon", "foliage", "terrain", "glass", "unlit", "forcefield", "dissolve", "ice"};
		const std::vector<std::string> kPrims = {"cube", "sphere", "plane", "cylinder", "capsule", "cone"};
		const std::vector<std::string> kLights = {"directional", "point", "spot"};
		const std::vector<std::string> kVehicleTypes = {"car", "motorcycle"};
		const std::vector<std::string> kWheelCollision = {"ray", "sphere", "cylinder"};
		const std::vector<std::string> kJointTypes = {"fixed", "point", "distance", "hinge", "cone", "sixdof", "swingtwist", "slider"};

		void Sep(const char* label)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("%s", label);
			ImGui::Separator();
		}

		// Picks another entity of the scene by name; `id` is "" for "none".
		bool EntityCombo(const char* label, std::string& id, const SceneDoc& doc, const std::string& self, const char* none)
		{
			std::string cur = none;
			for (const SceneEntity& o : doc.entities)
				if (o.id == id) cur = o.name;
			bool changed = false;
			ImGui::PushID(label);
			ImGui::TextUnformatted(label);
			ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.38f);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##e", cur.c_str()))
			{
				if (ImGui::Selectable(none, id.empty())) { id.clear(); changed = true; }
				for (const SceneEntity& o : doc.entities)
				{
					if (o.id == self) continue;
					ImGui::PushID(o.id.c_str());
					if (ImGui::Selectable(o.name.c_str(), o.id == id)) { id = o.id; changed = true; }
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
			return changed;
		}

		const std::vector<std::string> kBodyTypes = {"dynamic", "static", "kinematic"};
		const std::vector<std::string> kParticleShaders = {"soft", "simple", "distortion", "lit"};
		const std::vector<std::string> kShapes = {"box", "sphere", "capsule", "cylinder", "convex", "mesh"};

		// Per shader parameters shown below the material (stored in shaderParams)
		struct ParamDef { const char* name; float def, mn, mx; };
		std::vector<ParamDef> ParamsFor(const std::string& s)
		{
			if (s == "water") return {{"waveAmplitude", 0.12f, 0, 2}, {"waveSpeed", 0.8f, 0, 4}, {"foam", 0.5f, 0, 1}, {"depthFade", 1.5f, 0.1f, 10}, {"refraction", 0.4f, 0, 1}};
			if (s == "toon") return {{"bands", 3, 2, 8}, {"rim", 0.4f, 0, 1}, {"outline", 0.02f, 0, 0.1f}};
			if (s == "foliage") return {{"windStrength", 0.4f, 0, 2}, {"translucency", 0.5f, 0, 1}};
			if (s == "terrain") return {{"slopeBlend", 0.5f, 0, 1}, {"tiling2", 6, 1, 64}};
			if (s == "glass") return {{"ior", 1.5f, 1, 2.5f}, {"absorption", 0.1f, 0, 1}};
			if (s == "forcefield") return {{"fresnel", 3, 0.5f, 8}, {"scanSpeed", 0.5f, 0, 4}};
			if (s == "dissolve") return {{"amount", 0.0f, 0, 1}, {"edgeWidth", 0.05f, 0, 0.3f}};
			if (s == "ice") return {{"frost", 0.5f, 0, 1}, {"depth", 0.6f, 0, 1}};
			if (s == "pbr") return {{"glint", 0.0f, 0, 1}};
			return {};
		}
	}

	void DrawInspector(Ui& ui)
	{
		Editor& ed = *ui.ed;
		if (!ImGui::Begin("Inspector", &ed.panel.inspector))
		{
			ImGui::End();
			return;
		}
		SceneEntity* e = ed.PrimaryEntity();
		if (!e)
		{
			ImGui::Dummy(ImVec2(0, 40));
			ImGui::PushStyleColor(ImGuiCol_Text, ui::col::Muted());
			const char* t = "Nothing selected";
			ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(t).x) * 0.5f);
			ImGui::TextUnformatted(t);
			ImGui::PopStyleColor();
			ImGui::End();
			return;
		}
		if (ed.selection.size() > 1)
			ImGui::TextDisabled("%zu selected - showing the first", ed.selection.size());
		const bool locked = ed.play != PlayState::Edit;
		if (locked) ImGui::BeginDisabled();
		const std::string before = SceneDoc::EntityToJson(*e).Dump(0);

		// header: active, name, static
		ImGui::Checkbox("##active", &e->active);
		ImGui::SameLine();
		char name[128];
		snprintf(name, sizeof(name), "%s", e->name.c_str());
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 64);
		if (ImGui::InputText("##name", name, sizeof(name)) && name[0]) e->name = name;
		ImGui::SameLine();
		ImGui::Checkbox("Static", &e->isStatic);
		ImGui::TextDisabled("%s", e->id.c_str());

		// tag / layer
		{
			char tag[64];
			snprintf(tag, sizeof(tag), "%s", e->tag.c_str());
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Tag");
			ImGui::SameLine(92);
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
			if (ImGui::InputText("##tag", tag, sizeof(tag))) e->tag = tag;
			ImGui::SameLine();
			const auto& layers = ed.project.Settings().physics.layers;
			std::string cur = e->layer >= 0 && e->layer < (int)layers.size() ? layers[e->layer] : std::to_string(e->layer);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##layer", cur.c_str()))
			{
				for (size_t i = 0; i < layers.size(); ++i)
					if (ImGui::Selectable(layers[i].c_str(), (int)i == e->layer)) e->layer = (int)i;
				ImGui::EndCombo();
			}
		}

		if (Header(ICON_MD_OPEN_WITH, "Transform"))
		{
			Vec3Control("Position", e->transform.position, 0.05f, 0);
			Vec3Control("Rotation", e->transform.rotation, 0.5f, 0, "%.1f");
			Vec3Control("Scale", e->transform.scale, 0.01f, 1);
		}

		if (e->mesh)
		{
			bool rm = false;
			if (Header(ICON_MD_VIEW_IN_AR, "Mesh", &rm))
			{
				SceneMesh& m = *e->mesh;
				if (m.kind == SceneMeshKind::Primitive)
				{
					Combo("Primitive", m.primitive, kPrims);
					Vec3Control("Size", m.size, 0.02f, 1);
					int seg = m.segments;
					ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Segments"); ImGui::SameLine(92); ImGui::SetNextItemWidth(-1);
					if (ImGui::SliderInt("##seg", &seg, 6, 96)) m.segments = seg;
				}
				else
					PathField("Model", m.model, ".gltf;.glb;.obj;.fbx;.ply", ui);
				Check("Cast shadows", m.castShadows);
				ImGui::SameLine();
				Check("Receive shadows", m.receiveShadows);
				Check("Visible", m.visible);
				Float("Cull dist.", m.cullDistance, 1.0f, 0, 100000, m.cullDistance > 0 ? "%.0f m" : "never");
			}
			if (rm) { e->mesh.reset(); e->material.reset(); }
		}

		if (e->material)
		{
			bool rm = false;
			if (Header(ICON_MD_PALETTE, "Material", &rm))
			{
				SceneMaterial& m = *e->material;
				Combo("Shader", m.shader, kShaders);
				float c[4] = {m.color.x, m.color.y, m.color.z, m.color.w};
				ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Colour"); ImGui::SameLine(92); ImGui::SetNextItemWidth(-1);
				if (ImGui::ColorEdit4("##col", c, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) m.color = {c[0], c[1], c[2], c[3]};
				Slider("Roughness", m.roughness, 0, 1);
				Slider("Metalness", m.metalness, 0, 1);
				Slider("Reflectance", m.reflectance, 0, 1);
				ColorEdit3("Emissive", m.emissive);
				Float("Emissive pwr", m.emissiveStrength, 0.05f, 0, 1000);
				PathField("Albedo", m.baseColorMap, ".png;.jpg;.jpeg;.tga;.bmp;.dds;.ktx;.hdr;.webp", ui);
				PathField("Normal", m.normalMap, ".png;.jpg;.jpeg;.tga;.bmp;.dds;.ktx", ui);
				PathField("Rough/Metal", m.roughnessMap, ".png;.jpg;.jpeg;.tga;.bmp;.dds;.ktx", ui);
				PathField("Emissive", m.emissiveMap, ".png;.jpg;.jpeg;.tga;.bmp;.dds;.ktx", ui);
				PathField("Occlusion", m.occlusionMap, ".png;.jpg;.jpeg;.tga;.bmp;.dds;.ktx", ui);
				float tl[2] = {m.tiling.x, m.tiling.y};
				ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Tiling"); ImGui::SameLine(92); ImGui::SetNextItemWidth(-1);
				if (ImGui::DragFloat2("##tile", tl, 0.05f)) m.tiling = {tl[0], tl[1]};
				Check("Double sided", m.doubleSided);
				ImGui::SameLine();
				Check("Cast shadows##m", m.castShadows);
				Check("Alpha blend", m.alphaBlend);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				Float("Alpha cutoff", m.alphaCutoff, 0.01f, 0, 1);
				const std::vector<ParamDef> params = ParamsFor(m.shader);
				if (!params.empty())
				{
					SectionLabel("Shader parameters");
					if (!m.shaderParams.IsObject()) m.shaderParams = Json::MakeObject();
					for (const ParamDef& p : params)
					{
						float v = (float)m.shaderParams.GetDouble(p.name, p.def);
						if (Slider(p.name, v, p.mn, p.mx)) m.shaderParams[p.name] = (double)v;
					}
				}
			}
			if (rm) e->material.reset();
		}

		if (e->light)
		{
			bool rm = false;
			if (Header(ICON_MD_LIGHTBULB, "Light", &rm))
			{
				SceneLight& l = *e->light;
				Combo("Type", l.type, kLights);
				ColorEdit3("Colour", l.color);
				Float("Intensity", l.intensity, 0.1f, 0, 100000);
				if (l.type != "directional") Float("Range", l.range, 0.1f, 0.1f, 10000);
				if (l.type == "spot")
				{
					Slider("Outer angle", l.outerAngle, 5, 170);
					Slider("Inner angle", l.innerAngle, 0, l.outerAngle);
				}
				Check("Cast shadows", l.castShadows);
				Slider("Volumetric", l.volumetric, 0, 5);
			}
			if (rm) e->light.reset();
		}

		if (e->camera)
		{
			bool rm = false;
			if (Header(ICON_MD_VIDEOCAM, "Camera", &rm))
			{
				SceneCamera& c = *e->camera;
				Slider("Field of view", c.fov, 10, 140);
				Float("Near", c.nearPlane, 0.01f, 0.001f, 100);
				Float("Far", c.farPlane, 1.0f, 1, 100000);
				bool main = c.main;
				if (Check("Main camera", main))
				{
					for (SceneEntity& o : ed.scene.entities)
						if (o.camera) o.camera->main = false;
					c.main = main;
				}
			}
			if (rm) e->camera.reset();
		}

		if (e->rigidbody)
		{
			bool rm = false;
			if (Header(ICON_MD_BUILD, "Rigid body", &rm))
			{
				SceneRigidBody& r = *e->rigidbody;
				Combo("Type", r.type, kBodyTypes);
				Combo("Shape", r.shape, kShapes);
				if (r.type == "dynamic") Float("Mass", r.mass, 0.1f, 0.01f, 100000);
				Slider("Friction", r.friction, 0, 2);
				Slider("Bounciness", r.restitution, 0, 1);
				Slider("Lin. damping", r.linearDamping, 0, 1);
				Slider("Ang. damping", r.angularDamping, 0, 1);
				Vec3Control("Shape size", r.shapeSize, 0.02f, 0);
				ImGui::TextDisabled("(0 = fit to the mesh)");
				Vec3Control("Offset", r.shapeOffset, 0.02f, 0);
				Check("Trigger", r.trigger);
				ImGui::SameLine();
				Check("Continuous", r.continuous);
				Check("Character controller (capsule, locked rotation)", r.lockRotation);
				Sep("Simulation");
				if (r.trigger) ImGui::TextDisabled("Trigger: no collision, raises on_trigger_enter / on_trigger_exit");
				Slider("Buoyancy", r.buoyancy, 0, 3);
				Check("Lock to 2D plane", r.lock2D);
				ImGui::SameLine();
				Check("Never sleep", r.alwaysActive);
				Check("Start asleep", r.startDeactivated);
				if (r.lockRotation)
				{
					Slider("Max slope", r.maxSlopeAngle, 0, 89);
					Slider("Gravity factor", r.gravityFactor, 0, 3);
				}
			}
			if (rm) e->rigidbody.reset();
		}

		if (e->vehicle)
		{
			bool rm = false;
			if (Header(ICON_MD_DIRECTIONS_CAR, "Vehicle", &rm))
			{
				SceneVehicle& v = *e->vehicle;
				if (!e->rigidbody || e->rigidbody->type != "dynamic") ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1), "Needs a dynamic Rigid body on this entity");
				Combo("Type", v.type, kVehicleTypes);
				Combo("Wheel test", v.collision, kWheelCollision);
				Vec3Control("Chassis size", v.chassis, 0.02f, 0.05f);
				Float("Wheel radius", v.wheelRadius, 0.01f, 0.05f, 2);
				Float("Wheel width", v.wheelWidth, 0.01f, 0.02f, 1);
				Float("Front offset", v.frontWheelOffset, 0.01f, -2, 2);
				Float("Rear offset", v.rearWheelOffset, 0.01f, -2, 2);
				Float("Engine torque", v.engineTorque, 5, 10, 20000);
				Float("Clutch", v.clutchStrength, 0.1f, 0, 100);
				Slider("Max steer", v.maxSteerAngle, 1, 80);
				Sep("Suspension");
				Float("Min length", v.suspensionMin, 0.01f, 0.01f, 2);
				Float("Max length", v.suspensionMax, 0.01f, 0.02f, 2);
				Float("Frequency", v.suspensionFrequency, 0.05f, 0.1f, 20);
				Float("Damping", v.suspensionDamping, 0.01f, 0, 5);
				if (v.type == "car") Check("Four wheel drive", v.fourWheelDrive);
				else
				{
					Slider("Max roll", v.maxRollAngle, 5, 90);
					Check("Lean control", v.leanControl);
				}
				Sep("Wheel visuals");
				EntityCombo("Front left", v.wheelFL, ed.scene, e->id, "(none)");
				if (v.type == "car") EntityCombo("Front right", v.wheelFR, ed.scene, e->id, "(none)");
				EntityCombo("Rear left", v.wheelRL, ed.scene, e->id, "(none)");
				if (v.type == "car") EntityCombo("Rear right", v.wheelRR, ed.scene, e->id, "(none)");
				ImGui::TextDisabled("Script: entity:drive(throttle, steer, brake, handbrake)");
			}
			if (rm) e->vehicle.reset();
		}

		if (e->joint)
		{
			bool rm = false;
			if (Header(ICON_MD_LINK, "Joint", &rm))
			{
				SceneJoint& j = *e->joint;
				if (!e->rigidbody) ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1), "Needs a Rigid body on this entity");
				Combo("Type", j.type, kJointTypes);
				EntityCombo("Connected to", j.connected, ed.scene, e->id, "(world)");
				ImGui::TextDisabled("Joint frame = this entity's transform (hinge/cone axis Y, slider axis X)");
				if (j.type == "distance") { Float("Min distance", j.minDistance, 0.01f, 0, 1000); Float("Max distance", j.maxDistance, 0.01f, 0, 1000); }
				else if (j.type == "hinge")
				{
					Slider("Min angle", j.minAngle, -180, 180);
					Slider("Max angle", j.maxAngle, -180, 180);
					Float("Motor speed (rad/s)", j.motorSpeed, 0.05f, -100, 100);
				}
				else if (j.type == "cone") Slider("Half cone angle", j.coneAngle, 0, 180);
				else if (j.type == "swingtwist")
				{
					Slider("Swing normal", j.swingNormal, 0, 180);
					Slider("Swing plane", j.swingPlane, 0, 180);
					Slider("Twist min", j.twistMin, -180, 180);
					Slider("Twist max", j.twistMax, -180, 180);
				}
				else if (j.type == "slider")
				{
					Float("Min limit", j.sliderMin, 0.01f, -1000, 1000);
					Float("Max limit", j.sliderMax, 0.01f, -1000, 1000);
					Float("Motor speed", j.sliderSpeed, 0.05f, -100, 100);
					Float("Motor force", j.sliderForce, 1, 0, 100000);
				}
				else if (j.type == "sixdof")
				{
					ImGui::TextUnformatted("Locked axes");
					Check("X", j.lockX); ImGui::SameLine(); Check("Y", j.lockY); ImGui::SameLine(); Check("Z", j.lockZ);
					Check("Rot X", j.lockRotX); ImGui::SameLine(); Check("Rot Y", j.lockRotY); ImGui::SameLine(); Check("Rot Z", j.lockRotZ);
				}
				Float("Break distance", j.breakDistance, 0.05f, 0, 1000);
				ImGui::TextDisabled("(0 = unbreakable)");
				Check("No self collision", j.disableSelfCollision);
			}
			if (rm) e->joint.reset();
		}

		if (e->audio)
		{
			bool rm = false;
			if (Header(ICON_MD_SPEAKER, "Audio source", &rm))
			{
				SceneAudio& a = *e->audio;
				PathField("File", a.file, ".wav;.ogg;.mp3;.flac", ui);
				Slider("Volume", a.volume, 0, 2);
				Check("Loop", a.loop);
				ImGui::SameLine();
				Check("Play on start", a.playOnStart);
				Check("Spatial", a.spatial);
				if (a.spatial)
				{
					Float("Min dist.", a.minDistance, 0.1f, 0, 1000);
					Float("Max dist.", a.maxDistance, 0.5f, 0, 100000);
				}
			}
			if (rm) e->audio.reset();
		}

		if (e->animator)
		{
			bool rm = false;
			if (Header(ICON_MD_ANIMATION, "Animator", &rm))
			{
				SceneAnimator& an = *e->animator;
				const std::vector<wi::ecs::Entity> clips = ui.bridge->Animations(e->id);
				std::vector<std::string> names;
				for (wi::ecs::Entity c : clips)
					if (const wi::scene::NameComponent* n = ui.wscene->names.GetComponent(c)) names.push_back(n->name);
				{
					std::vector<std::string> kf = {"(none)"};
					for (const AssetEntry* a : ui.ed->assets.OfType(AssetType::Animation))
						if (a->extension == ".lanim") kf.push_back(a->path);
					std::string kc = an.autoplayClip.empty() ? "(none)" : an.autoplayClip;
					if (Combo("Keyframe clip", kc, kf)) { an.autoplayClip = kc == "(none)" ? std::string() : kc; ui.ed->Touch(); }
					if (!an.autoplayClip.empty() && ImGui::Checkbox("Loop keyframe clip", &an.autoplayLoop)) ui.ed->Touch();
					if (!an.autoplayClip.empty() && ImGui::SmallButton("Open in Timeline")) { ui.ed->panel.timeline = true; ui.ed->timelineOpen = an.autoplayClip; ui.ed->timeline.rootId = e->id; }
				}
				if (names.empty()) ImGui::TextDisabled("This entity's model has no animation clips.");
				std::vector<std::string> opts = {"(none)"};
				opts.insert(opts.end(), names.begin(), names.end());
				std::string cur = an.autoplay.empty() ? "(none)" : an.autoplay;
				if (Combo("Autoplay", cur, opts)) an.autoplay = cur == "(none)" ? std::string() : cur;
				for (size_t ci = 0; ci < clips.size() && ci < names.size(); ++ci)
				{
					wi::scene::AnimationComponent* ac = ui.wscene->animations.GetComponent(clips[ci]);
					if (!ac) continue;
					ImGui::PushID((int)ci);
					SceneAnimator::Clip c;
					c.name = names[ci];
					bool exists = false;
					for (const SceneAnimator::Clip& x : an.clips) if (x.name == c.name) { c = x; exists = true; }
					const SceneAnimator::Clip before = c;
					ImGui::Separator();
					ImGui::TextUnformatted(c.name.c_str());
					ImGui::SameLine(ImGui::GetContentRegionMax().x - 66);
					if (ac->IsPlaying()) { if (ImGui::SmallButton(ICON_MD_STOP "##p")) ac->Stop(); }
					else if (ImGui::SmallButton(ICON_MD_PLAY_ARROW "##p")) { ac->Play(); }
					ImGui::TextDisabled("%.2f s", ac->end - ac->start);
					Check("Loop", c.loop);
					ImGui::SameLine();
					Check("Ping-pong", c.pingPong);
					Float("Speed", c.speed, 0.01f, 0, 10);
					Slider("Weight", c.weight, 0, 1);
					if (c.name == before.name && (c.loop != before.loop || c.pingPong != before.pingPong || c.speed != before.speed || c.weight != before.weight))
					{
						if (exists) { for (SceneAnimator::Clip& x : an.clips) if (x.name == c.name) x = c; }
						else an.clips.push_back(c);
						// live preview of the settings
						if (c.pingPong) ac->SetPingPong(true); else ac->SetLooped(c.loop);
						ac->speed = c.speed;
						ac->amount = c.weight;
					}
					ImGui::PopID();
				}
				ImGui::TextDisabled("Script: entity:play(clip), entity:animation(name):set_speed(...)");
			}
			if (rm) e->animator.reset();
		}

		if (e->particles)
		{
			bool rm = false;
			if (Header(ICON_MD_GRAIN, "Particle emitter", &rm))
			{
				SceneParticles& p = *e->particles;
				ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Max particles"); ImGui::SameLine(92); ImGui::SetNextItemWidth(-1);
				ImGui::DragInt("##mp", &p.maxParticles, 10, 16, 1000000);
				Float("Rate /s", p.rate, 1.0f, 0, 100000, "%.0f");
				ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Burst at start"); ImGui::SameLine(92); ImGui::SetNextItemWidth(-1);
				ImGui::DragInt("##bs", &p.burstOnCreate, 1, 0, 100000);
				Combo("Shader", p.shader, kParticleShaders);
				Float("Size", p.size, 0.005f, 0.001f, 100);
				Float("Life", p.life, 0.02f, 0.05f, 120);
				Slider("Life random", p.randomLife, 0, 1);
				Vec3Control("Velocity", p.velocity, 0.05f, 0);
				Float("Random", p.randomFactor, 0.01f, 0, 20);
				Float("Normal", p.normalFactor, 0.01f, 0, 20);
				Vec3Control("Gravity", p.gravity, 0.05f, 0);
				Float("Drag", p.drag, 0.002f, 0, 1);
				Float("Mass", p.mass, 0.01f, 0.01f, 100);
				Float("Rotation", p.rotation, 0.01f, -20, 20);
				float sc[2] = {p.scaleX, p.scaleY};
				ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Stretch"); ImGui::SameLine(92); ImGui::SetNextItemWidth(-1);
				if (ImGui::DragFloat2("##st", sc, 0.01f, 0.01f, 20)) { p.scaleX = sc[0]; p.scaleY = sc[1]; }
				float c[4] = {p.color.x, p.color.y, p.color.z, p.color.w};
				ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Colour"); ImGui::SameLine(92); ImGui::SetNextItemWidth(-1);
				if (ImGui::ColorEdit4("##pc", c, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) p.color = {c[0], c[1], c[2], c[3]};
				Slider("Colour random", p.randomColor, 0, 1);
				PathField("Texture", p.texture, ".png;.jpg;.jpeg;.tga;.dds;.ktx;.webp", ui);
				Sep("Sprite sheet");
				ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Frames X/Y/N"); ImGui::SameLine(92); ImGui::SetNextItemWidth(-1);
				int fr[3] = {p.framesX, p.framesY, p.frameCount};
				if (ImGui::DragInt3("##fr", fr, 0.2f, 1, 64)) { p.framesX = fr[0]; p.framesY = fr[1]; p.frameCount = fr[2]; }
				Float("Frame rate", p.frameRate, 0.1f, 0, 120);
				Check("Frame blending", p.frameBlending);
				Sep("Behaviour");
				Check("Sorted", p.sorted);
				ImGui::SameLine();
				Check("Depth collisions", p.collisions);
				if (p.collisions) Slider("Bounce", p.restitution, 0, 1);
				Check("Emit from mesh surface", p.emitFromMesh);
				Check("Play on start", p.playOnStart);
				Slider("Fade in end", p.opacityPeakStart, 0, 1);
				Slider("Fade out start", p.opacityPeakEnd, 0, 1);
			}
			if (rm) e->particles.reset();
		}

		if (e->terrain)
		{
			bool rm = false;
			if (Header(ICON_MD_TERRAIN, "Terrain", &rm))
			{
				SceneTerrain& t = *e->terrain;
				static const int kRes[] = {33, 65, 129, 257, 513, 1025};
				std::string resLabel = std::to_string(t.resolution);
				std::vector<std::string> resOpts;
				for (int r : kRes) resOpts.push_back(std::to_string(r));
				if (Combo("Resolution", resLabel, resOpts)) t.resolution = std::atoi(resLabel.c_str());
				float sz[2] = {t.size.x, t.size.y};
				ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Size (m)"); ImGui::SameLine(92); ImGui::SetNextItemWidth(-1);
				if (ImGui::DragFloat2("##tsz", sz, 1.0f, 8, 8192)) t.size = {sz[0], sz[1]};
				Float("Height", t.height, 0.1f, 1, 2000, "%.1f m");
				Sep(t.heightmap.empty() ? "Procedural noise" : "Procedural noise (ignored: painted heightmap)");
				ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Seed"); ImGui::SameLine(92); ImGui::SetNextItemWidth(-1);
				ImGui::DragInt("##tseed", &t.seed, 1, 0, 1000000);
				Float("Frequency", t.frequency, 0.0005f, 0.0005f, 0.5f, "%.4f");
				ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Octaves"); ImGui::SameLine(92); ImGui::SetNextItemWidth(-1);
				ImGui::SliderInt("##toct", &t.octaves, 1, 10);
				Slider("Persistence", t.persistence, 0.1f, 0.9f);
				Float("Lacunarity", t.lacunarity, 0.01f, 1.2f, 4);
				Slider("Amount", t.noiseAmount, 0, 1.5f);
				Slider("Island edge", t.edgeFalloff, 0, 1);
				if (!t.heightmap.empty())
				{
					ImGui::TextDisabled("Heightmap: %s", t.heightmap.c_str());
					if (ImGui::SmallButton("Reset to noise"))
					{
						fs::Remove(ed.project.Resolve(t.heightmap));
						t.heightmap.clear();
					}
				}
				Sep("Surface");
				Float("UV tiling", t.uvTiling, 0.5f, 1, 1000);
				Slider("Roughness", t.roughness, 0, 1);
				PathField("Detail tex.", t.texture, ".png;.jpg;.jpeg;.tga;.dds;.ktx", ui);
				Check("Collider", t.collider);
				Sep("Layers (colour by height and slope)");
				if (t.layers.empty() && ImGui::SmallButton("Customise default layers")) t.layers = terrain::DefaultLayers();
				if (t.layers.empty()) ImGui::TextDisabled("Default: sand, grass, rock, snow");
				int delL = -1;
				for (size_t li = 0; li < t.layers.size(); ++li)
				{
					SceneTerrain::Layer& L = t.layers[li];
					ImGui::PushID((int)li);
					ImGui::Separator();
					float c4[4] = {L.color.x, L.color.y, L.color.z, 1};
					if (ImGui::ColorEdit3("##lc", c4, ImGuiColorEditFlags_NoInputs)) L.color = {c4[0], c4[1], c4[2], 1};
					ImGui::SameLine();
					char nb[48]; std::snprintf(nb, sizeof nb, "%s", L.name.c_str());
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28);
					if (ImGui::InputText("##ln", nb, sizeof nb)) L.name = nb;
					ImGui::SameLine();
					if (ImGui::SmallButton(ICON_MD_CLOSE)) delL = (int)li;
					Float("Height min", L.minHeight, 0.005f, 0, 1);
					Float("Height max", L.maxHeight, 0.005f, 0, 1);
					Float("Slope min", L.minSlope, 0.5f, 0, 90, "%.0f deg");
					Float("Slope max", L.maxSlope, 0.5f, 0, 90, "%.0f deg");
					Float("Blend", L.blend, 0.002f, 0, 0.5f);
					ImGui::PopID();
				}
				if (delL >= 0) t.layers.erase(t.layers.begin() + delL);
				if (ImGui::SmallButton(ICON_MD_ADD "  Add layer"))
				{
					if (t.layers.empty()) t.layers = terrain::DefaultLayers();
					SceneTerrain::Layer L; L.name = "layer"; t.layers.push_back(L);
				}
				Sep("Sculpt");
				ImGui::Checkbox(ICON_MD_BRUSH "  Sculpt mode (LMB paint, Ctrl invert, Shift smooth)", &ed.terrainTool.active);
				static const std::vector<std::string> kBrushes = {"Raise", "Lower", "Smooth", "Flatten", "Noise"};
				std::string bn = kBrushes[(size_t)std::min(4, std::max(0, ed.terrainTool.brush))];
				if (Combo("Brush", bn, kBrushes))
					ed.terrainTool.brush = (int)(std::find(kBrushes.begin(), kBrushes.end(), bn) - kBrushes.begin());
				Float("Radius", ed.terrainTool.radius, 0.2f, 0.5f, 500, "%.1f m");
				Slider("Strength", ed.terrainTool.strength, 0.02f, 1);
				ImGui::TextDisabled("Strokes save to terrain/<id>.height in the project.");
			}
			if (rm) { e->terrain.reset(); ed.terrainTool.active = false; }
		}

		if (e->navmesh)
		{
			bool rm = false;
			if (Header(ICON_MD_ROUTE, "Navigation mesh", &rm))
			{
				SceneNavMesh& n = *e->navmesh;
				Vec3Control("Volume", n.bounds, 0.5f, 50);
				Float("Cell size", n.cellSize, 0.01f, 0.05f, 2);
				Float("Agent radius", n.agentRadius, 0.01f, 0.05f, 5);
				Float("Agent height", n.agentHeight, 0.02f, 0.3f, 10);
				Slider("Max slope", n.maxSlope, 5, 80);
				Float("Step height", n.stepHeight, 0.01f, 0, 2);
				Check("Bake when the game starts", n.bakeOnStart);
				ImGui::TextDisabled("Surfaces come from entities marked Static.");
				if (ImGui::Button(ICON_MD_ROUTE "  Bake preview", ImVec2(-1, 0))) { ed.navBakeRequest = true; ed.navPreview = true; }
				ImGui::Checkbox("Show navmesh", &ed.navPreview);
				if (!ed.navInfo.empty()) ImGui::TextDisabled("%s", ed.navInfo.c_str());
			}
			if (rm) e->navmesh.reset();
		}

		if (e->navagent)
		{
			bool rm = false;
			if (Header(ICON_MD_DIRECTIONS_WALK, "Navigation agent", &rm))
			{
				SceneNavAgent& a = *e->navagent;
				Float("Speed", a.speed, 0.05f, 0, 100, "%.2f m/s");
				Float("Acceleration", a.acceleration, 0.1f, 0.1f, 200);
				Float("Turn speed", a.turnSpeed, 1, 10, 3600, "%.0f deg/s");
				Float("Stop distance", a.stoppingDistance, 0.01f, 0.02f, 10);
				Float("Radius", a.radius, 0.01f, 0.05f, 5);
				Check("Rotate to movement", a.autoRotate);
				Check("Move the entity", a.autoMove);
				ImGui::TextDisabled("Script: entity:agent_move_to(pos); on_nav_arrive(self)");
			}
			if (rm) e->navagent.reset();
		}

		for (size_t i = 0; i < e->scripts.size(); ++i)
		{
			SceneScript& s = e->scripts[i];
			ImGui::PushID((int)i);
			bool rm = false;
			if (Header(ICON_MD_CODE, fs::FileName(s.file).c_str(), &rm))
			{
				Check("Enabled", s.enabled);
				ImGui::SameLine();
				if (ImGui::SmallButton(ICON_MD_CODE " Edit")) { ed.panel.code = true; ed.OpenFile(s.file); }
				PathField("File", s.file, ".lua", ui);
				// properties: editable list of scalar values
				if (!s.properties.IsObject()) s.properties = Json::MakeObject();
				std::string toRemove;
				for (const Json::Member& m : std::vector<Json::Member>(s.properties.Members().begin(), s.properties.Members().end()))
				{
					ImGui::PushID(m.first.c_str());
					if (m.second.IsNumber())
					{
						float v = (float)m.second.AsDouble();
						ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(m.first.c_str()); ImGui::SameLine(92); ImGui::SetNextItemWidth(-30);
						if (ImGui::DragFloat("##v", &v, 0.02f)) s.properties[m.first] = (double)v;
					}
					else if (m.second.IsBool())
					{
						bool v = m.second.AsBool();
						if (ImGui::Checkbox(m.first.c_str(), &v)) s.properties[m.first] = v;
					}
					else if (m.second.IsString())
					{
						char buf[256];
						snprintf(buf, sizeof(buf), "%s", m.second.AsString().c_str());
						ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(m.first.c_str()); ImGui::SameLine(92); ImGui::SetNextItemWidth(-30);
						if (ImGui::InputText("##v", buf, sizeof(buf))) s.properties[m.first] = std::string(buf);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton(ICON_MD_CLOSE)) toRemove = m.first;
					ImGui::PopID();
				}
				if (!toRemove.empty()) s.properties.MembersMutable().erase(
					std::remove_if(s.properties.MembersMutable().begin(), s.properties.MembersMutable().end(), [&](const Json::Member& m) { return m.first == toRemove; }),
					s.properties.MembersMutable().end());
				static char key[48] = "";
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
				ImGui::InputTextWithHint("##newprop", "new property", key, sizeof(key));
				ImGui::SameLine();
				if (ImGui::SmallButton("number") && key[0]) { s.properties[key] = 0.0; key[0] = 0; }
				ImGui::SameLine();
				if (ImGui::SmallButton("bool") && key[0]) { s.properties[key] = false; key[0] = 0; }
				ImGui::SameLine();
				if (ImGui::SmallButton("text") && key[0]) { s.properties[key] = std::string(); key[0] = 0; }
			}
			ImGui::PopID();
			if (rm) { e->scripts.erase(e->scripts.begin() + i); break; }
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		const float bw = ImGui::GetContentRegionAvail().x;
		if (ImGui::Button(ICON_MD_ADD "  Add component", ImVec2(bw, 28))) ImGui::OpenPopup("addcomp");
		if (ImGui::BeginPopup("addcomp"))
		{
			if (!e->mesh && ImGui::MenuItem("Mesh (primitive)")) { e->mesh = SceneMesh{}; if (!e->material) e->material = SceneMaterial{}; }
			if (!e->material && e->mesh && ImGui::MenuItem("Material")) e->material = SceneMaterial{};
			if (!e->light && ImGui::MenuItem("Light")) e->light = SceneLight{};
			if (!e->camera && ImGui::MenuItem("Camera")) e->camera = SceneCamera{};
			if (!e->rigidbody && ImGui::MenuItem("Rigid body")) e->rigidbody = SceneRigidBody{};
			if (!e->vehicle && e->rigidbody && ImGui::MenuItem("Vehicle")) e->vehicle = SceneVehicle{};
			if (!e->joint && e->rigidbody && ImGui::MenuItem("Joint")) e->joint = SceneJoint{};
			if (!e->audio && ImGui::MenuItem("Audio source")) e->audio = SceneAudio{};
			if (!e->animator && ImGui::MenuItem("Animator")) e->animator = SceneAnimator{};
			if (!e->particles && ImGui::MenuItem("Particle emitter")) e->particles = SceneParticles{};
			if (!e->terrain && ImGui::MenuItem("Terrain")) { e->terrain = SceneTerrain{}; e->isStatic = true; }
			if (!e->navmesh && ImGui::MenuItem("Navigation mesh")) e->navmesh = SceneNavMesh{};
			if (!e->navagent && ImGui::MenuItem("Navigation agent")) e->navagent = SceneNavAgent{};
			ImGui::Separator();
			if (ImGui::BeginMenu("Script"))
			{
				for (const AssetEntry* a : ed.assets.OfType(AssetType::Script))
					if (!a->inPackage && ImGui::MenuItem(a->path.c_str())) e->scripts.push_back({a->path, true, Json()});
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}
		if (locked) ImGui::EndDisabled();
		if (SceneDoc::EntityToJson(*e).Dump(0) != before)
			ed.Touch();
		ImGui::End();
	}
}
